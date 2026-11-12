/* eval.c - expression evaluation and the builtin function library
 *
 * Precedence, lowest binding first (MMBasic order):
 *   OR XOR | AND | NOT | = <> < <= > >= | << >> | + - | * / \ MOD | unary | ^
 */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <fnmatch.h>
#include <unistd.h>
#include "shk.h"

static void parse_or(Value *out);

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */
static double to_rad(double a) { return vm.opt_degrees ? a * M_PI / 180.0 : a; }
static double from_rad(double a) { return vm.opt_degrees ? a * 180.0 / M_PI : a; }

static int both_int(const Value *a, const Value *b)
{
  return a->type == V_INT && b->type == V_INT;
}

static void need_num(const Value *v, const char *op)
{
  if (v->type == V_STR) shk_error("Operator %s needs numbers", op);
}

/* string comparison returning <0, 0, >0 */
static int str_cmp(const Value *a, const Value *b)
{
  return strcmp(a->s ? a->s : "", b->s ? b->s : "");
}

/* ------------------------------------------------------------------ */
/* argument collection                                                 */
/* ------------------------------------------------------------------ */
#define MAXARG 12

typedef struct {
  int    n;
  Value  v[MAXARG];
  int    present[MAXARG];
} Args;

static void args_free(Args *a)
{
  int i;
  for (i = 0; i < a->n; i++) val_free(&a->v[i]);
  a->n = 0;
}

/* Parse "( a, b, ... )".  Empty slots (a,,c) are marked absent. */
static void parse_args(Args *a, int minn, int maxn, const char *who)
{
  int i;
  memset(a, 0, sizeof *a);
  for (i = 0; i < MAXARG; i++) val_init(&a->v[i]);

  if (!accept_op('(')) {
    if (minn > 0) shk_error("%s expects arguments", who);
    return;
  }
  if (accept_op(')')) {
    if (minn > 0) shk_error("%s expects %d argument(s)", who, minn);
    return;
  }
  for (;;) {
    if (a->n >= MAXARG) shk_error("Too many arguments to %s", who);
    if (at_op(',') || at_op(')')) {
      a->present[a->n] = 0;
      a->n++;
    } else {
      parse_or(&a->v[a->n]);
      a->present[a->n] = 1;
      a->n++;
    }
    if (accept_op(',')) continue;
    break;
  }
  expect_op(')');
  if (a->n < minn || a->n > maxn)
    shk_error("%s expects %d to %d argument(s), got %d", who, minn, maxn, a->n);
}

static double arg_f(Args *a, int i)
{
  if (i >= a->n || !a->present[i]) shk_error("Missing argument");
  return val_f(&a->v[i]);
}

static int64_t arg_i(Args *a, int i)
{
  if (i >= a->n || !a->present[i]) shk_error("Missing argument");
  return val_i(&a->v[i]);
}

static const char *arg_s(Args *a, int i)
{
  if (i >= a->n || !a->present[i]) shk_error("Missing argument");
  return val_s(&a->v[i]);
}

static int has(Args *a, int i) { return i < a->n && a->present[i]; }

/* ------------------------------------------------------------------ */
/* numeric to string, MMBasic style                                    */
/* ------------------------------------------------------------------ */
void num_to_str(const Value *v, char *buf, size_t n)
{
  if (v->type == V_INT) {
    snprintf(buf, n, "%lld", (long long)v->i);
    return;
  }
  {
    double d = v->f;
    if (d == (double)(int64_t)d && fabs(d) < 1e15) {
      snprintf(buf, n, "%lld", (long long)d);
    } else {
      char tmp[64];
      char *p;
      snprintf(tmp, sizeof tmp, "%.10g", d);
      /* trim a trailing ".0" style artefact left by %g on some libcs */
      p = strchr(tmp, 'e');
      if (!p) {
        p = tmp + strlen(tmp);
        if (p > tmp && strchr(tmp, '.')) {
          while (p > tmp && p[-1] == '0') *--p = 0;
          if (p > tmp && p[-1] == '.') *--p = 0;
        }
      }
      snprintf(buf, n, "%s", tmp);
    }
  }
}

/* ------------------------------------------------------------------ */
/* FORMAT$ - validate the format string before handing it to printf    */
/* ------------------------------------------------------------------ */
static void do_format(const char *fmt, double val, char *out, size_t outn)
{
  char spec[64];
  const char *p = fmt;
  int n = 0;
  int conv = 0;
  size_t used = 0;

  out[0] = 0;
  while (*p) {
    if (*p != '%') {
      if (used + 1 < outn) out[used++] = *p;
      p++;
      continue;
    }
    if (p[1] == '%') {
      if (used + 1 < outn) out[used++] = '%';
      p += 2;
      continue;
    }
    if (conv) shk_error("FORMAT$ accepts only one conversion");
    spec[n++] = *p++;
    while (*p && strchr("-+ #0", *p) && n < 40) spec[n++] = *p++;
    while (isdigit((unsigned char)*p) && n < 50) spec[n++] = *p++;
    if (*p == '.') { spec[n++] = *p++;
      while (isdigit((unsigned char)*p) && n < 58) spec[n++] = *p++; }
    if (!*p) shk_error("Bad format string");
    {
      char c = *p++;
      char tail[72];
      switch (c) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o':
          spec[n++] = 'l'; spec[n++] = 'l'; spec[n++] = c; spec[n] = 0;
          snprintf(tail, sizeof tail, spec, (long long)llround(val));
          break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
          spec[n++] = c; spec[n] = 0;
          snprintf(tail, sizeof tail, spec, val);
          break;
        default:
          shk_error("Unsupported conversion %%%c in FORMAT$", c);
          return;
      }
      conv = 1;
      snprintf(out + used, outn - used, "%s", tail);
      used += strlen(out + used);
    }
  }
  out[used < outn ? used : outn - 1] = 0;
}

