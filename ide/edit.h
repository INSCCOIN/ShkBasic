/* edit.h - the text buffer behind the editor pane */
#ifndef EDIT_H
#define EDIT_H

#define MAX_PATH_LEN 4096
#define UNDO_MAX 64

typedef struct {
  char *s;
  int   len, cap;
} Line;

typedef struct {
  char *text;      /* whole-buffer snapshot */
  int   cy, cx;
} Snap;

typedef struct {
  Line *l;
  int   n, cap;

  char  path[MAX_PATH_LEN];
  int   dirty;
  int   no_trailing_nl;

  int   cy, cx;          /* cursor line and column                */
  int   top, left;       /* first visible line and column         */
  int   goalx;           /* remembered column for vertical moves  */

  Snap  undo[UNDO_MAX];
  int   undo_n;
  Snap  redo[UNDO_MAX];
  int   redo_n;
} Buf;

void  buf_init(Buf *b);
void  buf_free(Buf *b);
int   buf_load(Buf *b, const char *path);
int   buf_save(Buf *b, const char *path);
void  buf_new(Buf *b);

char *buf_text(Buf *b, int *len_out);       /* caller frees           */
void  buf_set_text(Buf *b, const char *t);

void  buf_push_undo(Buf *b);
int   buf_undo(Buf *b);
int   buf_redo(Buf *b);

void  buf_insert_char(Buf *b, int c);
void  buf_insert_str(Buf *b, const char *s);
void  buf_newline(Buf *b);
void  buf_backspace(Buf *b);
void  buf_delete(Buf *b);
void  buf_delete_line(Buf *b);
void  buf_indent(Buf *b, int width);
void  buf_unindent(Buf *b, int width);

void  buf_clamp(Buf *b);
int   buf_find(Buf *b, const char *needle, int from_y, int from_x,
               int *fy, int *fx, int wrap);

#endif
