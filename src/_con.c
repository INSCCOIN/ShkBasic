/* con.c - console input and output, raw-mode keys, millisecond timer */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/select.h>
#include <termios.h>
#include "shk.h"

static struct termios saved_tio;
static int tio_saved = 0;
static int raw_on = 0;
static int col = 0;
static double timer_base = 0;

static double now_ms(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

void con_init(void)
{
  if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved_tio) == 0)
    tio_saved = 1;
  timer_base = now_ms();
  col = 0;
}

void con_raw(int on)
{
  struct termios t;
  if (!tio_saved) { raw_on = on; return; }
  if (on && !raw_on) {
    t = saved_tio;
    t.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    raw_on = 1;
  } else if (!on && raw_on) {
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_tio);
    raw_on = 0;
  }
}

void con_shutdown(void)
{
  con_raw(0);
  fflush(stdout);
}

/* non-blocking single key, 0 when nothing is waiting */
int con_key(void)
{
  unsigned char c;
  int was_raw = raw_on;
  int r;
  fd_set fds;
  struct timeval tv;

  if (!was_raw) con_raw(1);
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  r = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
  if (r <= 0) { if (!was_raw) con_raw(0); return 0; }
  r = (int)read(STDIN_FILENO, &c, 1);
  if (!was_raw) con_raw(0);
  if (r != 1) return 0;
  return c;
}

int con_getkey_block(void)
{
  int c;
  for (;;) {
    c = con_key();
    if (c) return c;
    usleep(2000);
  }
}

char *con_readline(const char *prompt)
{
  size_t cap = 128, n = 0;
  char *buf;
  int was_raw = raw_on;
  int c;

  if (was_raw) con_raw(0);
  if (prompt && *prompt) { fputs(prompt, stdout); fflush(stdout); col = 0; }

  buf = shk_alloc(cap);
  for (;;) {
    c = fgetc(stdin);
    if (c == EOF) {
      if (n == 0) { free(buf); if (was_raw) con_raw(1); return NULL; }
      break;
    }
    if (c == '\n') break;
    if (c == '\r') continue;
    if (n + 1 >= cap) { cap *= 2; buf = shk_realloc(buf, cap); }
    buf[n++] = (char)c;
  }
  buf[n] = 0;
  col = 0;
  if (was_raw) con_raw(1);
  return buf;
}

void con_outn(const char *s, int n)
{
  int i;
  for (i = 0; i < n; i++) {
    fputc(s[i], stdout);
    if (s[i] == '\n') col = 0;
    else if (s[i] == '\r') col = 0;
    else if (s[i] == '\t') col = (col + 8) & ~7;
    else col++;
  }
}

void con_out(const char *s) { if (s) con_outn(s, (int)strlen(s)); }

void con_putc(int c) { char b = (char)c; con_outn(&b, 1); }

int con_col(void) { return col; }
void con_set_col(int c) { col = c; }
void con_flush(void) { fflush(stdout); }

double con_timer_ms(void) { return now_ms() - timer_base; }
void   con_timer_reset(void) { timer_base = now_ms(); }

void con_pause_ms(double ms)
{
  double end;
  if (ms <= 0) return;
  fflush(stdout);
  gfx_present();
  end = now_ms() + ms;
  for (;;) {
    double left = end - now_ms();
    if (left <= 0) break;
    if (left > 20) usleep(20000);
    else usleep((useconds_t)(left * 1000));
  }
}
