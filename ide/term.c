/* term.c - raw terminal, key decoding and flicker-free drawing */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include "term.h"

static struct termios saved;
static int have_saved = 0;
static int rows = 24, cols = 80;
static volatile sig_atomic_t resized = 0;

/* output accumulator: one write(2) per frame keeps the screen steady
 * even over a slow SSH link */
static char  *obuf = NULL;
static size_t olen = 0, ocap = 0;

static void obuf_add(const char *s, size_t n)
{
  if (olen + n + 1 > ocap) {
    while (olen + n + 1 > ocap) ocap = ocap ? ocap * 2 : 8192;
    obuf = realloc(obuf, ocap);
    if (!obuf) { perror("realloc"); exit(1); }
  }
  memcpy(obuf + olen, s, n);
  olen += n;
}

static void obuf_str(const char *s) { obuf_add(s, strlen(s)); }

static void on_winch(int sig) { (void)sig; resized = 1; }

static void query_size(void)
{
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
    rows = ws.ws_row;
    cols = ws.ws_col;
  }
  if (rows < 8) rows = 8;
  if (cols < 20) cols = 20;
  if (rows > 200) rows = 200;
  if (cols > 400) cols = 400;
}

int term_init(void)
{
  struct termios t;
  struct sigaction sa;

  if (!isatty(STDIN_FILENO)) return 0;
  if (tcgetattr(STDIN_FILENO, &saved) != 0) return 0;
  have_saved = 1;

  t = saved;
  t.c_iflag &= (tcflag_t)~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  t.c_oflag &= (tcflag_t)~(OPOST);
  t.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) != 0) return 0;

  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_winch;
  sigaction(SIGWINCH, &sa, NULL);

  query_size();
  /* alternate screen, hide cursor */
  if (write(STDOUT_FILENO, "\033[?1049h\033[?25l", 14) < 0) { /* ignore */ }
  return 1;
}

void term_shutdown(void)
{
  if (write(STDOUT_FILENO, "\033[0m\033[?25h\033[?1049l", 18) < 0) { /* ignore */ }
  if (have_saved) tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
  free(obuf);
  obuf = NULL;
  ocap = olen = 0;
}

void term_suspend(void)
{
  if (write(STDOUT_FILENO, "\033[0m\033[?25h\033[?1049l", 18) < 0) { /* ignore */ }
  if (have_saved) tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
}

void term_resume(void)
{
  struct termios t;
  if (have_saved) {
    t = saved;
    t.c_iflag &= (tcflag_t)~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    t.c_oflag &= (tcflag_t)~(OPOST);
    t.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
  }
  if (write(STDOUT_FILENO, "\033[?1049h\033[?25l", 14) < 0) { /* ignore */ }
  query_size();
  resized = 1;
}

int term_rows(void) { return rows; }
int term_cols(void) { return cols; }

int term_resized(void)
{
  if (!resized) return 0;
  resized = 0;
  query_size();
  return 1;
}

/* ------------------------------------------------------------------ */
/* key decoding                                                        */
/* ------------------------------------------------------------------ */
static int read_byte(int timeout_ms)
{
  unsigned char c;
  fd_set fds;
  struct timeval tv, *ptv = NULL;
  int r;

  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  if (timeout_ms >= 0) {
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ptv = &tv;
  }
  r = select(STDIN_FILENO + 1, &fds, NULL, NULL, ptv);
  if (r < 0) return (errno == EINTR) ? -2 : -1;
  if (r == 0) return -1;
  r = (int)read(STDIN_FILENO, &c, 1);
  if (r != 1) return -1;
  return c;
}

