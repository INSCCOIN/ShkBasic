/* lex.c - source text to a flat token array
 *
 * The whole program becomes one Tok[] so that every jump target is a
 * single integer index.  Line boundaries survive as T_EOL tokens that
 * carry the source line number for diagnostics.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "shk.h"

const char *kw_name[KW_COUNT] = {
#define X(id,txt) txt,
  KEYWORD_LIST
#undef X
};

typedef struct {
  Tok *t;
  int  n, cap;
} TokBuf;

static void tb_push(TokBuf *b, Tok t)
{
  if (b->n == b->cap) {
    b->cap = b->cap ? b->cap * 2 : 256;
    b->t = shk_realloc(b->t, (size_t)b->cap * sizeof(Tok));
  }
  b->t[b->n++] = t;
}

static int kw_lookup(const char *s, int len)
{
  int i, j;
  for (i = 0; i < KW_COUNT; i++) {
    const char *k = kw_name[i];
    for (j = 0; j < len; j++) {
      if (!k[j] || toupper((unsigned char)s[j]) != k[j]) break;
    }
    if (j == len && !k[len]) return i;
  }
  return -1;
}

/* STEPKW and DECR are internal placeholders; never match them in source */
static int kw_usable(int k)
{
  return k >= 0 && k != KW_STEPKW;
}

static int ident_char(int c)
{
  return isalnum(c) || c == '_' || c == '.';
}

/* Parse a numeric literal starting at *p.  Advances *p. */
static void lex_number(const char **p, Tok *t)
{
  const char *s = *p;
  char buf[64];
  int n = 0;
  int isfloat = 0;

  if (s[0] == '&') {
    int base = 0;
    const char *d;
    uint64_t acc = 0;
    char c = (char)toupper((unsigned char)s[1]);
    if (c == 'H') base = 16;
    else if (c == 'B') base = 2;
    else if (c == 'O') base = 8;
    else base = 16;                 /* &nnn is hex in MMBasic */
    d = s + (base == 16 && c != 'H' ? 1 : 2);
    while (*d) {
      int v;
      if (isdigit((unsigned char)*d)) v = *d - '0';
      else if (isalpha((unsigned char)*d)) v = toupper((unsigned char)*d) - 'A' + 10;
      else break;
      if (v >= base) break;
      acc = acc * (uint64_t)base + (uint64_t)v;
      d++;
    }
    t->type = T_INT;
    t->inum = (int64_t)acc;
    *p = d;
    return;
  }

  while (isdigit((unsigned char)*s) && n < 60) buf[n++] = *s++;
  if (*s == '.') { isfloat = 1; buf[n++] = *s++;
    while (isdigit((unsigned char)*s) && n < 60) buf[n++] = *s++; }
  if (*s == 'e' || *s == 'E') {
    const char *save = s;
    int m = n;
    buf[n++] = *s++;
    if (*s == '+' || *s == '-') buf[n++] = *s++;
    if (isdigit((unsigned char)*s)) {
      isfloat = 1;
      while (isdigit((unsigned char)*s) && n < 60) buf[n++] = *s++;
    } else { s = save; n = m; }
  }
  buf[n] = 0;

  if (isfloat) {
    t->type = T_NUM;
    t->num = strtod(buf, NULL);
  } else {
    char *end;
    long long v = strtoll(buf, &end, 10);
    /* overflow of a plain integer literal degrades to float */
    if (strlen(buf) > 18) { t->type = T_NUM; t->num = strtod(buf, NULL); }
    else { t->type = T_INT; t->inum = v; }
  }
  *p = s;
}

