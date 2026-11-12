/* edit.c - text buffer: loading, editing, undo and search
 *
 * Lines are kept as an array of individually allocated strings.  For
 * the size of program this IDE is for (a few hundred lines) that is
 * simpler and faster than a gap buffer, and undo can afford to keep
 * whole-file snapshots.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "edit.h"

static void *xalloc(size_t n)
{
  void *p = calloc(1, n ? n : 1);
  if (!p) { fputs("shkide: out of memory\n", stderr); exit(1); }
  return p;
}

static void *xrealloc(void *p, size_t n)
{
  void *q = realloc(p, n ? n : 1);
  if (!q) { fputs("shkide: out of memory\n", stderr); exit(1); }
  return q;
}

static void line_init(Line *l)
{
  l->cap = 32;
  l->s = xalloc((size_t)l->cap);
  l->len = 0;
  l->s[0] = 0;
}

static void line_reserve(Line *l, int need)
{
  if (need + 1 <= l->cap) return;
  while (l->cap < need + 1) l->cap *= 2;
  l->s = xrealloc(l->s, (size_t)l->cap);
}

static void line_set(Line *l, const char *s, int n)
{
  line_reserve(l, n);
  memcpy(l->s, s, (size_t)n);
  l->s[n] = 0;
  l->len = n;
}

static void buf_reserve(Buf *b, int need)
{
  if (need <= b->cap) return;
  while (b->cap < need) b->cap = b->cap ? b->cap * 2 : 64;
  b->l = xrealloc(b->l, (size_t)b->cap * sizeof(Line));
}

static Line *insert_line_at(Buf *b, int at)
{
  buf_reserve(b, b->n + 1);
  if (at < b->n)
    memmove(&b->l[at+1], &b->l[at], (size_t)(b->n - at) * sizeof(Line));
  line_init(&b->l[at]);
  b->n++;
  return &b->l[at];
}

static void remove_line_at(Buf *b, int at)
{
  if (at < 0 || at >= b->n) return;
  free(b->l[at].s);
  if (at < b->n - 1)
    memmove(&b->l[at], &b->l[at+1], (size_t)(b->n - at - 1) * sizeof(Line));
  b->n--;
  if (b->n == 0) insert_line_at(b, 0);
}

/* ------------------------------------------------------------------ */
void buf_init(Buf *b)
{
  memset(b, 0, sizeof *b);
  insert_line_at(b, 0);
}

static void snaps_free(Snap *s, int n)
{
  int i;
  for (i = 0; i < n; i++) free(s[i].text);
}

void buf_free(Buf *b)
{
  int i;
  for (i = 0; i < b->n; i++) free(b->l[i].s);
  free(b->l);
  snaps_free(b->undo, b->undo_n);
  snaps_free(b->redo, b->redo_n);
  memset(b, 0, sizeof *b);
}

void buf_new(Buf *b)
{
  buf_free(b);
  buf_init(b);
}

char *buf_text(Buf *b, int *len_out)
{
  size_t total = 1;
  char *t;
  size_t at = 0;
  int i;
  for (i = 0; i < b->n; i++) total += (size_t)b->l[i].len + 1;
  t = xalloc(total);
  for (i = 0; i < b->n; i++) {
    memcpy(t + at, b->l[i].s, (size_t)b->l[i].len);
    at += (size_t)b->l[i].len;
    if (i < b->n - 1 || !b->no_trailing_nl) t[at++] = '\n';
  }
  t[at] = 0;
  if (len_out) *len_out = (int)at;
  return t;
}

void buf_set_text(Buf *b, const char *t)
{
  const char *p = t;
  int i;
  for (i = 0; i < b->n; i++) free(b->l[i].s);
  b->n = 0;
  for (;;) {
    const char *nl = strchr(p, '\n');
    int len = nl ? (int)(nl - p) : (int)strlen(p);
    Line *l;
    if (len && p[len-1] == '\r') len--;
    l = insert_line_at(b, b->n);
    line_set(l, p, len);
    if (!nl) { b->no_trailing_nl = 1; break; }
    p = nl + 1;
    if (!*p) { b->no_trailing_nl = 0; break; }
  }
  if (b->n == 0) insert_line_at(b, 0);
  buf_clamp(b);
}