/* ------------------------------------------------------------------ */
/* DIR$ state                                                          */
/* ------------------------------------------------------------------ */
static DIR *dir_handle = NULL;
static char dir_pattern[256];
static int  dir_kind;    /* 0 any, 1 file, 2 dir */

static void dir_next(Value *out)
{
  struct dirent *de;
  if (!dir_handle) { val_sets(out, ""); return; }
  while ((de = readdir(dir_handle))) {
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
    if (dir_pattern[0] && fnmatch(dir_pattern, de->d_name, 0) != 0) continue;
    if (dir_kind == 1 && de->d_type == DT_DIR) continue;
    if (dir_kind == 2 && de->d_type != DT_DIR) continue;
    val_sets(out, de->d_name);
    return;
  }
  closedir(dir_handle);
  dir_handle = NULL;
  val_sets(out, "");
}

/* ------------------------------------------------------------------ */
/* builtin table                                                       */
/* ------------------------------------------------------------------ */
static const char *builtins[] = {
  "ABS","ACOS","ASC","ASIN","ATN","ATAN2","BIN$","BOUND","CHOICE","CHR$",
  "CINT","COS","CWD$","DATE$","DEG","DIR$","EOF","EXP","FIELD$","FIX",
  "FORMAT$","HEX$","INKEY$","INSTR","INT","LCASE$","LEFT$","LEN","LOC",
  "LOF","LOG","LTRIM$","MAX","MID$","MIN","OCT$","PI","RAD","RGB",
  "RIGHT$","RND","RTRIM$","SGN","SIN","SPACE$","SQR","STR$","STRING$",
  "TAB","TAN","TIME$","TIMER","TRIM$","UCASE$","VAL","REPLACE$","INV",
  "MM.HRES","MM.VRES","MM.DEVICE$","MM.VER","MM.CMDLINE$","INPUT$","POS","ASIN",
  NULL
};

int is_builtin_func(const char *name)
{
  int i;
  for (i = 0; builtins[i]; i++)
    if (!strcasecmp(builtins[i], name)) return 1;
  return 0;
}

/* MMBasic allows both RGB("red") and the bare word RGB(red) */
static int named_colour(const char *c, uint32_t *out)
{
  if      (!strcasecmp(c,"black"))   *out = 0x000000;
  else if (!strcasecmp(c,"white"))   *out = 0xFFFFFF;
  else if (!strcasecmp(c,"red"))     *out = 0xFF0000;
  else if (!strcasecmp(c,"green"))   *out = 0x00FF00;
  else if (!strcasecmp(c,"blue"))    *out = 0x0000FF;
  else if (!strcasecmp(c,"yellow"))  *out = 0xFFFF00;
  else if (!strcasecmp(c,"cyan"))    *out = 0x00FFFF;
  else if (!strcasecmp(c,"magenta")) *out = 0xFF00FF;
  else if (!strcasecmp(c,"brown"))   *out = 0xA52A2A;
  else if (!strcasecmp(c,"orange"))  *out = 0xFFA500;
  else if (!strcasecmp(c,"pink"))    *out = 0xFFC0CB;
  else if (!strcasecmp(c,"purple"))  *out = 0x800080;
  else if (!strcasecmp(c,"grey") || !strcasecmp(c,"gray")) *out = 0x808080;
  else return 0;
  return 1;
}

static void str_sub(Value *out, const char *s, int start, int len)
{
  int n = (int)strlen(s);
  if (start < 1) start = 1;
  if (start > n) { val_sets(out, ""); return; }
  if (len < 0 || start - 1 + len > n) len = n - (start - 1);
  val_setsn(out, s + start - 1, len);
}

static void to_base(int64_t v, int base, int width, char *buf, size_t bn)
{
  char tmp[80];
  int n = 0, i;
  uint64_t u = (uint64_t)v;
  if (u == 0) tmp[n++] = '0';
  while (u && n < 70) {
    int d = (int)(u % (uint64_t)base);
    tmp[n++] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
    u /= (uint64_t)base;
  }
  while (n < width && n < 70) tmp[n++] = '0';
  for (i = 0; i < n && (size_t)i < bn - 1; i++) buf[i] = tmp[n - 1 - i];
  buf[i] = 0;
}