int lex_source(const char *src, Tok **out, int *nout)
{
  TokBuf b;
  const char *p = src;
  int line = 1;
  int stmt_start = 1;       /* at the beginning of a statement        */
  int line_start = 1;       /* at the beginning of a physical line    */
  int in_data = 0;          /* DATA swallows commas as separators     */

  memset(&b, 0, sizeof b);

  while (*p) {
    Tok t;
    memset(&t, 0, sizeof t);
    t.line = line;
    t.link = -1;
    t.link2 = -1;

    if (*p == '\r') { p++; continue; }

    if (*p == '\n') {
      t.type = T_EOL;
      tb_push(&b, t);
      p++; line++;
      stmt_start = line_start = 1;
      in_data = 0;
      continue;
    }

    if (*p == ' ' || *p == '\t') { p++; continue; }

    /* comments ---------------------------------------------------- */
    if (*p == '\'') {
      while (*p && *p != '\n') p++;
      continue;
    }

    /* leading line number ----------------------------------------- */
    if (line_start && isdigit((unsigned char)*p)) {
      const char *q = p;
      while (isdigit((unsigned char)*q)) q++;
      /* a bare integer at line start followed by space or a letter is
       * a line number; "10" alone on a line is also one             */
      if (*q == ' ' || *q == '\t' || *q == '\n' || *q == 0 || isalpha((unsigned char)*q)) {
        t.type = T_LINENO;
        t.inum = strtoll(p, NULL, 10);
        tb_push(&b, t);
        p = q;
        line_start = 0;
        stmt_start = 1;
        continue;
      }
    }
    line_start = 0;

    /* string literal ----------------------------------------------- */
    if (*p == '"') {
      const char *q = ++p;
      char *buf;
      int n = 0;
      while (*q && *q != '"' && *q != '\n') { q++; n++; }
      buf = shk_alloc((size_t)n + 1);
      memcpy(buf, p, (size_t)n);
      buf[n] = 0;
      t.type = T_STR;
      t.str = buf;
      tb_push(&b, t);
      p = q;
      if (*p == '"') p++;
      stmt_start = 0;
      continue;
    }

    /* number -------------------------------------------------------- */
    if (isdigit((unsigned char)*p) ||
        (*p == '.' && isdigit((unsigned char)p[1])) ||
        (*p == '&' && p[1])) {
      lex_number(&p, &t);
      tb_push(&b, t);
      stmt_start = 0;
      continue;
    }

    /* identifier or keyword ----------------------------------------- */
    if (isalpha((unsigned char)*p) || *p == '_') {
      const char *q = p;
      int len, kw;
      char name[MAXNAME];
      while (ident_char((unsigned char)*q)) q++;
      len = (int)(q - p);
      /* type suffix belongs to the name */
      if (*q == '$' || *q == '%' || *q == '!') { q++; len++; }
      if (len >= MAXNAME) len = MAXNAME - 1;
      memcpy(name, p, (size_t)len);
      name[len] = 0;

      /* REM comment */
      if (len == 3 && !strncasecmp(name, "REM", 3)) {
        while (*p && *p != '\n') p++;
        continue;
      }

      kw = in_data ? -1 : kw_lookup(name, len);
      if (kw_usable(kw)) {
        t.type = T_KW;
        t.kw = (uint8_t)kw;
        t.str = shk_strdup(name);
        if (kw == KW_DATA) in_data = 1;
        tb_push(&b, t);
        p = q;
        stmt_start = 0;
        continue;
      }

      /* label: identifier immediately followed by ':' at stmt start */
      if (stmt_start && *q == ':' && name[len-1] != '$' &&
          name[len-1] != '%' && name[len-1] != '!') {
        t.type = T_LABEL;
        t.str = shk_strdup(name);
        tb_push(&b, t);
        p = q + 1;
        stmt_start = 1;
        continue;
      }

      t.type = T_IDENT;
      t.str = shk_strdup(name);
      tb_push(&b, t);
      p = q;
      stmt_start = 0;
      continue;
    }

    /* operators and punctuation -------------------------------------- */
    {
      int c = (unsigned char)*p;
      int two = 0;
      switch (c) {
        case '<':
          if (p[1] == '=') { t.op = OP_LE; two = 1; }
          else if (p[1] == '>') { t.op = OP_NE; two = 1; }
          else if (p[1] == '<') { t.op = OP_SHL; two = 1; }
          else t.op = '<';
          break;
        case '>':
          if (p[1] == '=') { t.op = OP_GE; two = 1; }
          else if (p[1] == '<') { t.op = OP_NE; two = 1; }
          else if (p[1] == '>') { t.op = OP_SHR; two = 1; }
          else t.op = '>';
          break;
        case '=':
          if (p[1] == '<') { t.op = OP_LE; two = 1; }
          else if (p[1] == '>') { t.op = OP_GE; two = 1; }
          else t.op = '=';
          break;
        case ':':
          t.type = T_COLON;
          tb_push(&b, t);
          p++;
          stmt_start = 1;
          in_data = 0;
          continue;
        case '?':
          /* ? is shorthand for PRINT */
          t.type = T_KW;
          t.kw = KW_PRINT;
          t.str = shk_strdup("PRINT");
          tb_push(&b, t);
          p++;
          stmt_start = 0;
          continue;
        default:
          t.op = c;
          break;
      }
      t.type = T_OP;
      tb_push(&b, t);
      p += two ? 2 : 1;
      stmt_start = 0;
      continue;
    }
  }

  {
    Tok t;
    memset(&t, 0, sizeof t);
    t.line = line; t.link = -1; t.link2 = -1;
    t.type = T_EOL;
    tb_push(&b, t);
    t.type = T_EOF;
    tb_push(&b, t);
  }

  *out = b.t;
  *nout = b.n;
  return 0;
}

const char *tok_text(const Tok *t)
{
  static char buf[64];
  switch (t->type) {
    case T_EOF:    return "end of program";
    case T_EOL:    return "end of line";
    case T_COLON:  return ":";
    case T_NUM:    snprintf(buf, sizeof buf, "%g", t->num); return buf;
    case T_INT:    snprintf(buf, sizeof buf, "%lld", (long long)t->inum); return buf;
    case T_STR:    return t->str ? t->str : "\"\"";
    case T_IDENT:
    case T_LABEL:
    case T_KW:     return t->str ? t->str : "?";
    case T_LINENO: snprintf(buf, sizeof buf, "%lld", (long long)t->inum); return buf;
    case T_OP:
      switch (t->op) {
        case OP_LE:  return "<=";
        case OP_GE:  return ">=";
        case OP_NE:  return "<>";
        case OP_SHL: return "<<";
        case OP_SHR: return ">>";
        default: buf[0] = (char)t->op; buf[1] = 0; return buf;
      }
  }
  return "?";
}
