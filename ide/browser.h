/* browser.h - the directory pane */
#ifndef BROWSER_H
#define BROWSER_H

#define BR_NAME 256
#define BR_PATH 4096

typedef struct {
  char name[BR_NAME];
  int  isdir;
  long size;
} FEnt;

typedef struct {
  char  dir[BR_PATH];
  FEnt *e;
  int   n, cap;
  int   sel, top;
  int   show_all;      /* off: directories and *.bas only */
} Browser;

void br_init(Browser *b, const char *dir);
void br_free(Browser *b);
int  br_load(Browser *b, const char *dir);
void br_refresh(Browser *b);
/* joins the selection onto the current directory; returns 1 if a dir */
int  br_selected_path(Browser *b, char *out, size_t outn);
int  br_enter(Browser *b, char *file_out, size_t outn);
void br_select_name(Browser *b, const char *name);
void br_join(const char *dir, const char *name, char *out, size_t outn);

#endif