void call_builtin(const char *name, Value *out)
{
  Args a;
  char buf[512];

  val_init(out);

  /* ---- zero-argument constants (parens optional) ----------------- */
  if (!strcasecmp(name, "PI"))       { val_setf(out, M_PI); return; }
  if (!strcasecmp(name, "TIMER"))    { val_setf(out, con_timer_ms()); return; }
  if (!strcasecmp(name, "MM.HRES"))  { val_seti(out, gfx_width()); return; }
  if (!strcasecmp(name, "MM.VRES"))  { val_seti(out, gfx_height()); return; }
  if (!strcasecmp(name, "MM.VER"))   { val_setf(out, 1.0); return; }
  if (!strcasecmp(name, "MM.DEVICE$")) { val_sets(out, "SharkDeck"); return; }
  if (!strcasecmp(name, "MM.CMDLINE$")) { val_sets(out, shk_cmdline ? shk_cmdline : ""); return; }
  if (!strcasecmp(name, "POS"))      { val_seti(out, con_col()); return; }
  if (!strcasecmp(name, "INKEY$")) {
    int c = con_key();
    if (c <= 0) val_sets(out, "");
    else { char k[2]; k[0] = (char)c; k[1] = 0; val_sets(out, k); }
    return;
  }
  if (!strcasecmp(name, "DATE$")) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(buf, sizeof buf, "%02d-%02d-%04d",
             tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900);
    val_sets(out, buf);
    return;
  }
  if (!strcasecmp(name, "TIME$")) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(buf, sizeof buf, "%02d:%02d:%02d",
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    val_sets(out, buf);
    return;
  }
  if (!strcasecmp(name, "CWD$")) {
    if (!getcwd(buf, sizeof buf)) buf[0] = 0;
    val_sets(out, buf);
    return;
  }

  /* ---- everything else takes an argument list --------------------- */
  if (!strcasecmp(name, "INV"))   { parse_args(&a,1,1,name); val_seti(out, ~arg_i(&a,0)); args_free(&a); return; }
  if (!strcasecmp(name, "ABS"))   { parse_args(&a,1,1,name); val_setf(out, fabs(arg_f(&a,0))); args_free(&a); return; }
  if (!strcasecmp(name, "SGN"))   { double d; parse_args(&a,1,1,name); d = arg_f(&a,0); val_seti(out, d>0?1:(d<0?-1:0)); args_free(&a); return; }
  if (!strcasecmp(name, "INT"))   { parse_args(&a,1,1,name); val_seti(out, (int64_t)floor(arg_f(&a,0))); args_free(&a); return; }
  if (!strcasecmp(name, "FIX"))   { double d; parse_args(&a,1,1,name); d = arg_f(&a,0); val_seti(out, (int64_t)(d<0?ceil(d):floor(d))); args_free(&a); return; }
  if (!strcasecmp(name, "CINT"))  { parse_args(&a,1,1,name); val_seti(out, val_i(&a.v[0])); args_free(&a); return; }
  if (!strcasecmp(name, "SQR"))   { double d; parse_args(&a,1,1,name); d = arg_f(&a,0); if (d < 0) shk_error("SQR of a negative number"); val_setf(out, sqrt(d)); args_free(&a); return; }
  if (!strcasecmp(name, "EXP"))   { parse_args(&a,1,1,name); val_setf(out, exp(arg_f(&a,0))); args_free(&a); return; }
  if (!strcasecmp(name, "LOG"))   { double d; parse_args(&a,1,1,name); d = arg_f(&a,0); if (d <= 0) shk_error("LOG of a non-positive number"); val_setf(out, log(d)); args_free(&a); return; }
  if (!strcasecmp(name, "SIN"))   { parse_args(&a,1,1,name); val_setf(out, sin(to_rad(arg_f(&a,0)))); args_free(&a); return; }
  if (!strcasecmp(name, "COS"))   { parse_args(&a,1,1,name); val_setf(out, cos(to_rad(arg_f(&a,0)))); args_free(&a); return; }
  if (!strcasecmp(name, "TAN"))   { parse_args(&a,1,1,name); val_setf(out, tan(to_rad(arg_f(&a,0)))); args_free(&a); return; }
  if (!strcasecmp(name, "ATN"))   { parse_args(&a,1,1,name); val_setf(out, from_rad(atan(arg_f(&a,0)))); args_free(&a); return; }
  if (!strcasecmp(name, "ASIN"))  { parse_args(&a,1,1,name); val_setf(out, from_rad(asin(arg_f(&a,0)))); args_free(&a); return; }
  if (!strcasecmp(name, "ACOS"))  { parse_args(&a,1,1,name); val_setf(out, from_rad(acos(arg_f(&a,0)))); args_free(&a); return; }
  if (!strcasecmp(name, "ATAN2")) { parse_args(&a,2,2,name); val_setf(out, from_rad(atan2(arg_f(&a,0), arg_f(&a,1)))); args_free(&a); return; }
  if (!strcasecmp(name, "DEG"))   { parse_args(&a,1,1,name); val_setf(out, arg_f(&a,0) * 180.0 / M_PI); args_free(&a); return; }
  if (!strcasecmp(name, "RAD"))   { parse_args(&a,1,1,name); val_setf(out, arg_f(&a,0) * M_PI / 180.0); args_free(&a); return; }

  if (!strcasecmp(name, "MAX") || !strcasecmp(name, "MIN")) {
    int i, ismax = !strcasecmp(name, "MAX");
    double best;
    parse_args(&a, 1, MAXARG, name);
    best = arg_f(&a, 0);
    for (i = 1; i < a.n; i++) {
      double d = arg_f(&a, i);
      if (ismax ? (d > best) : (d < best)) best = d;
    }
    val_setf(out, best);
    args_free(&a);
    return;
  }

  if (!strcasecmp(name, "RND")) {
    if (at_op('(')) { parse_args(&a, 0, 1, name); args_free(&a); }
    val_setf(out, (double)rand() / ((double)RAND_MAX + 1.0));
    return;
  }

  if (!strcasecmp(name, "RGB")) {
    uint32_t named;
    /* RGB(red) - a bare colour word, before it is read as a variable */
    if (at_op('(') && peek(1)->type == T_IDENT &&
        peek(2)->type == T_OP && peek(2)->op == ')' &&
        named_colour(peek(1)->str, &named)) {
      advance(); advance(); advance();
      val_seti(out, (int64_t)named);
      return;
    }
    parse_args(&a, 1, 3, name);
    if (a.n == 1 && a.v[0].type == V_STR) {
      uint32_t v = 0xFFFFFF;
      named_colour(val_s(&a.v[0]), &v);
      val_seti(out, v);
    } else {
      int64_t r = arg_i(&a,0), g = a.n>1?arg_i(&a,1):0, b = a.n>2?arg_i(&a,2):0;
      if (r < 0) r = 0;
      if (r > 255) r = 255;
      if (g < 0) g = 0;
      if (g > 255) g = 255;
      if (b < 0) b = 0;
      if (b > 255) b = 255;
      val_seti(out, (r<<16)|(g<<8)|b);
    }
    args_free(&a);
    return;
  }

  /* ---- strings ---------------------------------------------------- */
  if (!strcasecmp(name, "LEN"))    { parse_args(&a,1,1,name); val_seti(out, (int64_t)strlen(arg_s(&a,0))); args_free(&a); return; }
  if (!strcasecmp(name, "ASC"))    { const char *s; parse_args(&a,1,1,name); s = arg_s(&a,0); val_seti(out, (unsigned char)s[0]); args_free(&a); return; }
  if (!strcasecmp(name, "CHR$"))   { char k[2]; parse_args(&a,1,1,name); k[0] = (char)arg_i(&a,0); k[1] = 0; val_setsn(out, k, 1); args_free(&a); return; }
  if (!strcasecmp(name, "LEFT$"))  { parse_args(&a,2,2,name); str_sub(out, arg_s(&a,0), 1, (int)arg_i(&a,1)); args_free(&a); return; }
  if (!strcasecmp(name, "RIGHT$")) {
    const char *s; int n, k;
    parse_args(&a,2,2,name);
    s = arg_s(&a,0); n = (int)strlen(s); k = (int)arg_i(&a,1);
    if (k < 0) k = 0;
    if (k > n) k = n;
    val_setsn(out, s + n - k, k);
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "MID$")) {
    parse_args(&a,2,3,name);
    str_sub(out, arg_s(&a,0), (int)arg_i(&a,1), has(&a,2) ? (int)arg_i(&a,2) : -1);
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "UCASE$") || !strcasecmp(name, "LCASE$")) {
    int up = !strcasecmp(name, "UCASE$");
    const char *s;
    char *d;
    int i;
    parse_args(&a,1,1,name);
    s = arg_s(&a,0);
    d = shk_strdup(s);
    for (i = 0; d[i]; i++)
      d[i] = (char)(up ? toupper((unsigned char)d[i]) : tolower((unsigned char)d[i]));
    val_sets(out, d);
    free(d);
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "TRIM$") || !strcasecmp(name, "LTRIM$") || !strcasecmp(name, "RTRIM$")) {
    const char *s, *b;
    int n;
    int dol = strcasecmp(name, "RTRIM$") != 0;
    int dor = strcasecmp(name, "LTRIM$") != 0;
    parse_args(&a,1,1,name);
    s = arg_s(&a,0);
    b = s;
    n = (int)strlen(s);
    if (dol) while (*b == ' ' || *b == '\t') { b++; n--; }
    if (dor) while (n > 0 && (b[n-1] == ' ' || b[n-1] == '\t')) n--;
    val_setsn(out, b, n);
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "SPACE$")) {
    int n, i;
    char *d;
    parse_args(&a,1,1,name);
    n = (int)arg_i(&a,0);
    if (n < 0) n = 0;
    if (n > STR_MAX) n = STR_MAX;
    d = shk_alloc((size_t)n + 1);
    for (i = 0; i < n; i++) d[i] = ' ';
    d[n] = 0;
    val_sets(out, d);
    free(d);
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "STRING$")) {
    int n, i, c;
    char *d;
    parse_args(&a,2,2,name);
    n = (int)arg_i(&a,0);
    if (a.v[1].type == V_STR) { const char *s = val_s(&a.v[1]); c = s[0] ? (unsigned char)s[0] : ' '; }
    else c = (int)arg_i(&a,1);
    if (n < 0) n = 0;
    if (n > STR_MAX) n = STR_MAX;
    d = shk_alloc((size_t)n + 1);
    for (i = 0; i < n; i++) d[i] = (char)c;
    d[n] = 0;
    val_setsn(out, d, n);
    free(d);
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "INSTR")) {
    const char *hay, *nee, *p;
    int start = 1;
    parse_args(&a,2,3,name);
    if (a.n == 3) { start = (int)arg_i(&a,0); hay = arg_s(&a,1); nee = arg_s(&a,2); }
    else { hay = arg_s(&a,0); nee = arg_s(&a,1); }
    if (start < 1) start = 1;
    if (start > (int)strlen(hay)) { val_seti(out, 0); args_free(&a); return; }
    p = strstr(hay + start - 1, nee);
    val_seti(out, p ? (int64_t)(p - hay) + 1 : 0);
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "REPLACE$")) {
    const char *s, *f, *r;
    char *d;
    size_t cap, used = 0, fl;
    parse_args(&a,3,3,name);
    s = arg_s(&a,0); f = arg_s(&a,1); r = arg_s(&a,2);
    fl = strlen(f);
    cap = 512;
    d = shk_alloc(cap);
    if (!fl) { val_sets(out, s); free(d); args_free(&a); return; }
    while (*s) {
      const char *m = strstr(s, f);
      size_t chunk = m ? (size_t)(m - s) : strlen(s);
      size_t add = chunk + (m ? strlen(r) : 0);
      while (used + add + 1 > cap) { cap *= 2; d = shk_realloc(d, cap); }
      memcpy(d + used, s, chunk); used += chunk;
      if (!m) break;
      memcpy(d + used, r, strlen(r)); used += strlen(r);
      s = m + fl;
    }
    d[used] = 0;
    val_sets(out, d);
    free(d);
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "STR$")) {
    parse_args(&a,1,3,name);
    if (a.v[0].type == V_STR) shk_error("STR$ expects a number");
    num_to_str(&a.v[0], buf, sizeof buf);
    val_sets(out, buf);
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "VAL")) {
    const char *s;
    char *end;
    double d;
    parse_args(&a,1,1,name);
    s = arg_s(&a,0);
    while (*s == ' ' || *s == '\t') s++;
    if (s[0] == '&') {
      int base = 16;
      char c = (char)toupper((unsigned char)s[1]);
      const char *q;
      uint64_t acc = 0;
      if (c == 'B') base = 2; else if (c == 'O') base = 8;
      q = s + (isalpha((unsigned char)s[1]) ? 2 : 1);
      while (*q) {
        int v;
        if (isdigit((unsigned char)*q)) v = *q - '0';
        else if (isalpha((unsigned char)*q)) v = toupper((unsigned char)*q) - 'A' + 10;
        else break;
        if (v >= base) break;
        acc = acc * (uint64_t)base + (uint64_t)v;
        q++;
      }
      val_seti(out, (int64_t)acc);
      args_free(&a);
      return;
    }
    d = strtod(s, &end);
    if (end == s) val_setf(out, 0);
    else if (d == floor(d) && fabs(d) < 9e15 && !strpbrk(s, ".eE"))
      val_seti(out, (int64_t)d);
    else val_setf(out, d);
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "HEX$") || !strcasecmp(name, "BIN$") || !strcasecmp(name, "OCT$")) {
    int base = !strcasecmp(name,"HEX$") ? 16 : (!strcasecmp(name,"BIN$") ? 2 : 8);
    int width = 0;
    parse_args(&a,1,2,name);
    if (has(&a,1)) width = (int)arg_i(&a,1);
    to_base(arg_i(&a,0), base, width, buf, sizeof buf);
    val_sets(out, buf);
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "FORMAT$")) {
    parse_args(&a,1,2,name);
    do_format(has(&a,1) ? arg_s(&a,1) : "%g", arg_f(&a,0), buf, sizeof buf);
    val_sets(out, buf);
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "FIELD$")) {
    /* FIELD$(string, n, delimiters) */
    const char *s, *dl;
    int want, cur_field = 1;
    const char *start;
    parse_args(&a,2,3,name);
    s = arg_s(&a,0);
    want = (int)arg_i(&a,1);
    dl = has(&a,2) ? arg_s(&a,2) : ",";
    start = s;
    while (*s) {
      if (strchr(dl, *s)) {
        if (cur_field == want) { val_setsn(out, start, (int)(s - start)); args_free(&a); return; }
        cur_field++;
        start = s + 1;
      }
      s++;
    }
    if (cur_field == want) val_setsn(out, start, (int)(s - start));
    else val_sets(out, "");
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "CHOICE")) {
    parse_args(&a,3,3,name);
    val_copy(out, val_truth(&a.v[0]) ? &a.v[1] : &a.v[2]);
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "TAB")) {
    int col, want, i;
    parse_args(&a,1,1,name);
    want = (int)arg_i(&a,0);
    col = con_col() + 1;
    if (want < 1) want = 1;
    {
      char *d;
      int n = want - col;
      if (n < 0) n = 0;
      d = shk_alloc((size_t)n + 1);
      for (i = 0; i < n; i++) d[i] = ' ';
      d[n] = 0;
      val_sets(out, d);
      free(d);
    }
    args_free(&a);
    return;
  }

  /* ---- files ------------------------------------------------------ */
  if (!strcasecmp(name, "EOF")) {
    int h;
    int c;
    parse_args(&a,1,1,name);
    h = (int)arg_i(&a,0);
    if (h < 1 || h >= MAXFILE || !vm.fh[h]) shk_error("File #%d is not open", h);
    c = fgetc(vm.fh[h]);
    if (c == EOF) val_seti(out, 1);
    else { ungetc(c, vm.fh[h]); val_seti(out, 0); }
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "LOC")) {
    int h;
    parse_args(&a,1,1,name);
    h = (int)arg_i(&a,0);
    if (h < 1 || h >= MAXFILE || !vm.fh[h]) shk_error("File #%d is not open", h);
    val_seti(out, (int64_t)ftell(vm.fh[h]));
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "LOF")) {
    int h;
    long cur_pos, end;
    parse_args(&a,1,1,name);
    h = (int)arg_i(&a,0);
    if (h < 1 || h >= MAXFILE || !vm.fh[h]) shk_error("File #%d is not open", h);
    cur_pos = ftell(vm.fh[h]);
    fseek(vm.fh[h], 0, SEEK_END);
    end = ftell(vm.fh[h]);
    fseek(vm.fh[h], cur_pos, SEEK_SET);
    val_seti(out, (int64_t)end);
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "INPUT$")) {
    /* INPUT$(n, #h) or INPUT$(n) from the console */
    int n, h = 0, got;
    char *d;
    memset(&a, 0, sizeof a);
    expect_op('(');
    {
      Value v;
      val_init(&v);
      parse_or(&v);
      n = (int)val_i(&v);
      val_free(&v);
    }
    if (accept_op(',')) {
      accept_op('#');
      {
        Value v;
        val_init(&v);
        parse_or(&v);
        h = (int)val_i(&v);
        val_free(&v);
      }
    }
    expect_op(')');
    if (n < 0) n = 0;
    d = shk_alloc((size_t)n + 1);
    if (h) {
      if (h < 1 || h >= MAXFILE || !vm.fh[h]) { free(d); shk_error("File #%d is not open", h); }
      got = (int)fread(d, 1, (size_t)n, vm.fh[h]);
    } else {
      for (got = 0; got < n; got++) d[got] = (char)con_getkey_block();
    }
    val_setsn(out, d, got);
    free(d);
    return;
  }
  if (!strcasecmp(name, "DIR$")) {
    parse_args(&a,0,2,name);
    if (a.n >= 1 && a.present[0]) {
      const char *pat = arg_s(&a,0);
      const char *slash = strrchr(pat, '/');
      char dirpart[256];
      if (dir_handle) { closedir(dir_handle); dir_handle = NULL; }
      dir_kind = 0;
      if (has(&a,1)) {
        const char *k = val_s(&a.v[1]);
        if (!strcasecmp(k, "DIR")) dir_kind = 2;
        else if (!strcasecmp(k, "FILE")) dir_kind = 1;
      }
      if (slash) {
        size_t dl = (size_t)(slash - pat);
        if (dl >= sizeof dirpart) dl = sizeof dirpart - 1;
        memcpy(dirpart, pat, dl);
        dirpart[dl] = 0;
        snprintf(dir_pattern, sizeof dir_pattern, "%s", slash + 1);
      } else {
        snprintf(dirpart, sizeof dirpart, ".");
        snprintf(dir_pattern, sizeof dir_pattern, "%s", pat);
      }
      dir_handle = opendir(dirpart[0] ? dirpart : ".");
    }
    dir_next(out);
    args_free(&a);
    return;
  }
  if (!strcasecmp(name, "BOUND")) {
    shk_error("BOUND must be applied to an array");
    return;
  }

  shk_error("Unknown function %s", name);
}

