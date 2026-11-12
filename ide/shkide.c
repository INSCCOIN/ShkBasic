/* shkide.c - a terminal IDE for shkbasic
 *
 * Runs over SSH and on the SharkDeck's own console, because it is
 * nothing but termios and ANSI escapes.  Layout adapts: below 60
 * columns the file pane becomes a full-screen overlay instead of a
 * sidebar, which is what the deck's 3.5" panel needs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <libgen.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <termios.h>
#include "term.h"
#include "edit.h"
#include "browser.h"
#include "syntax.h"

#define IDE_VERSION "1.0"
#define TABW 2
#define SIDEBAR 22
#define NARROW 70          /* below this the sidebar becomes an overlay */
#define OUT_MAX 400

enum { FOCUS_EDIT, FOCUS_BROWSE };

static Buf      buf;
static Browser  br;
static int      focus = FOCUS_EDIT;
static int      show_browser = 1;
static int      out_h = 8;          /* 0 hides the output pane */
static char     msg[256];
static char     find_term[128];
static char     runner[MAX_PATH_LEN * 2];

static char   **outl = NULL;
static int      outn = 0;
static int      out_top = 0;
static int      err_line = 0;       /* 1-based, 0 when none */
static int      help_open = 0;

/* ------------------------------------------------------------------ */
/* output pane                                                         */
/* ------------------------------------------------------------------ */
static void out_clear(void)
{
  int i;
  for (i = 0; i < outn; i++) free(outl[i]);
  outn = 0;
  out_top = 0;
  err_line = 0;
}

static void out_add(const char *s)
{
  char *copy;
  if (outn == OUT_MAX) {
    free(outl[0]);
    memmove(&outl[0], &outl[1], (size_t)(OUT_MAX - 1) * sizeof(char *));
    outn--;
  }
  if (!outl) outl = calloc(OUT_MAX, sizeof(char *));
  copy = strdup(s ? s : "");
  outl[outn++] = copy;

  /* "error on line 12:" and "line 12:" both mean jump there */
  if (!err_line) {
    const char *p = strstr(s, "line ");
    if (p && (p == s || !isalpha((unsigned char)p[-1]))) {
      int n = atoi(p + 5);
      if (n > 0) err_line = n;
    }
  }
}

