/* main.c - command line driver for shkbasic */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include "shk.h"

#define SHK_VERSION "1.0"

int mkdir_compat(const char *p) { return mkdir(p, 0777); }

static char *read_file(const char *path)
{
  FILE *f = fopen(path, "rb");
  long n;
  char *buf;
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n < 0) { fclose(f); return NULL; }
  buf = malloc((size_t)n + 2);
  if (!buf) { fclose(f); return NULL; }
  if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
  buf[n] = '\n';
  buf[n+1] = 0;
  fclose(f);
  return buf;
}

static void usage(void)
{
  fputs(
    "shkbasic " SHK_VERSION " - MMBasic for the SharkDeck\n"
    "\n"
    "usage: shkbasic [options] program.bas\n"
    "       shkbasic [options] -e 'PRINT 2+2'\n"
    "\n"
    "  -e STMT      run one line of BASIC instead of a file\n"
    "  -g           open the framebuffer at startup\n"
    "  -n           never touch the framebuffer (draw to memory only)\n"
    "  -o FILE.bmp  write the graphics screen to a BMP when the program ends\n"
    "  -s SEED      seed the random number generator (repeatable runs)\n"
    "  -v           print the version and exit\n"
    "  -h           print this help\n"
    "\n"
    "Graphics work with or without /dev/fb0: without it the canvas lives in\n"
    "memory, which is what makes -o useful when you are on SSH.\n",
    stderr);
}

int main(int argc, char **argv)
{
  const char * volatile path = NULL;
  const char * volatile inline_src = NULL;
  const char * volatile bmp_out = NULL;
  volatile int want_fb = 0;
  int no_fb = 0;
  int seeded = 0;
  char * volatile src = NULL;
  int i;
  volatile int rc = 0;

  for (i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
    if (!strcmp(a, "-v") || !strcmp(a, "--version")) { printf("shkbasic %s\n", SHK_VERSION); return 0; }
    if (!strcmp(a, "-g")) { want_fb = 1; continue; }
    if (!strcmp(a, "-n")) { no_fb = 1; continue; }
    if (!strcmp(a, "-e")) { if (++i >= argc) { usage(); return 2; } inline_src = argv[i]; continue; }
    if (!strcmp(a, "-o")) { if (++i >= argc) { usage(); return 2; } bmp_out = argv[i]; continue; }
    if (!strcmp(a, "-s")) { if (++i >= argc) { usage(); return 2; } srand((unsigned)atoi(argv[i])); seeded = 1; continue; }
    if (a[0] == '-' && a[1]) { fprintf(stderr, "shkbasic: unknown option %s\n", a); return 2; }
    path = a;
    /* everything after the program name is handed to the program */
    if (i + 1 < argc) {
      size_t n = 0;
      int k;
      for (k = i + 1; k < argc; k++) n += strlen(argv[k]) + 1;
      shk_cmdline = malloc(n + 1);
      shk_cmdline[0] = 0;
      for (k = i + 1; k < argc; k++) {
        if (k > i + 1) strcat(shk_cmdline, " ");
        strcat(shk_cmdline, argv[k]);
      }
    }
    break;
  }

  if (!path && !inline_src) { usage(); return 2; }
  if (!seeded) srand((unsigned)time(NULL));
  if (no_fb) setenv("SHKBASIC_NOFB", "1", 1);

  memset(&vm, 0, sizeof vm);
  vm.opt_base = 0;
  vm.opt_default = V_FLOAT;
  var_init();
  con_init();

  if (setjmp(vm.err_jmp)) {
    fprintf(stderr, "shkbasic: %s\n", vm.err_msg);
    con_shutdown();
    return 1;
  }

  if (inline_src) {
    size_t n = strlen(inline_src);
    src = malloc(n + 2);
    memcpy(src, inline_src, n);
    src[n] = '\n';
    src[n+1] = 0;
  } else {
    src = read_file(path);
    if (!src) { fprintf(stderr, "shkbasic: cannot read %s\n", path); return 1; }
  }

  prog_load(src);

  if (want_fb) gfx_init(1);

  exec_run();

  if (vm.err_code) {
    con_raw(0);
    fflush(stdout);
    if (vm.err_line > 0)
      fprintf(stderr, "shkbasic: error on line %d: %s\n", vm.err_line, vm.err_msg);
    else
      fprintf(stderr, "shkbasic: error: %s\n", vm.err_msg);
    rc = 1;
  }

  if (bmp_out) {
    if (!gfx_available()) gfx_init(0);
    if (!gfx_save_bmp(bmp_out))
      fprintf(stderr, "shkbasic: cannot write %s\n", bmp_out);
  }

  for (i = 1; i < MAXFILE; i++) if (vm.fh[i]) fclose(vm.fh[i]);
  gfx_shutdown();
  con_shutdown();
  var_clear_all();
  prog_free();
  free(src);
  return rc;
}