/* ------------------------------------------------------------------ */
/* atoms                                                               */
/* ------------------------------------------------------------------ */
/* Parse subscripts for an array reference and return the flat index.  */
static int parse_subscripts(Var *v)
{
  int subs[MAXDIM];
  int n = 0;
  expect_op('(');
  if (!at_op(')')) {
    for (;;) {
      Value iv;
      val_init(&iv);
      parse_or(&iv);
      if (n >= MAXDIM) shk_error("Too many subscripts");
      subs[n++] = (int)val_i(&iv);
      val_free(&iv);
      if (accept_op(',')) continue;
      break;
    }
  }
  expect_op(')');
  return var_index(v, subs, n);
}

static void parse_atom(Value *out)
{
  Tok *t = cur();

  val_init(out);

  if (t->type == T_INT)  { val_seti(out, t->inum); advance(); return; }
  if (t->type == T_NUM)  { val_setf(out, t->num);  advance(); return; }
  if (t->type == T_STR)  { val_sets(out, t->str ? t->str : ""); advance(); return; }

  if (t->type == T_OP && t->op == '(') {
    advance();
    parse_or(out);
    expect_op(')');
    return;
  }

  if (t->type == T_KW) {
    switch (t->kw) {
      case KW_TIMER:
        advance();
        val_setf(out, con_timer_ms());
        return;
      case KW_PIXEL: {
        /* PIXEL(x, y) reads the colour back out of the canvas */
        Args a;
        advance();
        parse_args(&a, 2, 2, "PIXEL");
        val_seti(out, (int64_t)gfx_getpixel((int)arg_i(&a,0), (int)arg_i(&a,1)));
        args_free(&a);
        return;
      }
      case KW_NOT: {
        Value v;
        advance();
        val_init(&v);
        parse_atom(&v);
        val_seti(out, val_truth(&v) ? 0 : 1);
        val_free(&v);
        return;
      }
      default:
        shk_error("Unexpected %s in an expression", tok_text(t));
    }
  }

  if (t->type == T_IDENT) {
    char name[MAXNAME];
    Var *v;
    Proc *p;
    snprintf(name, sizeof name, "%s", t->str);

    /* BOUND(a(), n) needs the array itself, not its value */
    if (!strcasecmp(name, "BOUND")) {
      Var *av;
      int which = 0;
      advance();
      expect_op('(');
      if (cur()->type != T_IDENT) shk_error("BOUND expects an array");
      av = var_find(cur()->str);
      if (!av) shk_error("Array %s is not declared", cur()->str);
      av = var_deref(av);
      advance();
      if (accept_op('(')) expect_op(')');
      if (accept_op(',')) {
        Value iv;
        val_init(&iv);
        parse_or(&iv);
        which = (int)val_i(&iv);
        val_free(&iv);
      }
      expect_op(')');
      if (!(av->flags & VF_ARRAY)) shk_error("%s is not an array", av->name);
      if (which < 0 || which >= av->ndim) which = 0;
      val_seti(out, av->dim[which] - 1 + vm.opt_base);
      return;
    }

    v = var_find(name);

    /* Inside a FUNCTION its own name is also the return-value variable,
     * so "f(x)" there means a recursive call, not a subscript.         */
    p = prog_find_proc(name);
    if (p && p->is_func && peek(1)->type == T_OP && peek(1)->op == '(' &&
        (!v || !(var_deref(v)->flags & VF_ARRAY))) {
      advance();
      call_proc(p, out);
      return;
    }

    if (v) {
      Var *d = var_deref(v);
      advance();
      if (d->flags & VF_ARRAY) {
        int idx;
        if (!at_op('(')) shk_error("Array %s needs a subscript", d->name);
        idx = parse_subscripts(d);
        var_get(d, idx, out);
      } else {
        var_get(d, -1, out);
      }
      return;
    }

    p = prog_find_proc(name);
    if (p) {
      if (!p->is_func) shk_error("%s is a SUB, not a FUNCTION", name);
      advance();
      call_proc(p, out);
      return;
    }

    if (is_builtin_func(name)) {
      advance();
      call_builtin(name, out);
      return;
    }

    /* unknown name: create it (unless OPTION EXPLICIT) */
    advance();
    if (at_op('(')) shk_error("%s is not a known function or declared array", name);
    v = var_lookup_or_create(name, 0);
    var_get(v, -1, out);
    return;
  }

  shk_error("Expected an expression, found %s", tok_text(t));
}