static void out_addf(const char *fmt, ...)
{
  char b[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  out_add(b);
}

/* ------------------------------------------------------------------ */
/* layout                                                              */
/* ------------------------------------------------------------------ */
static int narrow(void) { return term_cols() < NARROW; }

/* On a narrow screen there is no room for a sidebar, so the file pane
 * takes over the whole main area while it has focus. */
static int browser_overlay(void)
{
  return narrow() && show_browser && focus == FOCUS_BROWSE;
}

static int sidebar_w(void)
{
  int w;
  if (!show_browser || narrow()) return 0;
  w = term_cols() / 3;
  return w < SIDEBAR ? w : SIDEBAR;
}

static int main_top(void) { return 1; }

static int main_bottom(void)      /* last row of the editor area */
{
  int h = term_rows();
  int bottom = h - 2;             /* status line is h-1 */
  if (out_h > 0) bottom -= out_h + 1;
  if (bottom < main_top()) bottom = main_top();
  return bottom;
}

static int edit_rows(void) { return main_bottom() - main_top() + 1; }

static int gutter_w(void)
{
  int n = buf.n, d = 1;
  while (n >= 10) { n /= 10; d++; }
  if (d < 3) d = 3;
  return d + 1;
}

static int edit_x0(void) { return sidebar_w() ? sidebar_w() + 1 : 0; }
static int edit_w(void)  { return term_cols() - edit_x0() - gutter_w(); }

/* display column of a byte offset, expanding tabs */
static int dcol_of(const Line *l, int cx)
{
  int i, c = 0;
  for (i = 0; i < cx && i < l->len; i++)
    c += (l->s[i] == '\t') ? (TABW - (c % TABW)) : 1;
  return c;
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */
static int syn_colour(int cls)
{
  switch (cls) {
    case SYN_KEYWORD: return C_BYELLOW;
    case SYN_BUILTIN: return C_BCYAN;
    case SYN_STRING:  return C_BGREEN;
    case SYN_NUMBER:  return C_BMAGENTA;
    case SYN_COMMENT: return C_BBLACK;
    case SYN_LABEL:   return C_BRED;
    case SYN_OP:      return C_WHITE;
    default:          return C_DEFAULT;
  }
}

static void draw_header(void)
{
  const char *name = buf.path[0] ? buf.path : "(untitled)";
  const char *base = strrchr(name, '/');
  char left[MAX_PATH_LEN + 64];
  int w = term_cols();
  int i;

  if (base && buf.path[0]) base++; else base = name;
  snprintf(left, sizeof left, " shkide  %s%s", base, buf.dirty ? " *" : "");

  term_move(0, 0);
  term_set(C_BLACK, C_CYAN, 1);
  for (i = 0; i < w; i++) term_putc(' ');
  term_move(0, 0);
  term_put(left);
  if (msg[0]) {
    int mlen = (int)strlen(msg);
    int at = w - mlen - 1;
    if (at > (int)strlen(left) + 2) { term_move(0, at); term_put(msg); }
  }
  term_reset_attr();
}

static void draw_sidebar(void)
{
  int overlay = browser_overlay();
  int bw = overlay ? term_cols() : sidebar_w();
  int top = main_top(), bot = main_bottom();
  int rows = bot - top + 1;
  int i;
  char label[64];

  if (!bw) return;

  if (br.sel < br.top) br.top = br.sel;
  if (br.sel >= br.top + rows - 1) br.top = br.sel - rows + 2;
  if (br.top < 0) br.top = 0;

  term_move(top, 0);
  term_set(focus == FOCUS_BROWSE ? C_BWHITE : C_BBLACK, C_DEFAULT, 1);
  snprintf(label, sizeof label, "FILES%s", br.show_all ? " (all)" : "");
  term_printf("%-*.*s", bw, bw, label);
  term_reset_attr();

  for (i = 1; i < rows; i++) {
    int idx = br.top + i - 1;
    term_move(top + i, 0);
    if (idx >= br.n) {
      term_printf("%*s", bw, "");
      continue;
    }
    {
      FEnt *e = &br.e[idx];
      char nm[BR_NAME + 2];
      int sel = (idx == br.sel);
      snprintf(nm, sizeof nm, "%s%s", e->name, e->isdir ? "/" : "");
      if (sel && focus == FOCUS_BROWSE) term_set(C_BLACK, C_WHITE, 0);
      else if (sel) term_set(C_BWHITE, C_BBLACK, 0);
      else if (e->isdir) term_set(C_BBLUE, C_DEFAULT, 1);
      else term_set(C_DEFAULT, C_DEFAULT, 0);
      term_printf(" %-*.*s", bw - 1, bw - 1, nm);
      term_reset_attr();
    }
  }

  if (overlay) return;

  /* vertical rule between the panes */
  term_set(C_BBLACK, C_DEFAULT, 0);
  for (i = 0; i < rows; i++) { term_move(top + i, bw); term_putc('|'); }
  term_reset_attr();
}

static void draw_editor(void)
{
  int top = main_top(), bot = main_bottom();
  int rows = bot - top + 1;
  int x0 = edit_x0();
  int gw = gutter_w();
  int w = edit_w();
  int r;
  unsigned char attr[4096];

  if (w < 4) w = 4;

  /* keep the cursor on screen */
  if (buf.cy < buf.top) buf.top = buf.cy;
  if (buf.cy >= buf.top + rows) buf.top = buf.cy - rows + 1;
  if (buf.top < 0) buf.top = 0;
  {
    int dc = dcol_of(&buf.l[buf.cy], buf.cx);
    if (dc < buf.left) buf.left = dc;
    if (dc >= buf.left + w) buf.left = dc - w + 1;
    if (buf.left < 0) buf.left = 0;
  }

  for (r = 0; r < rows; r++) {
    int y = buf.top + r;
    term_move(top + r, x0);

    if (y >= buf.n) {
      term_set(C_BBLACK, C_DEFAULT, 0);
      term_printf("%*s", gw, "~");
      term_reset_attr();
      term_printf("%*s", w, "");
      continue;
    }

    /* gutter */
    if (err_line && y + 1 == err_line) term_set(C_BWHITE, C_RED, 1);
    else if (y == buf.cy) term_set(C_BWHITE, C_DEFAULT, 1);
    else term_set(C_BBLACK, C_DEFAULT, 0);
    term_printf("%*d ", gw - 1, y + 1);
    term_reset_attr();

    /* text with highlighting, clipped to the horizontal window */
    {
      Line *l = &buf.l[y];
      int n = l->len > (int)sizeof attr ? (int)sizeof attr : l->len;
      int i, dc = 0, printed = 0, cur = -1;
      syn_line(l->s, n, attr);
      for (i = 0; i < n && printed < w; i++) {
        int cls = attr[i];
        int ch = (unsigned char)l->s[i];
        int width = (ch == '\t') ? (TABW - (dc % TABW)) : 1;
        int k;
        for (k = 0; k < width; k++) {
          if (dc + k < buf.left) continue;
          if (printed >= w) break;
          if (cls != cur) { term_set(syn_colour(cls), C_DEFAULT, cls == SYN_KEYWORD); cur = cls; }
          term_putc((ch == '\t' || ch < 32) ? ' ' : ch);
          printed++;
        }
        dc += width;
      }
      if (cur != -1) term_reset_attr();
      while (printed < w) { term_putc(' '); printed++; }
    }
  }
}

static void draw_output(void)
{
  int h = term_rows();
  int sep = h - 2 - out_h;
  int i;
  int w = term_cols();

  if (out_h <= 0) return;

  term_move(sep, 0);
  term_set(C_BLACK, C_BBLACK, 0);
  {
    char label[80];
    snprintf(label, sizeof label, " OUTPUT %s",
             err_line ? "  ^E jumps to the error " : "");
    term_printf("%-*.*s", w, w, label);
  }
  term_reset_attr();

  if (out_top > outn - out_h) out_top = outn - out_h;
  if (out_top < 0) out_top = 0;

  for (i = 0; i < out_h; i++) {
    int idx = out_top + i;
    term_move(sep + 1 + i, 0);
    if (idx >= outn) { term_printf("%*s", w, ""); continue; }
    {
      const char *s = outl[idx];
      int isterr = (strstr(s, "shkide:") == s) || strstr(s, "error") != NULL;
      term_set(isterr ? C_BRED : C_DEFAULT, C_DEFAULT, 0);
      term_printf("%-*.*s", w, w, s);
      term_reset_attr();
    }
  }
}

static void draw_status(void)
{
  int h = term_rows(), w = term_cols();
  char right[64];
  char left[256];
  int i;

  term_move(h - 1, 0);
  term_set(C_BLACK, C_WHITE, 0);
  for (i = 0; i < w; i++) term_putc(' ');

  if (focus == FOCUS_BROWSE && w < 70)
    snprintf(left, sizeof left, " Enter open  Bksp up  n new  d del  Tab edit");
  else if (focus == FOCUS_BROWSE)
    snprintf(left, sizeof left,
             " Enter open  Bksp up  n new  d del  r rename  a all  Tab editor  ^Q quit");
  else if (w < 70)
    snprintf(left, sizeof left, " ^S save  F5 run  ^O files  F1 help");
  else
    snprintf(left, sizeof left,
             " ^S save  F5 run  F6 run+input  ^O files  ^F find  ^G goto  F1 help  ^Q quit");

  snprintf(right, sizeof right, "   %d:%d ", buf.cy + 1, buf.cx + 1);
  term_move(h - 1, 0);
  term_printf("%-*.*s", w - (int)strlen(right), w - (int)strlen(right), left);
  term_move(h - 1, w - (int)strlen(right));
  term_put(right);
  term_reset_attr();
}

static const char *help_text[] = {
  "shkide " IDE_VERSION " - keys",
  "",
  "  File        ^S save     ^A save as   ^N new      ^O file pane",
  "  Edit        ^Z undo     ^Y redo      ^K cut line ^D duplicate",
  "              Tab indent  Shift-Tab unindent",
  "  Move        arrows  Home  End  PgUp  PgDn  ^Up/^Dn page",
  "  Search      ^F find     F3 find next  ^G goto line",
  "  Run         F5 run (output pane)      F6 run with the terminal",
  "              ^E jump to the reported error line",
  "  View        ^L cycle the output pane height",
  "  Quit        ^Q",
  "",
  "  File pane:  Enter open   Bksp parent   n new   d delete",
  "              r rename     a show all    Tab back to the editor",
  "",
  "  F5 saves first, then runs the file, so line numbers in errors",
  "  line up with what you see.  F6 hands the program the real",
  "  terminal, which is what INPUT and INKEY$ need.",
  "",
  "  press any key",
  NULL
};

static void draw_help(void)
{
  int n = 0, i;
  int w = 0, h, x, y;
  while (help_text[n]) {
    int l = (int)strlen(help_text[n]);
    if (l > w) w = l;
    n++;
  }
  w += 4;
  h = n + 2;
  if (w > term_cols()) w = term_cols();
  if (h > term_rows()) h = term_rows();
  x = (term_cols() - w) / 2;
  y = (term_rows() - h) / 2;
  if (x < 0) x = 0;
  if (y < 0) y = 0;

  for (i = 0; i < h; i++) {
    term_move(y + i, x);
    term_set(C_BWHITE, C_BLUE, 0);
    if (i == 0 || i == h - 1) term_printf("%-*.*s", w, w, "");
    else {
      char linebuf[1152];
      snprintf(linebuf, sizeof linebuf, "  %s", help_text[i-1] ? help_text[i-1] : "");
      term_printf("%-*.*s", w, w, linebuf);
    }
    term_reset_attr();
  }
}

static void redraw(void)
{
  int cr, cc;

  term_begin();
  draw_header();
  if (browser_overlay()) {
    draw_sidebar();
  } else {
    draw_sidebar();
    draw_editor();
  }
  draw_output();
  draw_status();
  if (help_open) draw_help();

  if (focus == FOCUS_BROWSE) {
    cr = main_top() + 1 + (br.sel - br.top);
    cc = 0;
  } else {
    cr = main_top() + (buf.cy - buf.top);
    cc = edit_x0() + gutter_w() + (dcol_of(&buf.l[buf.cy], buf.cx) - buf.left);
  }
  if (cr < 0) cr = 0;
  if (cr >= term_rows()) cr = term_rows() - 1;
  if (cc < 0) cc = 0;
  if (cc >= term_cols()) cc = term_cols() - 1;
  term_end(cr, cc, !help_open);
}

/* ------------------------------------------------------------------ */
/* dialogs                                                             */
/* ------------------------------------------------------------------ */
static int prompt(const char *label, char *out, size_t outn)
{
  int len = (int)strlen(out);
  for (;;) {
    int h = term_rows(), w = term_cols();
    int k;
    term_begin();
    draw_header();
    draw_sidebar();
    if (!browser_overlay()) draw_editor();
    draw_output();
    term_move(h - 1, 0);
    term_set(C_BLACK, C_BYELLOW, 1);
    term_printf("%-*.*s", w, w, "");
    term_move(h - 1, 1);
    term_printf("%s%s", label, out);
    term_reset_attr();
    term_end(h - 1, (int)strlen(label) + len + 1, 1);

    k = term_key(-1);
    if (k == K_ESC) return 0;
    if (k == K_ENTER || k == '\n') return 1;
    if (k == K_BACKSPACE || k == 8) {
      if (len > 0) out[--len] = 0;
      continue;
    }
    if (k == CTRL('U')) { len = 0; out[0] = 0; continue; }
    if (k == K_RESIZE) continue;
    if (k >= 32 && k < 127 && (size_t)len + 1 < outn) {
      out[len++] = (char)k;
      out[len] = 0;
    }
  }
}

static int confirm(const char *question)
{
  int h = term_rows(), w = term_cols();
  int k;
  term_begin();
  draw_header();
  draw_sidebar();
  if (!browser_overlay()) draw_editor();
  draw_output();
  term_move(h - 1, 0);
  term_set(C_BWHITE, C_RED, 1);
  term_printf("%-*.*s", w, w, "");
  term_move(h - 1, 1);
  term_printf("%s  (y/n)", question);
  term_reset_attr();
  term_end(h - 1, (int)strlen(question) + 9, 0);

  for (;;) {
    k = term_key(-1);
    if (k == 'y' || k == 'Y') return 1;
    if (k == 'n' || k == 'N' || k == K_ESC) return 0;
  }
}

static void setmsg(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
}

/* ------------------------------------------------------------------ */
/* file actions                                                        */
/* ------------------------------------------------------------------ */
static int do_save_as(void)
{
  char path[MAX_PATH_LEN];
  snprintf(path, sizeof path, "%s", buf.path);
  if (!prompt("Save as: ", path, sizeof path)) { setmsg("cancelled"); return 0; }
  if (!path[0]) return 0;
  if (!buf_save(&buf, path)) { setmsg("cannot write %s", path); return 0; }
  setmsg("saved %s", path);
  br_refresh(&br);
  return 1;
}

static int do_save(void)
{
  if (!buf.path[0]) return do_save_as();
  if (!buf_save(&buf, buf.path)) { setmsg("cannot write %s", buf.path); return 0; }
  setmsg("saved");
  br_refresh(&br);
  return 1;
}

static int maybe_discard(void)
{
  if (!buf.dirty) return 1;
  return confirm("Discard unsaved changes?");
}

static void do_open(const char *path)
{
  if (!maybe_discard()) return;
  if (!buf_load(&buf, path)) { setmsg("cannot read %s", path); return; }
  out_clear();
  setmsg("opened %s", path);
  focus = FOCUS_EDIT;
}

/* ------------------------------------------------------------------ */
/* running                                                             */
/* ------------------------------------------------------------------ */
/* look for the interpreter next to this binary, then on PATH */
static void absolutise(char *p, size_t n)
{
  char full[PATH_MAX];
  if (p[0] == '/') return;
  if (realpath(p, full)) snprintf(p, n, "%s", full);
}

static void find_runner(const char *argv0)
{
  char dir[MAX_PATH_LEN];
  static char cand[MAX_PATH_LEN * 2];
  const char *slash;

  snprintf(dir, sizeof dir, "%s", argv0 ? argv0 : "");
  slash = strrchr(dir, '/');
  if (slash) {
    dir[slash - dir] = 0;
    snprintf(cand, sizeof cand, "%s/shkbasic", dir);
    if (access(cand, X_OK) == 0) {
      snprintf(runner, sizeof runner, "%s", cand);
      absolutise(runner, sizeof runner);
      return;
    }
    snprintf(cand, sizeof cand, "%s/../shkbasic", dir);
    if (access(cand, X_OK) == 0) {
      snprintf(runner, sizeof runner, "%s", cand);
      absolutise(runner, sizeof runner);
      return;
    }
  }
  if (access("./shkbasic", X_OK) == 0) {
    snprintf(runner, sizeof runner, "./shkbasic");
    absolutise(runner, sizeof runner);
    return;
  }
  snprintf(runner, sizeof runner, "shkbasic");
}

/* split a path into the directory to run in and the file name */
static void split_path(const char *path, char *dir, size_t dn, char *file, size_t fn)
{
  const char *slash = strrchr(path, '/');
  if (slash) {
    size_t n = (size_t)(slash - path);
    if (n == 0) n = 1;
    if (n >= dn) n = dn - 1;
    memcpy(dir, path, n);
    dir[n] = 0;
    snprintf(file, fn, "%s", slash + 1);
  } else {
    snprintf(dir, dn, ".");
    snprintf(file, fn, "%s", path);
  }
}

static void run_captured(void)
{
  int pfd[2];
  pid_t pid;
  char dir[MAX_PATH_LEN], file[MAX_PATH_LEN];
  char line[1024];
  int linelen = 0;
  int status = 0;
  int killed = 0;

  if (!buf.path[0]) { if (!do_save_as()) return; }
  else if (buf.dirty && !do_save()) return;

  out_clear();
  out_addf("--- %s ---", buf.path);
  redraw();

  if (pipe(pfd) != 0) { out_add("shkide: cannot create a pipe"); return; }

  split_path(buf.path, dir, sizeof dir, file, sizeof file);

  pid = fork();
  if (pid < 0) { out_add("shkide: cannot fork"); close(pfd[0]); close(pfd[1]); return; }
  if (pid == 0) {
    close(pfd[0]);
    dup2(pfd[1], STDOUT_FILENO);
    dup2(pfd[1], STDERR_FILENO);
    close(pfd[1]);
    /* no terminal for a captured run, so INPUT sees end of file
     * rather than hanging forever */
    {
      int devnull = open("/dev/null", O_RDONLY);
      if (devnull >= 0) { dup2(devnull, STDIN_FILENO); close(devnull); }
    }
    if (chdir(dir) != 0) _exit(127);
    execl(runner, "shkbasic", "-n", file, (char *)NULL);
    execlp("shkbasic", "shkbasic", "-n", file, (char *)NULL);
    _exit(127);
  }

  close(pfd[1]);
  fcntl(pfd[0], F_SETFL, O_NONBLOCK);

  for (;;) {
    fd_set fds;
    struct timeval tv;
    int maxfd = pfd[0] > STDIN_FILENO ? pfd[0] : STDIN_FILENO;
    int r;

    FD_ZERO(&fds);
    FD_SET(pfd[0], &fds);
    FD_SET(STDIN_FILENO, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 50000;

    r = select(maxfd + 1, &fds, NULL, NULL, &tv);
    if (r < 0 && errno != EINTR) break;

    if (r > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
      int k = term_key(0);
      if (k == CTRL('C') || k == K_ESC) {
        kill(pid, SIGTERM);
        killed = 1;
      }
    }

    if (r > 0 && FD_ISSET(pfd[0], &fds)) {
      char chunk[1024];
      ssize_t got = read(pfd[0], chunk, sizeof chunk);
      if (got > 0) {
        ssize_t i;
        for (i = 0; i < got; i++) {
          if (chunk[i] == '\n' || linelen >= (int)sizeof line - 1) {
            line[linelen] = 0;
            out_add(line);
            linelen = 0;
            if (chunk[i] != '\n') linelen = 0;
          } else if (chunk[i] != '\r') {
            line[linelen++] = chunk[i];
          }
        }
        out_top = outn - out_h;
        redraw();
        continue;
      }
      if (got == 0) break;
      if (got < 0 && errno != EAGAIN && errno != EINTR) break;
    }

    {
      pid_t w = waitpid(pid, &status, WNOHANG);
      if (w == pid) {
        /* drain whatever is left in the pipe */
        for (;;) {
          char chunk[1024];
          ssize_t got = read(pfd[0], chunk, sizeof chunk);
          ssize_t i;
          if (got <= 0) break;
          for (i = 0; i < got; i++) {
            if (chunk[i] == '\n' || linelen >= (int)sizeof line - 1) {
              line[linelen] = 0;
              out_add(line);
              linelen = 0;
            } else if (chunk[i] != '\r') line[linelen++] = chunk[i];
          }
        }
        break;
      }
    }
  }
  if (linelen) { line[linelen] = 0; out_add(line); }
  close(pfd[0]);
  waitpid(pid, &status, 0);

  if (killed) out_add("--- stopped ---");
  else if (WIFEXITED(status)) {
    int code = WEXITSTATUS(status);
    if (code == 127) out_addf("shkide: cannot run %s", runner);
    else out_addf("--- exit %d ---", code);
  } else out_add("--- terminated ---");

  if (out_h <= 0) out_h = 8;
  out_top = outn - out_h;
  if (out_top < 0) out_top = 0;
  setmsg(err_line ? "error on line %d (^E)" : "done", err_line);
}

static void run_interactive(void)
{
  pid_t pid;
  int status;
  char dir[MAX_PATH_LEN], file[MAX_PATH_LEN];

  if (!buf.path[0]) { if (!do_save_as()) return; }
  else if (buf.dirty && !do_save()) return;

  split_path(buf.path, dir, sizeof dir, file, sizeof file);

  term_suspend();
  if (write(STDOUT_FILENO, "\033[2J\033[H", 7) < 0) { /* ignore */ }

  pid = fork();
  if (pid == 0) {
    if (chdir(dir) != 0) _exit(127);
    execl(runner, "shkbasic", file, (char *)NULL);
    execlp("shkbasic", "shkbasic", file, (char *)NULL);
    _exit(127);
  }
  if (pid > 0) waitpid(pid, &status, 0);

  {
    const char *tail = "\r\n--- press any key to return to shkide ---";
    if (write(STDOUT_FILENO, tail, strlen(tail)) < 0) { /* ignore */ }
  }
  {
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    {
      char c;
      if (read(STDIN_FILENO, &c, 1) < 0) { /* ignore */ }
    }
  }
  term_resume();
  setmsg("returned from the program");
}

/* ------------------------------------------------------------------ */
/* editor commands                                                     */
/* ------------------------------------------------------------------ */
static void move_up(int n)
{
  int i;
  for (i = 0; i < n; i++) if (buf.cy > 0) buf.cy--;
  if (buf.cx > buf.l[buf.cy].len) buf.cx = buf.l[buf.cy].len;
}

static void move_down(int n)
{
  int i;
  for (i = 0; i < n; i++) if (buf.cy < buf.n - 1) buf.cy++;
  if (buf.cx > buf.l[buf.cy].len) buf.cx = buf.l[buf.cy].len;
}

static void do_find(int again)
{
  int fy, fx;
  if (!again || !find_term[0]) {
    if (!prompt("Find: ", find_term, sizeof find_term)) { setmsg("cancelled"); return; }
    if (!find_term[0]) return;
  }
  if (buf_find(&buf, find_term, buf.cy, buf.cx + 1, &fy, &fx, 1)) {
    buf.cy = fy;
    buf.cx = fx;
    setmsg("found on line %d", fy + 1);
  } else {
    setmsg("not found: %s", find_term);
  }
}

static void do_goto(void)
{
  char g[32] = "";
  int n;
  if (!prompt("Go to line: ", g, sizeof g)) return;
  n = atoi(g);
  if (n < 1) return;
  if (n > buf.n) n = buf.n;
  buf.cy = n - 1;
  buf.cx = 0;
  setmsg("line %d", n);
}

static void do_duplicate(void)
{
  Line *l;
  char *copy;
  buf_push_undo(&buf);
  l = &buf.l[buf.cy];
  copy = strdup(l->s);
  buf.cx = l->len;
  buf_newline(&buf);
  /* buf_newline copies the indent; replace the whole new line */
  {
    Line *nl = &buf.l[buf.cy];
    nl->len = 0;
    nl->s[0] = 0;
    buf.cx = 0;
    buf_insert_str(&buf, copy);
  }
  free(copy);
}

/* indent the current line the way the previous one suggests */
/* Typing ENDIF, NEXT, ELSE and friends pulls the line back one level.
 * Only once per line, so that END -> ENDIF does not dedent twice. */
static int dedented_line = -1;

static void auto_indent_current(void)
{
  int dedent, delta;
  if (buf.cy == 0 || buf.cy == dedented_line) return;
  delta = syn_indent_delta(buf.l[buf.cy].s, buf.l[buf.cy].len, &dedent);
  (void)delta;
  if (dedent && buf.cx >= buf.l[buf.cy].len) {
    buf_unindent(&buf, TABW);
    dedented_line = buf.cy;
  }
}

static void insert_newline_smart(void)
{
  int dedent;
  int delta = syn_indent_delta(buf.l[buf.cy].s, buf.l[buf.cy].len, &dedent);
  buf_newline(&buf);
  if (delta > 0) buf_indent(&buf, TABW);
}

/* ------------------------------------------------------------------ */
/* browser actions                                                     */
/* ------------------------------------------------------------------ */
static void browser_new_file(void)
{
  char name[BR_NAME] = "";
  char path[BR_PATH];
  FILE *f;
  if (!prompt("New file (e.g. game.bas): ", name, sizeof name)) return;
  if (!name[0]) return;
  br_join(br.dir, name, path, sizeof path);
  if (access(path, F_OK) == 0) { setmsg("%s already exists", name); return; }
  f = fopen(path, "wb");
  if (!f) { setmsg("cannot create %s", name); return; }
  fputs("' ", f);
  fputs(name, f);
  fputs("\n\n", f);
  fclose(f);
  br_refresh(&br);
  br_select_name(&br, name);
  do_open(path);
}

static void browser_delete(void)
{
  char path[BR_PATH];
  char q[BR_PATH + 32];
  int isdir = br_selected_path(&br, path, sizeof path);
  if (!path[0]) return;
  if (isdir) { setmsg("directories are not deleted from here"); return; }
  snprintf(q, sizeof q, "Delete %s?", br.e[br.sel].name);
  if (!confirm(q)) return;
  if (remove(path) != 0) { setmsg("cannot delete"); return; }
  setmsg("deleted");
  br_refresh(&br);
}

static void browser_rename(void)
{
  char path[BR_PATH], newpath[BR_PATH];
  char name[BR_NAME];
  if (br.sel < 0 || br.sel >= br.n) return;
  snprintf(name, sizeof name, "%s", br.e[br.sel].name);
  br_join(br.dir, name, path, sizeof path);
  if (!prompt("Rename to: ", name, sizeof name)) return;
  if (!name[0]) return;
  br_join(br.dir, name, newpath, sizeof newpath);
  if (rename(path, newpath) != 0) { setmsg("cannot rename"); return; }
  if (!strcmp(buf.path, path)) snprintf(buf.path, sizeof buf.path, "%s", newpath);
  br_refresh(&br);
  br_select_name(&br, name);
  setmsg("renamed");
}

/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
  const char *openpath = NULL;
  int i;

  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      printf("shkide %s - a terminal IDE for shkbasic\n\n"
             "usage: shkide [file.bas]\n\n"
             "Press F1 once it is open for the key list.\n", IDE_VERSION);
      return 0;
    }
    if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
      printf("shkide %s\n", IDE_VERSION);
      return 0;
    }
    openpath = argv[i];
  }

  find_runner(argv[0]);
  buf_init(&buf);

  if (openpath) {
    char dir[MAX_PATH_LEN], file[MAX_PATH_LEN];
    split_path(openpath, dir, sizeof dir, file, sizeof file);
    br_init(&br, dir);
    if (access(openpath, F_OK) == 0) {
      if (!buf_load(&buf, openpath)) {
        fprintf(stderr, "shkide: cannot read %s\n", openpath);
        return 1;
      }
      br_select_name(&br, file);
    } else {
      snprintf(buf.path, sizeof buf.path, "%s", openpath);
    }
  } else {
    br_init(&br, ".");
  }

  if (!term_init()) {
    fprintf(stderr, "shkide: needs a terminal (stdin is not a tty)\n");
    return 1;
  }

  setmsg("F1 for keys");

  for (;;) {
    int k;

    if (term_resized()) { /* layout recomputes itself on redraw */ }
    redraw();
    k = term_key(-1);
    if (k == K_NONE || k == K_RESIZE) continue;

    if (help_open) { help_open = 0; continue; }

    /* ---- global keys ------------------------------------------- */
    if (k == CTRL('Q')) {
      if (maybe_discard()) break;
      setmsg("still open");
      continue;
    }
    if (k == K_F1) { help_open = 1; continue; }
    if (k == CTRL('S')) { do_save(); continue; }
    if (k == CTRL('A')) { do_save_as(); continue; }
    if (k == K_F5 || k == CTRL('R')) { run_captured(); continue; }
    if (k == K_F6) { run_interactive(); continue; }
    if (k == CTRL('L')) {
      out_h = (out_h == 0) ? 8 : (out_h == 8 ? term_rows() / 2 : 0);
      continue;
    }
    if (k == CTRL('E')) {
      if (err_line) {
        buf.cy = err_line - 1;
        if (buf.cy >= buf.n) buf.cy = buf.n - 1;
        buf.cx = 0;
        focus = FOCUS_EDIT;
        setmsg("line %d", err_line);
      } else setmsg("no error reported");
      continue;
    }
    if (k == CTRL('O')) {
      if (narrow()) {
        show_browser = 1;
        focus = (focus == FOCUS_BROWSE) ? FOCUS_EDIT : FOCUS_BROWSE;
      } else if (!show_browser) {
        show_browser = 1;
        focus = FOCUS_BROWSE;
      } else {
        focus = (focus == FOCUS_BROWSE) ? FOCUS_EDIT : FOCUS_BROWSE;
      }
      continue;
    }
    if (k == CTRL('N')) {
      if (!maybe_discard()) continue;
      buf_new(&buf);
      out_clear();
      setmsg("new file");
      continue;
    }

    /* ---- file pane --------------------------------------------- */
    if (focus == FOCUS_BROWSE) {
      switch (k) {
        case K_UP:    if (br.sel > 0) br.sel--; break;
        case K_DOWN:  if (br.sel < br.n - 1) br.sel++; break;
        case K_PGUP:  br.sel -= edit_rows() - 2; if (br.sel < 0) br.sel = 0; break;
        case K_PGDN:  br.sel += edit_rows() - 2; if (br.sel >= br.n) br.sel = br.n - 1; break;
        case K_HOME:  br.sel = 0; break;
        case K_END:   br.sel = br.n ? br.n - 1 : 0; break;
        case K_ENTER: case '\n': {
          char file[BR_PATH];
          file[0] = 0;
          if (!br_enter(&br, file, sizeof file) && file[0]) do_open(file);
          break;
        }
        case K_BACKSPACE: case K_LEFT: {
          char up[BR_PATH];
          char leaving[BR_NAME];
          const char *slash = strrchr(br.dir, '/');
          snprintf(leaving, sizeof leaving, "%s", slash && slash[1] ? slash + 1 : "");
          br_join(br.dir, "..", up, sizeof up);
          if (br_load(&br, up) && leaving[0]) br_select_name(&br, leaving);
          break;
        }
        case K_RIGHT: case K_TAB: focus = FOCUS_EDIT; break;
        case 'n': case 'N': browser_new_file(); break;
        case 'd': case 'D': browser_delete(); break;
        case 'r': case 'R': browser_rename(); break;
        case 'a': case 'A': br.show_all = !br.show_all; br_refresh(&br); break;
        case 'g': case 'G': br_refresh(&br); setmsg("refreshed"); break;
        case K_ESC: focus = FOCUS_EDIT; break;
        default: break;
      }
      continue;
    }

    /* ---- editor -------------------------------------------------- */
    switch (k) {
      case K_UP:    move_up(1); break;
      case K_DOWN:  move_down(1); break;
      case K_LEFT:
        if (buf.cx > 0) buf.cx--;
        else if (buf.cy > 0) { buf.cy--; buf.cx = buf.l[buf.cy].len; }
        break;
      case K_RIGHT:
        if (buf.cx < buf.l[buf.cy].len) buf.cx++;
        else if (buf.cy < buf.n - 1) { buf.cy++; buf.cx = 0; }
        break;
      case K_HOME: {
        int ind = 0;
        Line *l = &buf.l[buf.cy];
        while (ind < l->len && (l->s[ind] == ' ' || l->s[ind] == '\t')) ind++;
        buf.cx = (buf.cx == ind) ? 0 : ind;
        break;
      }
      case K_END:   buf.cx = buf.l[buf.cy].len; break;
      case K_PGUP:  move_up(edit_rows() - 1); break;
      case K_PGDN:  move_down(edit_rows() - 1); break;
      case K_CUP:   move_up(edit_rows() - 1); break;
      case K_CDOWN: move_down(edit_rows() - 1); break;
      case K_CLEFT: {
        Line *l = &buf.l[buf.cy];
        while (buf.cx > 0 && !isalnum((unsigned char)l->s[buf.cx-1])) buf.cx--;
        while (buf.cx > 0 && isalnum((unsigned char)l->s[buf.cx-1])) buf.cx--;
        break;
      }
      case K_CRIGHT: {
        Line *l = &buf.l[buf.cy];
        while (buf.cx < l->len && isalnum((unsigned char)l->s[buf.cx])) buf.cx++;
        while (buf.cx < l->len && !isalnum((unsigned char)l->s[buf.cx])) buf.cx++;
        break;
      }

      case K_ENTER: case '\n':
        buf_push_undo(&buf);
        insert_newline_smart();
        break;
      case K_BACKSPACE:
        buf_push_undo(&buf);
        buf_backspace(&buf);
        break;
      case K_DELETE:
        buf_push_undo(&buf);
        buf_delete(&buf);
        break;
      case K_TAB:
        buf_push_undo(&buf);
        buf_indent(&buf, TABW);
        break;
      case CTRL('K'):
        buf_push_undo(&buf);
        buf_delete_line(&buf);
        break;
      case CTRL('D'):
        do_duplicate();
        break;
      case CTRL('Z'):
        if (!buf_undo(&buf)) setmsg("nothing to undo");
        break;
      case CTRL('Y'):
        if (!buf_redo(&buf)) setmsg("nothing to redo");
        break;
      case CTRL('F'):
        do_find(0);
        break;
      case K_F3:
        do_find(1);
        break;
      case CTRL('G'):
        do_goto();
        break;
      case CTRL('T'):
        buf_push_undo(&buf);
        buf_unindent(&buf, TABW);
        break;
      case K_ESC:
        break;
      default:
        if (k >= 32 && k < 127) {
          buf_push_undo(&buf);
          buf_insert_char(&buf, k);
          /* typing ENDIF / NEXT / ELSE pulls the line back a level */
          if (isalpha(k)) auto_indent_current();
        }
        break;
    }
    buf_clamp(&buf);
  }

  term_shutdown();
  buf_free(&buf);
  br_free(&br);
  out_clear();
  free(outl);
  return 0;
}