int buf_load(Buf *b, const char *path)
{
  FILE *f = fopen(path, "rb");
  long n;
  char *t;

  if (!f) return 0;
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n < 0) { fclose(f); return 0; }
  t = xalloc((size_t)n + 1);
  if (n && fread(t, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(t); return 0; }
  t[n] = 0;
  fclose(f);

  buf_free(b);
  buf_init(b);
  if (n) buf_set_text(b, t);
  free(t);
  snprintf(b->path, sizeof b->path, "%s", path);
  b->dirty = 0;
  b->cy = b->cx = b->top = b->left = 0;
  return 1;
}

int buf_save(Buf *b, const char *path)
{
  FILE *f;
  int len;
  char *t;
  char tmp[MAX_PATH_LEN + 16];

  /* write to a neighbour file and rename, so a full SD card or a
   * power cut cannot leave a half-written program behind */
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  f = fopen(tmp, "wb");
  if (!f) return 0;
  t = buf_text(b, &len);
  if (len && fwrite(t, 1, (size_t)len, f) != (size_t)len) {
    free(t); fclose(f); remove(tmp); return 0;
  }
  free(t);
  if (fflush(f) != 0) { fclose(f); remove(tmp); return 0; }
  if (fclose(f) != 0) { remove(tmp); return 0; }
  if (rename(tmp, path) != 0) { remove(tmp); return 0; }

  if (path != b->path) snprintf(b->path, sizeof b->path, "%s", path);
  b->dirty = 0;
  return 1;
}

/* ------------------------------------------------------------------ */
/* undo                                                                */
/* ------------------------------------------------------------------ */
static void snap_push(Snap *stack, int *n, Buf *b)
{
  Snap s;
  s.text = buf_text(b, NULL);
  s.cy = b->cy;
  s.cx = b->cx;
  if (*n == UNDO_MAX) {
    free(stack[0].text);
    memmove(&stack[0], &stack[1], (size_t)(UNDO_MAX - 1) * sizeof(Snap));
    (*n)--;
  }
  stack[(*n)++] = s;
}

void buf_push_undo(Buf *b)
{
  snap_push(b->undo, &b->undo_n, b);
  snaps_free(b->redo, b->redo_n);
  b->redo_n = 0;
}

int buf_undo(Buf *b)
{
  Snap s;
  if (b->undo_n == 0) return 0;
  snap_push(b->redo, &b->redo_n, b);
  s = b->undo[--b->undo_n];
  buf_set_text(b, s.text);
  b->cy = s.cy;
  b->cx = s.cx;
  free(s.text);
  b->dirty = 1;
  buf_clamp(b);
  return 1;
}

int buf_redo(Buf *b)
{
  Snap s;
  if (b->redo_n == 0) return 0;
  snap_push(b->undo, &b->undo_n, b);
  s = b->redo[--b->redo_n];
  buf_set_text(b, s.text);
  b->cy = s.cy;
  b->cx = s.cx;
  free(s.text);
  b->dirty = 1;
  buf_clamp(b);
  return 1;
}

/* ------------------------------------------------------------------ */
/* editing                                                             */
/* ------------------------------------------------------------------ */
void buf_clamp(Buf *b)
{
  if (b->n == 0) insert_line_at(b, 0);
  if (b->cy < 0) b->cy = 0;
  if (b->cy >= b->n) b->cy = b->n - 1;
  if (b->cx < 0) b->cx = 0;
  if (b->cx > b->l[b->cy].len) b->cx = b->l[b->cy].len;
}

void buf_insert_char(Buf *b, int c)
{
  Line *l;
  buf_clamp(b);
  l = &b->l[b->cy];
  line_reserve(l, l->len + 1);
  memmove(l->s + b->cx + 1, l->s + b->cx, (size_t)(l->len - b->cx + 1));
  l->s[b->cx] = (char)c;
  l->len++;
  b->cx++;
  b->dirty = 1;
}

void buf_insert_str(Buf *b, const char *s)
{
  while (*s) {
    if (*s == '\n') buf_newline(b);
    else buf_insert_char(b, (unsigned char)*s);
    s++;
  }
}