/* ------------------------------------------------------------------ */
/* operator layers                                                     */
/* ------------------------------------------------------------------ */
static void parse_pow(Value *out)
{
  parse_atom(out);
  if (at_op('^')) {
    Value rhs;
    double base, e;
    advance();
    val_init(&rhs);
    /* right associative, and unary minus binds tighter on the right */
    if (at_op('-')) { advance(); parse_pow(&rhs); val_setf(&rhs, -val_f(&rhs)); }
    else parse_pow(&rhs);
    need_num(out, "^");
    need_num(&rhs, "^");
    base = val_f(out);
    e = val_f(&rhs);
    val_setf(out, pow(base, e));
    val_free(&rhs);
  }
}

static void parse_unary(Value *out)
{
  if (at_op('-')) {
    advance();
    parse_unary(out);
    need_num(out, "-");
    if (out->type == V_INT) val_seti(out, -out->i);
    else val_setf(out, -out->f);
    return;
  }
  if (at_op('+')) { advance(); parse_unary(out); need_num(out, "+"); return; }
  parse_pow(out);
}

static void parse_mul(Value *out)
{
  parse_unary(out);
  for (;;) {
    int op;
    Value rhs;
    if (at_op('*'))      op = '*';
    else if (at_op('/')) op = '/';
    else if (at_op('\\')) op = '\\';
    else if (at_kw(KW_MOD)) op = 'M';
    else return;
    advance();
    val_init(&rhs);
    parse_unary(&rhs);
    need_num(out, "arithmetic");
    need_num(&rhs, "arithmetic");
    switch (op) {
      case '*':
        if (both_int(out, &rhs)) val_seti(out, out->i * rhs.i);
        else val_setf(out, val_f(out) * val_f(&rhs));
        break;
      case '/': {
        double d = val_f(&rhs);
        if (d == 0) { val_free(&rhs); shk_error("Division by zero"); }
        val_setf(out, val_f(out) / d);
        break;
      }
      case '\\': {
        int64_t b = val_i(&rhs);
        if (b == 0) { val_free(&rhs); shk_error("Division by zero"); }
        val_seti(out, val_i(out) / b);
        break;
      }
      case 'M': {
        int64_t b = val_i(&rhs);
        if (b == 0) { val_free(&rhs); shk_error("Division by zero"); }
        val_seti(out, val_i(out) % b);
        break;
      }
    }
    val_free(&rhs);
  }
}

