/* browser.c - directory listing for the file pane */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#include "browser.h"

static int has_ext(const char *name, const char *ext)
{
  size_t n = strlen(name), e = strlen(ext);
  return n > e && strcasecmp(name + n - e, ext) == 0;
}

static int cmp_ent(const void *pa, const void *pb)
{
  const FEnt *a = pa, *b = pb;
  if (a->isdir != b->isdir) return b->isdir - a->isdir;   /* dirs first */
  return strcasecmp(a->name, b->name);
}

void br_init(Browser *b, const char *dir)
{
  memset(b, 0, sizeof *b);
  br_load(b, dir);
}

void br_free(Browser *b)
{
  free(b->e);
  memset(b, 0, sizeof *b);
}

void br_join(const char *dir, const char *name, char *out, size_t outn)
{
  if (!strcmp(dir, "/")) snprintf(out, outn, "/%s", name);
  else snprintf(out, outn, "%s/%s", dir, name);
}

static void push(Browser *b, const char *name, int isdir, long size)
{
  if (b->n == b->cap) {
    b->cap = b->cap ? b->cap * 2 : 64;
    b->e = realloc(b->e, (size_t)b->cap * sizeof(FEnt));
    if (!b->e) { fputs("shkide: out of memory\n", stderr); exit(1); }
  }
  snprintf(b->e[b->n].name, BR_NAME, "%s", name);
  b->e[b->n].isdir = isdir;
  b->e[b->n].size = size;
  b->n++;
}

int br_load(Browser *b, const char *dir)
{
  DIR *d;
  struct dirent *de;
  char resolved[BR_PATH];
  int first;

  /* realpath needs a PATH_MAX buffer, which is larger than BR_PATH */
  {
    char full[PATH_MAX];
    if (realpath(dir, full)) snprintf(resolved, sizeof resolved, "%s", full);
    else snprintf(resolved, sizeof resolved, "%s", dir);
  }
  d = opendir(resolved);
  if (!d) return 0;

  snprintf(b->dir, sizeof b->dir, "%s", resolved);
  b->n = 0;
  first = b->n;

  while ((de = readdir(d))) {
    char full[BR_PATH + BR_NAME + 2];
    struct stat st;
    int isdir;

    if (!strcmp(de->d_name, ".")) continue;
    if (de->d_name[0] == '.' && strcmp(de->d_name, "..") && !b->show_all) continue;

    br_join(resolved, de->d_name, full, sizeof full);
    if (stat(full, &st) != 0) continue;
    isdir = S_ISDIR(st.st_mode);

    if (!isdir && !b->show_all &&
        !has_ext(de->d_name, ".bas") && !has_ext(de->d_name, ".inc"))
      continue;

    push(b, de->d_name, isdir, isdir ? 0 : (long)st.st_size);
  }
  closedir(d);

  qsort(b->e + first, (size_t)(b->n - first), sizeof(FEnt), cmp_ent);
  if (b->sel >= b->n) b->sel = b->n ? b->n - 1 : 0;
  if (b->sel < 0) b->sel = 0;
  b->top = 0;
  return 1;
}

void br_refresh(Browser *b)
{
  char keep[BR_NAME];
  int sel = b->sel;
  keep[0] = 0;
  if (b->sel >= 0 && b->sel < b->n) snprintf(keep, sizeof keep, "%s", b->e[b->sel].name);
  br_load(b, b->dir);
  if (keep[0]) {
    br_select_name(b, keep);
  } else if (sel < b->n) {
    b->sel = sel;
  }
}

void br_select_name(Browser *b, const char *name)
{
  int i;
  for (i = 0; i < b->n; i++)
    if (!strcmp(b->e[i].name, name)) { b->sel = i; return; }
  if (b->sel >= b->n) b->sel = b->n ? b->n - 1 : 0;
}

int br_selected_path(Browser *b, char *out, size_t outn)
{
  if (b->sel < 0 || b->sel >= b->n) { if (outn) out[0] = 0; return 0; }
  br_join(b->dir, b->e[b->sel].name, out, outn);
  return b->e[b->sel].isdir;
}

int br_enter(Browser *b, char *file_out, size_t outn)
{
  char path[BR_PATH];
  if (b->sel < 0 || b->sel >= b->n) return 0;
  br_join(b->dir, b->e[b->sel].name, path, sizeof path);
  if (b->e[b->sel].isdir) {
    char leaving[BR_NAME];
    int was_up = !strcmp(b->e[b->sel].name, "..");
    const char *slash = strrchr(b->dir, '/');
    snprintf(leaving, sizeof leaving, "%s", slash && slash[1] ? slash + 1 : "");
    if (!br_load(b, path)) return 0;
    if (was_up && leaving[0]) br_select_name(b, leaving);
    return 1;
  }
  snprintf(file_out, outn, "%s", path);
  return 0;
}