/* copy the leading whitespace of the current line onto the new one */
void buf_newline(Buf *b)
{
  Line *l, *nl;
  int indent = 0;
  int tail;

  buf_clamp(b);
  l = &b->l[b->cy];
  while (indent < l->len && (l->s[indent] == ' ' || l->s[indent] == '\t')) indent++;
  if (indent > b->cx) indent = b->cx;
  tail = l->len - b->cx;

  nl = insert_line_at(b, b->cy + 1);
  l = &b->l[b->cy];              /* insert may have moved the array */
  line_reserve(nl, indent + tail);
  memcpy(nl->s, l->s, (size_t)indent);
  memcpy(nl->s + indent, l->s + b->cx, (size_t)tail);
  nl->len = indent + tail;
  nl->s[nl->len] = 0;

  l->len = b->cx;
  l->s[l->len] = 0;

  b->cy++;
  b->cx = indent;
  b->dirty = 1;
}

void buf_backspace(Buf *b)
{
  Line *l;
  buf_clamp(b);
  l = &b->l[b->cy];
  if (b->cx > 0) {
    memmove(l->s + b->cx - 1, l->s + b->cx, (size_t)(l->len - b->cx + 1));
    l->len--;
    b->cx--;
    b->dirty = 1;
    return;
  }
  if (b->cy == 0) return;
  {
    Line *prev = &b->l[b->cy - 1];
    int at = prev->len;
    line_reserve(prev, prev->len + l->len);
    memcpy(prev->s + prev->len, l->s, (size_t)l->len + 1);
    prev->len += l->len;
    remove_line_at(b, b->cy);
    b->cy--;
    b->cx = at;
    b->dirty = 1;
  }
}

void buf_delete(Buf *b)
{
  Line *l;
  buf_clamp(b);
  l = &b->l[b->cy];
  if (b->cx < l->len) {
    memmove(l->s + b->cx, l->s + b->cx + 1, (size_t)(l->len - b->cx));
    l->len--;
    b->dirty = 1;
    return;
  }
  if (b->cy >= b->n - 1) return;
  {
    Line *next = &b->l[b->cy + 1];
    line_reserve(l, l->len + next->len);
    memcpy(l->s + l->len, next->s, (size_t)next->len + 1);
    l->len += next->len;
    remove_line_at(b, b->cy + 1);
    b->dirty = 1;
  }
}

void buf_delete_line(Buf *b)
{
  buf_clamp(b);
  remove_line_at(b, b->cy);
  if (b->cy >= b->n) b->cy = b->n - 1;
  b->cx = 0;
  b->dirty = 1;
}

void buf_indent(Buf *b, int width)
{
  Line *l;
  int i;
  buf_clamp(b);
  l = &b->l[b->cy];
  line_reserve(l, l->len + width);
  memmove(l->s + width, l->s, (size_t)l->len + 1);
  for (i = 0; i < width; i++) l->s[i] = ' ';
  l->len += width;
  b->cx += width;
  b->dirty = 1;
}

void buf_unindent(Buf *b, int width)
{
  Line *l;
  int k = 0;
  buf_clamp(b);
  l = &b->l[b->cy];
  while (k < width && k < l->len && l->s[k] == ' ') k++;
  if (!k) return;
  memmove(l->s, l->s + k, (size_t)(l->len - k + 1));
  l->len -= k;
  b->cx -= k;
  if (b->cx < 0) b->cx = 0;
  b->dirty = 1;
}

/* ------------------------------------------------------------------ */
static const char *stristr(const char *hay, const char *needle)
{
  size_t nl = strlen(needle);
  if (!nl) return hay;
  for (; *hay; hay++)
    if (strncasecmp(hay, needle, nl) == 0) return hay;
  return NULL;
}

int buf_find(Buf *b, const char *needle, int from_y, int from_x,
             int *fy, int *fx, int wrap)
{
  int y;
  const char *p;

  if (!needle || !*needle || b->n == 0) return 0;
  if (from_y < 0) from_y = 0;
  if (from_y >= b->n) from_y = b->n - 1;

  if (from_x <= b->l[from_y].len) {
    p = stristr(b->l[from_y].s + from_x, needle);
    if (p) { *fy = from_y; *fx = (int)(p - b->l[from_y].s); return 1; }
  }
  for (y = from_y + 1; y < b->n; y++) {
    p = stristr(b->l[y].s, needle);
    if (p) { *fy = y; *fx = (int)(p - b->l[y].s); return 1; }
  }
  if (!wrap) return 0;
  for (y = 0; y <= from_y && y < b->n; y++) {
    p = stristr(b->l[y].s, needle);
    if (p) { *fy = y; *fx = (int)(p - b->l[y].s); return 1; }
  }
  return 0;
}