/* decode the tail of a CSI sequence */
static int decode_csi(void)
{
  char seq[16];
  int n = 0;
  int c;

  for (;;) {
    c = read_byte(60);
    if (c < 0) break;
    if (n < (int)sizeof seq - 1) seq[n++] = (char)c;
    if ((c >= 'A' && c <= 'Z') || c == '~' || (c >= 'a' && c <= 'z')) break;
  }
  seq[n] = 0;
  if (!n) return K_ESC;

  /* ctrl+arrow arrives as 1;5A .. 1;5D */
  if (!strcmp(seq, "1;5A")) return K_CUP;
  if (!strcmp(seq, "1;5B")) return K_CDOWN;
  if (!strcmp(seq, "1;5C")) return K_CRIGHT;
  if (!strcmp(seq, "1;5D")) return K_CLEFT;

  switch (seq[n-1]) {
    case 'A': return K_UP;
    case 'B': return K_DOWN;
    case 'C': return K_RIGHT;
    case 'D': return K_LEFT;
    case 'H': return K_HOME;
    case 'F': return K_END;
    case 'P': return K_F1;    /* some terminals send CSI P for F1 */
    case 'Q': return K_F2;
    case 'R': return K_F3;
    case 'S': return K_F4;
    case '~': {
      int v = atoi(seq);
      switch (v) {
        case 1: case 7: return K_HOME;
        case 2:  return K_INSERT;
        case 3:  return K_DELETE;
        case 4: case 8: return K_END;
        case 5:  return K_PGUP;
        case 6:  return K_PGDN;
        case 11: return K_F1;
        case 12: return K_F2;
        case 13: return K_F3;
        case 14: return K_F4;
        case 15: return K_F5;
        case 17: return K_F6;
        case 18: return K_F7;
        case 19: return K_F8;
        case 20: return K_F9;
        case 21: return K_F10;
      }
      return K_NONE;
    }
  }
  return K_NONE;
}

int term_key(int timeout_ms)
{
  int c = read_byte(timeout_ms);

  if (c == -2) return resized ? K_RESIZE : K_NONE;
  if (c < 0) return resized ? K_RESIZE : K_NONE;

  if (c != K_ESC) {
    if (c == 8) return K_BACKSPACE;     /* some keyboards send ^H */
    return c;
  }

  /* ESC alone, or the start of a sequence */
  c = read_byte(40);
  if (c < 0) return K_ESC;
  if (c == '[') return decode_csi();
  if (c == 'O') {
    int k = read_byte(40);
    switch (k) {
      case 'A': return K_UP;
      case 'B': return K_DOWN;
      case 'C': return K_RIGHT;
      case 'D': return K_LEFT;
      case 'H': return K_HOME;
      case 'F': return K_END;
      case 'P': return K_F1;
      case 'Q': return K_F2;
      case 'R': return K_F3;
      case 'S': return K_F4;
    }
    return K_NONE;
  }
  return K_ESC;
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */
void term_begin(void)
{
  olen = 0;
  obuf_str("\033[?25l\033[H");
}

void term_end(int cursor_row, int cursor_col, int show_cursor)
{
  char b[48];
  snprintf(b, sizeof b, "\033[%d;%dH", cursor_row + 1, cursor_col + 1);
  obuf_str(b);
  if (show_cursor) obuf_str("\033[?25h");
  if (olen && write(STDOUT_FILENO, obuf, olen) < 0) { /* ignore */ }
  olen = 0;
}

void term_set(int fg, int bg, int bold)
{
  char b[48];
  if (fg == C_DEFAULT && bg == C_DEFAULT && !bold) { obuf_str("\033[0m"); return; }
  snprintf(b, sizeof b, "\033[%s%d;%dm", bold ? "1;" : "",
           fg ? fg : 39, bg ? bg + 10 : 49);
  obuf_str(b);
}

void term_reset_attr(void) { obuf_str("\033[0m"); }

void term_move(int row, int col)
{
  char b[32];
  snprintf(b, sizeof b, "\033[%d;%dH", row + 1, col + 1);
  obuf_str(b);
}

void term_put(const char *s) { if (s) obuf_str(s); }
void term_putn(const char *s, int n) { if (s && n > 0) obuf_add(s, (size_t)n); }

void term_putc(int c)
{
  char b = (char)c;
  obuf_add(&b, 1);
}

void term_fill(int row, int col, int n, int c)
{
  int i;
  if (n <= 0) return;
  term_move(row, col);
  for (i = 0; i < n; i++) term_putc(c);
}

void term_clear_line(int row)
{
  term_move(row, 0);
  obuf_str("\033[K");
}

void term_printf(const char *fmt, ...)
{
  char b[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  obuf_str(b);
}
