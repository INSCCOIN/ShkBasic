/* val.c - value construction, conversion and memory helpers */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "shk.h"

void *shk_alloc(size_t n)
{
  void *p = calloc(1, n ? n : 1);
  if (!p) shk_error("Out of memory");
  return p;
}

void *shk_realloc(void *p, size_t n)
{
  void *q = realloc(p, n ? n : 1);
  if (!q) shk_error("Out of memory");
  return q;
}

char *shk_strdup(const char *s)
{
  size_t n;
  char *p;
  if (!s) s = "";
  n = strlen(s);
  p = shk_alloc(n + 1);
  memcpy(p, s, n + 1);
  return p;
}

void val_init(Value *v)
{
  v->type = V_FLOAT;
  v->f = 0;
  v->i = 0;
  v->s = NULL;
}

void val_free(Value *v)
{
  if (v->s) { free(v->s); v->s = NULL; }
  v->type = V_FLOAT;
  v->f = 0;
  v->i = 0;
}

void val_copy(Value *dst, const Value *src)
{
  if (dst == src) return;
  if (dst->s) free(dst->s);
  dst->type = src->type;
  dst->f = src->f;
  dst->i = src->i;
  dst->s = src->s ? shk_strdup(src->s) : NULL;
}

void val_setf(Value *v, double d)
{
  if (v->s) { free(v->s); v->s = NULL; }
  v->type = V_FLOAT;
  v->f = d;
}

void val_seti(Value *v, int64_t i)
{
  if (v->s) { free(v->s); v->s = NULL; }
  v->type = V_INT;
  v->i = i;
}

void val_sets(Value *v, const char *s)
{
  char *n = shk_strdup(s);
  if (v->s) free(v->s);
  v->type = V_STR;
  v->s = n;
}

void val_setsn(Value *v, const char *s, int len)
{
  char *n;
  if (len < 0) len = 0;
  n = shk_alloc((size_t)len + 1);
  if (len) memcpy(n, s, (size_t)len);
  n[len] = 0;
  if (v->s) free(v->s);
  v->type = V_STR;
  v->s = n;
}

double val_f(const Value *v)
{
  if (v->type == V_FLOAT) return v->f;
  if (v->type == V_INT)   return (double)v->i;
  shk_error("Expected a number");
  return 0;
}

int64_t val_i(const Value *v)
{
  double d;
  if (v->type == V_INT)   return v->i;
  if (v->type == V_FLOAT) {
    d = v->f;
    /* MMBasic rounds when converting float to integer */
    if (d >= 0) d = floor(d + 0.5); else d = ceil(d - 0.5);
    if (d > 9.2233720368547758e18 || d < -9.2233720368547758e18)
      shk_error("Number too large");
    return (int64_t)d;
  }
  shk_error("Expected a number");
  return 0;
}

const char *val_s(const Value *v)
{
  if (v->type != V_STR) shk_error("Expected a string");
  return v->s ? v->s : "";
}

int val_truth(const Value *v)
{
  if (v->type == V_STR) shk_error("Expected a number");
  return (v->type == V_INT) ? (v->i != 0) : (v->f != 0);
}