static void parse_add(Value *out)
{
  parse_mul(out);
  for (;;) {
    int plus;
    Value rhs;
    if (at_op('+')) plus = 1;
    else if (at_op('-')) plus = 0;
    else return;
    advance();
    val_init(&rhs);
    parse_mul(&rhs);
    if (out->type == V_STR || rhs.type == V_STR) {
      if (!plus || out->type != V_STR || rhs.type != V_STR) {
        val_free(&rhs);
        shk_error("Cannot mix strings and numbers here");
      }
      {
        const char *x = out->s ? out->s : "";
        const char *y = rhs.s ? rhs.s : "";
        size_t n = strlen(x) + strlen(y);
        char *d = shk_alloc(n + 1);
        strcpy(d, x);
        strcat(d, y);
        if (n > STR_MAX) { free(d); val_free(&rhs); shk_error("String too long"); }
        val_sets(out, d);
        free(d);
      }
    } else if (both_int(out, &rhs)) {
      val_seti(out, plus ? out->i + rhs.i : out->i - rhs.i);
    } else {
      double x = val_f(out), y = val_f(&rhs);
      val_setf(out, plus ? x + y : x - y);
    }
    val_free(&rhs);
  }
}

static void parse_shift(Value *out)
{
  parse_add(out);
  for (;;) {
    int left;
    Value rhs;
    int64_t n;
    if (at_op(OP_SHL)) left = 1;
    else if (at_op(OP_SHR)) left = 0;
    else return;
    advance();
    val_init(&rhs);
    parse_add(&rhs);
    need_num(out, "shift");
    n = val_i(&rhs);
    if (n < 0 || n > 63) val_seti(out, 0);
    else val_seti(out, left ? (val_i(out) << n) : (int64_t)((uint64_t)val_i(out) >> n));
    val_free(&rhs);
  }
}

