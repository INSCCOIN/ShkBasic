/* term.h - raw terminal, key decoding and flicker-free drawing
 *
 * No ncurses: the SharkDeck's stock image may not have it, and the
 * escape sequences we need are the ones every Linux console and PuTTY
 * already understand.
 */
#ifndef TERM_H
#define TERM_H

#include <stddef.h>

/* keys above the ASCII range */
enum {
  K_NONE = 0,
  K_ESC  = 27,
  K_ENTER = 13,
  K_TAB = 9,
  K_BACKSPACE = 127,

  K_UP = 0x110, K_DOWN, K_LEFT, K_RIGHT,
  K_HOME, K_END, K_PGUP, K_PGDN, K_DELETE, K_INSERT,
  K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10,
  K_CUP, K_CDOWN, K_CLEFT, K_CRIGHT,    /* ctrl + arrow */
  K_RESIZE
};

#ifdef CTRL
#undef CTRL
#endif
#define CTRL(c) ((c) & 0x1f)

/* colours (ANSI 16, which the framebuffer console renders faithfully) */
enum {
  C_DEFAULT = 0,
  C_BLACK = 30, C_RED, C_GREEN, C_YELLOW, C_BLUE, C_MAGENTA, C_CYAN, C_WHITE,
  C_BBLACK = 90, C_BRED, C_BGREEN, C_BYELLOW, C_BBLUE, C_BMAGENTA, C_BCYAN, C_BWHITE
};

int  term_init(void);
void term_shutdown(void);
void term_suspend(void);      /* give the terminal back to a child  */
void term_resume(void);
int  term_rows(void);
int  term_cols(void);
int  term_resized(void);      /* consumes the flag                   */

int  term_key(int timeout_ms);  /* -1 waits forever                  */

/* frame construction: begin, draw, end (one write per frame) */
void term_begin(void);
void term_end(int cursor_row, int cursor_col, int show_cursor);
void term_set(int fg, int bg, int bold);
void term_reset_attr(void);
void term_move(int row, int col);
void term_put(const char *s);
void term_putn(const char *s, int n);
void term_putc(int c);
void term_fill(int row, int col, int n, int c);
void term_clear_line(int row);
void term_printf(const char *fmt, ...);

#endif
