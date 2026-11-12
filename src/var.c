/* var.c - variables, arrays and scopes
 *
 * Lookup order is local frame then globals, matching MMBasic: a SUB
 * sees global variables unless it declares a LOCAL of the same name.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "shk.h"

static unsigned hash_name(const char *s)
{
  unsigned h = 5381;
  while (*s) h = h * 33u + (unsigned char)toupper((unsigned char)*s++);
  return h % VHASH;
}

static void upper_copy(char *dst, const char *src)
{
  int i = 0;
  while (src[i] && i < MAXNAME - 1) {
    dst[i] = (char)toupper((unsigned char)src[i]);
    i++;
  }
  dst[i] = 0;
}

/* Returns the declared type from a name suffix; base receives the name
 * with its suffix retained (names keep suffixes so a$ and a differ).  */
int name_type(const char *name, char *base)
{
  int n = (int)strlen(name);
  if (base) upper_copy(base, name);
  if (n > 0) {
    char c = name[n-1];
    if (c == '$') return V_STR;
    if (c == '%') return V_INT;
    if (c == '!') return V_FLOAT;
  }
  return 0;   /* unsuffixed: caller applies OPTION DEFAULT */
}

void var_init(void)
{
  memset(&vm.globals, 0, sizeof vm.globals);
}

static void var_free_one(Var *v)
{
  int i;
  if (!(v->flags & VF_ALIAS)) {
    if (v->flags & VF_ARRAY) {
      if (v->type == V_STR && v->a.s) {
        for (i = 0; i < v->nelem; i++) free(v->a.s[i]);
        free(v->a.s);
      } else if (v->type == V_INT) free(v->a.i);
      else free(v->a.f);
    } else if (v->type == V_STR) {
      free(v->ss);
    }
  }
  free(v);
}

void vt_clear(VarTab *t)
{
  int i;
  for (i = 0; i < VHASH; i++) {
    Var *v = t->b[i];
    while (v) { Var *n = v->next; var_free_one(v); v = n; }
    t->b[i] = NULL;
  }
}

void var_clear_all(void)
{
  int i;
  for (i = 0; i < vm.ncall; i++) vt_clear(&vm.call[i].locals);
  vm.ncall = 0;
  vt_clear(&vm.globals);
}

Var *var_find_in(VarTab *t, const char *name)
{
  char key[MAXNAME];
  Var *v;
  upper_copy(key, name);
  for (v = t->b[hash_name(key)]; v; v = v->next)
    if (!strcmp(v->name, key)) return v;
  return NULL;
}

Var *var_find(const char *name)
{
  Var *v;
  if (vm.ncall > 0) {
    v = var_find_in(&vm.call[vm.ncall-1].locals, name);
    if (v) return v;
  }
  return var_find_in(&vm.globals, name);
}

Var *var_create(VarTab *t, const char *name, int type)
{
  char key[MAXNAME];
  unsigned h;
  Var *v;
  upper_copy(key, name);
  h = hash_name(key);
  v = shk_alloc(sizeof(Var));
  memcpy(v->name, key, sizeof v->name);
  v->type = (uint8_t)type;
  v->nelem = 0;
  v->ndim = 0;
  if (type == V_STR) v->ss = shk_strdup("");
  v->next = t->b[h];
  t->b[h] = v;
  return v;
}

Var *var_lookup_or_create(const char *name, int type)
{
  Var *v = var_find(name);
  VarTab *t;
  if (v) return v;
  if (vm.opt_explicit)
    shk_error("Variable %s is not declared (OPTION EXPLICIT)", name);
  if (!type) {
    type = name_type(name, NULL);
    if (!type) type = vm.opt_default ? vm.opt_default : V_FLOAT;
  }
  /* implicit variables are created in the innermost scope that is
   * executing, which for MMBasic means global unless inside a sub   */
  t = (vm.ncall > 0) ? &vm.call[vm.ncall-1].locals : &vm.globals;
  if (vm.ncall > 0) t = &vm.globals;   /* MMBasic: implicit = global */
  return var_create(t, name, type);
}