static void parse_rel(Value *out)
{
  parse_shift(out);
  for (;;) {
    int op;
    Value rhs;
    int r;
    if (at_op('='))         op = '=';
    else if (at_op(OP_NE))  op = OP_NE;
    else if (at_op('<'))    op = '<';
    else if (at_op('>'))    op = '>';
    else if (at_op(OP_LE))  op = OP_LE;
    else if (at_op(OP_GE))  op = OP_GE;
    else return;
    advance();
    val_init(&rhs);
    parse_shift(&rhs);
    if (out->type == V_STR || rhs.type == V_STR) {
      if (out->type != V_STR || rhs.type != V_STR) {
        val_free(&rhs);
        shk_error("Cannot compare a string with a number");
      }
      r = str_cmp(out, &rhs);
    } else if (both_int(out, &rhs)) {
      r = out->i < rhs.i ? -1 : (out->i > rhs.i ? 1 : 0);
    } else {
      double x = val_f(out), y = val_f(&rhs);
      r = x < y ? -1 : (x > y ? 1 : 0);
    }
    val_free(&rhs);
    switch (op) {
      /* MMBasic comparisons yield 1 for true and 0 for false */
      case '=':    val_seti(out, r == 0); break;
      case OP_NE:  val_seti(out, r != 0); break;
      case '<':    val_seti(out, r <  0); break;
      case '>':    val_seti(out, r >  0); break;
      case OP_LE:  val_seti(out, r <= 0); break;
      case OP_GE:  val_seti(out, r >= 0); break;
    }
  }
}

static void parse_not(Value *out)
{
  if (at_kw(KW_NOT)) {
    advance();
    parse_not(out);
    need_num(out, "NOT");
    /* NOT is logical in MMBasic: INV is the bitwise complement */
    val_seti(out, val_truth(out) ? 0 : 1);
    return;
  }
  parse_rel(out);
}

static void parse_and(Value *out)
{
  parse_not(out);
  while (at_kw(KW_AND)) {
    Value rhs;
    advance();
    val_init(&rhs);
    parse_not(&rhs);
    need_num(out, "AND");
    need_num(&rhs, "AND");
    val_seti(out, val_i(out) & val_i(&rhs));
    val_free(&rhs);
  }
}

static void parse_or(Value *out)
{
  parse_and(out);
  for (;;) {
    int isxor;
    Value rhs;
    if (at_kw(KW_OR)) isxor = 0;
    else if (at_kw(KW_XOR)) isxor = 1;
    else return;
    advance();
    val_init(&rhs);
    parse_and(&rhs);
    need_num(out, "OR");
    need_num(&rhs, "OR");
    val_seti(out, isxor ? (val_i(out) ^ val_i(&rhs)) : (val_i(out) | val_i(&rhs)));
    val_free(&rhs);
  }
}

/* ------------------------------------------------------------------ */
void eval_expr(Value *out) { parse_or(out); }

void eval_expect_num(double *out)
{
  Value v;
  val_init(&v);
  parse_or(&v);
  if (v.type == V_STR) { val_free(&v); shk_error("Expected a number"); }
  *out = val_f(&v);
  val_free(&v);
}

int64_t eval_expect_int(void)
{
  Value v;
  int64_t r;
  val_init(&v);
  parse_or(&v);
  if (v.type == V_STR) { val_free(&v); shk_error("Expected a number"); }
  r = val_i(&v);
  val_free(&v);
  return r;
}

void eval_expect_str(Value *out)
{
  val_init(out);
  parse_or(out);
  if (out->type != V_STR) { val_free(out); shk_error("Expected a string"); }
}