Var *var_deref(Var *v)
{
  while (v && (v->flags & VF_ALIAS)) v = v->alias;
  return v;
}

void var_dim(Var *v, int ndim, const int *dims)
{
  int i;
  long long n = 1;
  if (ndim < 1 || ndim > MAXDIM) shk_error("Too many dimensions");
  for (i = 0; i < ndim; i++) {
    if (dims[i] <= 0) shk_error("Invalid array size");
    n *= dims[i];
    if (n > 8000000LL) shk_error("Array too large");
  }
  v->flags |= VF_ARRAY;
  v->ndim = ndim;
  for (i = 0; i < ndim; i++) v->dim[i] = dims[i];
  v->nelem = (int)n;
  if (v->type == V_STR) {
    v->a.s = shk_alloc((size_t)n * sizeof(char *));
    for (i = 0; i < n; i++) v->a.s[i] = shk_strdup("");
  } else if (v->type == V_INT) {
    v->a.i = shk_alloc((size_t)n * sizeof(int64_t));
  } else {
    v->a.f = shk_alloc((size_t)n * sizeof(double));
  }
}

int var_index(Var *v, const int *subs, int nsub)
{
  int i, idx = 0;
  v = var_deref(v);
  if (!(v->flags & VF_ARRAY)) shk_error("%s is not an array", v->name);
  if (nsub != v->ndim)
    shk_error("Expected %d dimension(s) for %s", v->ndim, v->name);
  for (i = 0; i < nsub; i++) {
    int s = subs[i] - vm.opt_base;
    if (s < 0 || s >= v->dim[i])
      shk_error("Index %d out of bounds for %s", subs[i], v->name);
    idx = idx * v->dim[i] + s;
  }
  return idx;
}

void var_get(Var *v, int idx, Value *out)
{
  v = var_deref(v);
  if (idx < 0) {
    switch (v->type) {
      case V_STR:   val_sets(out, v->ss ? v->ss : ""); break;
      case V_INT:   val_seti(out, v->si); break;
      default:      val_setf(out, v->sf); break;
    }
  } else {
    if (idx < 0 || idx >= v->nelem) shk_error("Index out of bounds");
    switch (v->type) {
      case V_STR:   val_sets(out, v->a.s[idx] ? v->a.s[idx] : ""); break;
      case V_INT:   val_seti(out, v->a.i[idx]); break;
      default:      val_setf(out, v->a.f[idx]); break;
    }
  }
}

void var_set(Var *v, int idx, const Value *val)
{
  v = var_deref(v);
  if (v->flags & VF_CONST) shk_error("Cannot assign to constant %s", v->name);

  if (v->type == V_STR) {
    const char *s;
    char *copy;
    if (val->type != V_STR) shk_error("Expected a string for %s", v->name);
    s = val->s ? val->s : "";
    if ((int)strlen(s) > STR_MAX) shk_error("String too long");
    copy = shk_strdup(s);
    if (idx < 0) { free(v->ss); v->ss = copy; }
    else {
      if (idx < 0 || idx >= v->nelem) shk_error("Index out of bounds");
      free(v->a.s[idx]); v->a.s[idx] = copy;
    }
    return;
  }

  if (val->type == V_STR) shk_error("Expected a number for %s", v->name);

  if (v->type == V_INT) {
    int64_t n = val_i(val);
    if (idx < 0) v->si = n;
    else {
      if (idx >= v->nelem) shk_error("Index out of bounds");
      v->a.i[idx] = n;
    }
  } else {
    double d = val_f(val);
    if (idx < 0) v->sf = d;
    else {
      if (idx >= v->nelem) shk_error("Index out of bounds");
      v->a.f[idx] = d;
    }
  }
}
