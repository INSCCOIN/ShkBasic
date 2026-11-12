/* exec.c - statement execution and control flow */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include "shk.h"

VM vm;
char *shk_cmdline = NULL;

void num_to_str(const Value *v, char *buf, size_t n);   /* eval.c */

static uint32_t cur_fg = 0xFFFFFF;
static uint32_t cur_bg = 0x000000;
static int      cur_font_scale = 1;

/* ------------------------------------------------------------------ */
/* errors                                                             */
/* ------------------------------------------------------------------ */
void shk_error(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(vm.err_msg, sizeof vm.err_msg, fmt, ap);
  va_end(ap);
  vm.err_line = (vm.pc >= 0 && vm.pc < vm.ntok) ? vm.tok[vm.pc].line : 0;
  longjmp(vm.err_jmp, 1);
}

/* ------------------------------------------------------------------ */
/* token cursor                                                        */
/* ------------------------------------------------------------------ */
Tok *cur(void)
{
  static Tok eof_tok;
  if (vm.pc < 0 || vm.pc >= vm.ntok) {
    memset(&eof_tok, 0, sizeof eof_tok);
    eof_tok.type = T_EOF;
    return &eof_tok;
  }
  return &vm.tok[vm.pc];
}

Tok *peek(int n)
{
  static Tok eof_tok;
  int i = vm.pc + n;
  if (i < 0 || i >= vm.ntok) {
    memset(&eof_tok, 0, sizeof eof_tok);
    eof_tok.type = T_EOF;
    return &eof_tok;
  }
  return &vm.tok[i];
}

void advance(void) { if (vm.pc < vm.ntok) vm.pc++; }

int at_op(int op) { Tok *t = cur(); return t->type == T_OP && t->op == op; }
int at_kw(int kw) { Tok *t = cur(); return t->type == T_KW && t->kw == kw; }
int accept_op(int op) { if (at_op(op)) { advance(); return 1; } return 0; }
int accept_kw(int kw) { if (at_kw(kw)) { advance(); return 1; } return 0; }

void expect_op(int op)
{
  char c[3];
  if (accept_op(op)) return;
  switch (op) {
    case OP_LE: strcpy(c, "<="); break;
    case OP_GE: strcpy(c, ">="); break;
    case OP_NE: strcpy(c, "<>"); break;
    default: c[0] = (char)op; c[1] = 0; break;
  }
  shk_error("Expected '%s' but found %s", c, tok_text(cur()));
}

void expect_kw(int kw)
{
  if (accept_kw(kw)) return;
  shk_error("Expected %s but found %s", kw_name[kw], tok_text(cur()));
}

int at_end_of_stmt(void)
{
  Tok *t = cur();
  if (t->type == T_EOL || t->type == T_EOF || t->type == T_COLON) return 1;
  /* on a single-line IF, ELSE terminates the preceding statement */
  if (t->type == T_KW && t->kw == KW_ELSE) return 1;
  return 0;
}

void skip_to_eol(void)
{
  while (cur()->type != T_EOL && cur()->type != T_EOF) advance();
}

/* skip the remainder of the current statement */
static void skip_stmt(void)
{
  int depth = 0;
  for (;;) {
    Tok *t = cur();
    if (t->type == T_EOF || t->type == T_EOL) return;
    if (t->type == T_COLON && depth == 0) return;
    if (t->type == T_OP && t->op == '(') depth++;
    if (t->type == T_OP && t->op == ')') depth--;
    advance();
  }
}

/* ------------------------------------------------------------------ */
/* lvalues                                                             */
/* ------------------------------------------------------------------ */
typedef struct { Var *v; int idx; } LValue;

static int parse_subs_for(Var *v)
{
  int subs[MAXDIM];
  int n = 0;
  expect_op('(');
  if (!at_op(')')) {
    for (;;) {
      Value iv;
      val_init(&iv);
      eval_expr(&iv);
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

static LValue parse_lvalue(void)
{
  LValue lv;
  char name[MAXNAME];
  Var *v;

  if (cur()->type != T_IDENT) shk_error("Expected a variable, found %s", tok_text(cur()));
  snprintf(name, sizeof name, "%s", cur()->str);
  advance();

  v = var_find(name);
  if (!v) v = var_lookup_or_create(name, 0);
  v = var_deref(v);

  lv.v = v;
  lv.idx = -1;
  if (at_op('(')) {
    if (!(v->flags & VF_ARRAY)) shk_error("%s is not an array", v->name);
    lv.idx = parse_subs_for(v);
  } else if (v->flags & VF_ARRAY) {
    shk_error("Array %s needs a subscript", v->name);
  }
  return lv;
}

/* ------------------------------------------------------------------ */
/* runtime stack                                                       */
/* ------------------------------------------------------------------ */
static Stk *push_stk(int kind)
{
  if (vm.sp >= MAXSTACK) shk_error("Too many nested loops or GOSUBs");
  memset(&vm.stk[vm.sp], 0, sizeof vm.stk[vm.sp]);
  vm.stk[vm.sp].kind = kind;
  return &vm.stk[vm.sp++];
}

static void pop_stk(void)
{
  if (vm.sp <= 0) shk_error("Stack underflow");
  vm.sp--;
  if (vm.stk[vm.sp].kind == S_SELECT) val_free(&vm.stk[vm.sp].sel);
}

/* ------------------------------------------------------------------ */
/* optional command arguments                                          */
/* ------------------------------------------------------------------ */
#define CMDMAX 12

typedef struct {
  int   n;
  Value v[CMDMAX];
  int   present[CMDMAX];
} CmdArgs;

static void cmd_init(CmdArgs *c)
{
  int i;
  memset(c, 0, sizeof *c);
  for (i = 0; i < CMDMAX; i++) val_init(&c->v[i]);
}

static void cmd_free(CmdArgs *c)
{
  int i;
  for (i = 0; i < CMDMAX; i++) val_free(&c->v[i]);
  c->n = 0;
}

/* Read comma-separated arguments up to end of statement.  Empty slots
 * (as in "LINE 0,0,10,10,,RGB(red)") are recorded as absent.          */
static void cmd_read(CmdArgs *c, int maxn)
{
  cmd_init(c);
  if (at_end_of_stmt()) return;
  for (;;) {
    if (c->n >= maxn || c->n >= CMDMAX) shk_error("Too many arguments");
    if (at_op(',') || at_end_of_stmt()) {
      c->present[c->n] = 0;
      c->n++;
    } else {
      eval_expr(&c->v[c->n]);
      c->present[c->n] = 1;
      c->n++;
    }
    if (accept_op(',')) continue;
    break;
  }
}

static int cmd_has(CmdArgs *c, int i) { return i < c->n && c->present[i]; }

static int64_t cmd_int(CmdArgs *c, int i, int64_t dflt)
{
  if (!cmd_has(c, i)) return dflt;
  return val_i(&c->v[i]);
}

static double cmd_num(CmdArgs *c, int i, double dflt)
{
  if (!cmd_has(c, i)) return dflt;
  return val_f(&c->v[i]);
}

/* ------------------------------------------------------------------ */
/* PRINT                                                               */
/* ------------------------------------------------------------------ */
static void out_str(int fh, const char *s)
{
  if (fh) {
    if (fh < 1 || fh >= MAXFILE || !vm.fh[fh]) shk_error("File #%d is not open", fh);
    fputs(s, vm.fh[fh]);
  } else {
    con_out(s);
  }
}

static void do_print(void)
{
  int fh = 0;
  int newline = 1;
  char buf[512];

  if (at_op('#')) {
    advance();
    fh = (int)eval_expect_int();
    if (!accept_op(',')) { if (!at_end_of_stmt()) shk_error("Expected ',' after the file number"); }
  }

  while (!at_end_of_stmt()) {
    if (accept_op(';')) { newline = 0; continue; }
    if (accept_op(',')) {
      /* tab to the next 10-column zone */
      int col = fh ? 0 : con_col();
      int pad = 10 - (col % 10);
      int i;
      for (i = 0; i < pad; i++) out_str(fh, " ");
      newline = 1;
      continue;
    }
    {
      Value v;
      val_init(&v);
      eval_expr(&v);
      if (v.type == V_STR) out_str(fh, v.s ? v.s : "");
      else { num_to_str(&v, buf, sizeof buf); out_str(fh, buf); }
      val_free(&v);
      newline = 1;
    }
  }
  if (newline) out_str(fh, "\n");
  if (!fh) con_flush();
}

/* ------------------------------------------------------------------ */
/* INPUT                                                               */
/* ------------------------------------------------------------------ */
static void assign_from_text(LValue *lv, const char *text)
{
  Value v;
  val_init(&v);
  if (lv->v->type == V_STR) {
    val_sets(&v, text);
  } else {
    char *end;
    double d = strtod(text, &end);
    if (end == text) d = 0;
    if (lv->v->type == V_INT) val_seti(&v, (int64_t)(d < 0 ? ceil(d - 0.5) : floor(d + 0.5)));
    else val_setf(&v, d);
  }
  var_set(lv->v, lv->idx, &v);
  val_free(&v);
}

static void do_input(int line_mode)
{
  int fh = 0;
  char prompt[256];
  LValue lv[16];
  int nlv = 0;
  int i;

  prompt[0] = 0;

  if (at_op('#')) {
    advance();
    fh = (int)eval_expect_int();
    accept_op(',');
  } else if (cur()->type == T_STR && (peek(1)->type == T_OP &&
             (peek(1)->op == ';' || peek(1)->op == ','))) {
    snprintf(prompt, sizeof prompt, "%s", cur()->str ? cur()->str : "");
    advance();
    advance();
  }

  while (!at_end_of_stmt()) {
    if (nlv >= 16) shk_error("Too many variables in INPUT");
    lv[nlv++] = parse_lvalue();
    if (accept_op(',')) continue;
    break;
  }
  if (!nlv) shk_error("INPUT needs at least one variable");

  if (fh) {
    if (fh < 1 || fh >= MAXFILE || !vm.fh[fh]) shk_error("File #%d is not open", fh);
    for (i = 0; i < nlv; i++) {
      char buf[1024];
      if (line_mode) {
        if (!fgets(buf, sizeof buf, vm.fh[fh])) buf[0] = 0;
        {
          size_t n = strlen(buf);
          while (n && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = 0;
        }
        assign_from_text(&lv[i], buf);
      } else {
        int c, n = 0;
        int inq = 0;
        /* read one comma- or newline-delimited field */
        while ((c = fgetc(vm.fh[fh])) != EOF) {
          if (c == '"') { inq = !inq; continue; }
          if (!inq && (c == ',' || c == '\n')) break;
          if (c == '\r') continue;
          if (n < (int)sizeof buf - 1) buf[n++] = (char)c;
        }
        buf[n] = 0;
        assign_from_text(&lv[i], buf);
      }
    }
    return;
  }

  for (;;) {
    char *line;
    char pbuf[300];
    if (prompt[0]) snprintf(pbuf, sizeof pbuf, "%s", prompt);
    else snprintf(pbuf, sizeof pbuf, "? ");
    line = con_readline(pbuf);
    if (!line) { /* EOF on stdin */
      for (i = 0; i < nlv; i++) assign_from_text(&lv[i], "");
      return;
    }
    if (line_mode) {
      assign_from_text(&lv[0], line);
      free(line);
      return;
    }
    {
      /* split on commas */
      char *p = line;
      int ok = 1;
      for (i = 0; i < nlv; i++) {
        char *q = strchr(p, ',');
        if (q) *q = 0;
        while (*p == ' ') p++;
        assign_from_text(&lv[i], p);
        if (!q) {
          if (i < nlv - 1) ok = 0;
          p = p + strlen(p);
        } else p = q + 1;
      }
      free(line);
      if (ok) return;
      con_out("Please enter all the values, separated by commas\n");
    }
  }
}

/* ------------------------------------------------------------------ */
/* DIM / LOCAL / CONST                                                 */
/* ------------------------------------------------------------------ */
static void do_dim(int local)
{
  int deftype = 0;
  VarTab *tab = local ? &vm.call[vm.ncall-1].locals
                      : (vm.ncall > 0 ? &vm.call[vm.ncall-1].locals : &vm.globals);

  if (local && vm.ncall == 0) shk_error("LOCAL is only valid inside a SUB or FUNCTION");
  if (!local) tab = &vm.globals;

  /* leading type: DIM INTEGER a, b */
  if (accept_kw(KW_INTEGER))      deftype = V_INT;
  else if (accept_kw(KW_FLOAT))   deftype = V_FLOAT;
  else if (accept_kw(KW_STRINGT)) deftype = V_STR;
  else if (at_kw(KW_AS)) {
    advance();
    if (accept_kw(KW_INTEGER))      deftype = V_INT;
    else if (accept_kw(KW_FLOAT))   deftype = V_FLOAT;
    else if (accept_kw(KW_STRINGT)) deftype = V_STR;
    else shk_error("Expected a type after AS");
  }

  for (;;) {
    char name[MAXNAME];
    int dims[MAXDIM];
    int ndim = 0;
    int type;
    Var *v;

    if (cur()->type != T_IDENT) shk_error("Expected a variable name in DIM");
    snprintf(name, sizeof name, "%s", cur()->str);
    advance();

    if (at_op('(')) {
      advance();
      if (!at_op(')')) {
        for (;;) {
          int64_t hi, lo = vm.opt_base;
          hi = eval_expect_int();
          if (accept_kw(KW_TO)) { lo = hi; hi = eval_expect_int(); }
          if (ndim >= MAXDIM) shk_error("Too many dimensions");
          if (hi < lo) shk_error("Invalid array bounds");
          dims[ndim++] = (int)(hi - lo + 1);
          if (accept_op(',')) continue;
          break;
        }
      }
      expect_op(')');
    }

    type = name_type(name, NULL);
    if (!type) type = deftype;
    if (accept_kw(KW_AS)) {
      if (accept_kw(KW_INTEGER))      type = V_INT;
      else if (accept_kw(KW_FLOAT))   type = V_FLOAT;
      else if (accept_kw(KW_STRINGT)) type = V_STR;
      else shk_error("Expected a type after AS");
    }
    if (!type) type = vm.opt_default ? vm.opt_default : V_FLOAT;

    if (var_find_in(tab, name)) shk_error("%s is already declared", name);
    v = var_create(tab, name, type);
    if (ndim) var_dim(v, ndim, dims);

    if (accept_op('=')) {
      Value iv;
      val_init(&iv);
      if (ndim) {
        /* DIM a(3) = (1,2,3,4) */
        int i = 0;
        expect_op('(');
        for (;;) {
          eval_expr(&iv);
          if (i >= v->nelem) { val_free(&iv); shk_error("Too many initialisers"); }
          var_set(v, i++, &iv);
          if (accept_op(',')) continue;
          break;
        }
        expect_op(')');
      } else {
        eval_expr(&iv);
        var_set(v, -1, &iv);
      }
      val_free(&iv);
    }

    if (accept_op(',')) continue;
    break;
  }
}

static void do_const(void)
{
  for (;;) {
    char name[MAXNAME];
    Value v;
    Var *c;
    int type;
    if (cur()->type != T_IDENT) shk_error("Expected a name after CONST");
    snprintf(name, sizeof name, "%s", cur()->str);
    advance();
    expect_op('=');
    val_init(&v);
    eval_expr(&v);
    type = v.type == V_STR ? V_STR : (v.type == V_INT ? V_INT : V_FLOAT);
    if (var_find_in(&vm.globals, name)) { val_free(&v); shk_error("%s is already declared", name); }
    c = var_create(&vm.globals, name, type);
    var_set(c, -1, &v);
    c->flags |= VF_CONST;
    val_free(&v);
    if (accept_op(',')) continue;
    break;
  }
}

/* ------------------------------------------------------------------ */
/* FOR / NEXT                                                          */
/* ------------------------------------------------------------------ */
static int for_done(Stk *s)
{
  Value cv;
  int done;
  val_init(&cv);
  var_get(s->var, s->idx, &cv);
  if (s->isint) {
    int64_t v = val_i(&cv);
    done = s->istep >= 0 ? (v > s->ilimit) : (v < s->ilimit);
  } else {
    double v = val_f(&cv);
    done = s->step >= 0 ? (v > s->limit + 1e-12) : (v < s->limit - 1e-12);
  }
  val_free(&cv);
  return done;
}

static void do_for(void)
{
  LValue lv;
  Value start;
  Stk *s;
  int forpc = vm.pc - 1;      /* the FOR token itself */
  double limit, step = 1;
  int isint;

  lv = parse_lvalue();
  if (lv.v->type == V_STR) shk_error("FOR needs a numeric variable");
  expect_op('=');
  val_init(&start);
  eval_expr(&start);
  var_set(lv.v, lv.idx, &start);
  expect_kw(KW_TO);
  {
    Value lv2;
    val_init(&lv2);
    eval_expr(&lv2);
    limit = val_f(&lv2);
    isint = (lv.v->type == V_INT) && (start.type == V_INT) && (lv2.type == V_INT);
    val_free(&lv2);
  }
  if (accept_kw(KW_STEP)) {
    Value sv;
    val_init(&sv);
    eval_expr(&sv);
    step = val_f(&sv);
    if (sv.type != V_INT) isint = 0;
    val_free(&sv);
  }
  val_free(&start);
  if (step == 0) shk_error("FOR step cannot be zero");

  s = push_stk(S_FOR);
  s->var = lv.v;
  s->idx = lv.idx;
  s->limit = limit;
  s->step = step;
  s->isint = isint;
  s->ilimit = (int64_t)llround(limit);
  s->istep = (int64_t)llround(step);
  s->pc = vm.pc;                       /* loop body starts here */
  s->exitpc = vm.tok[forpc].link;      /* the NEXT token */

  if (for_done(s)) {
    pop_stk();
    vm.pc = s->exitpc + 1;
    skip_stmt();                       /* skip "NEXT [var]" */
  }
}

static void do_next(void)
{
  Stk *s;
  Value cv;

  /* NEXT may name its variable, or several */
  while (!at_end_of_stmt()) {
    if (cur()->type == T_IDENT) advance();
    else if (accept_op(',')) continue;
    else break;
  }

  while (vm.sp > 0 && vm.stk[vm.sp-1].kind != S_FOR) pop_stk();
  if (vm.sp == 0) shk_error("NEXT without FOR");
  s = &vm.stk[vm.sp-1];

  val_init(&cv);
  var_get(s->var, s->idx, &cv);
  if (s->isint) {
    Value nv;
    val_init(&nv);
    val_seti(&nv, val_i(&cv) + s->istep);
    var_set(s->var, s->idx, &nv);
    val_free(&nv);
  } else {
    Value nv;
    val_init(&nv);
    val_setf(&nv, val_f(&cv) + s->step);
    var_set(s->var, s->idx, &nv);
    val_free(&nv);
  }
  val_free(&cv);

  if (for_done(s)) {
    pop_stk();
    return;                /* fall through past NEXT */
  }
  vm.pc = s->pc;
}

/* ------------------------------------------------------------------ */
/* DO / LOOP, WHILE / WEND                                             */
/* ------------------------------------------------------------------ */
static void do_do(void)
{
  int dopc = vm.pc - 1;
  Stk *s;
  int cond_kind = 0;    /* 1 = WHILE, 2 = UNTIL */
  Value c;

  if (accept_kw(KW_WHILE)) cond_kind = 1;
  else if (accept_kw(KW_UNTIL)) cond_kind = 2;

  if (cond_kind) {
    val_init(&c);
    eval_expr(&c);
    {
      int t = val_truth(&c);
      val_free(&c);
      if ((cond_kind == 1 && !t) || (cond_kind == 2 && t)) {
        vm.pc = vm.tok[dopc].link + 1;    /* past LOOP */
        skip_stmt();
        return;
      }
    }
  }

  s = push_stk(S_DO);
  s->pc = dopc;                 /* re-evaluate the DO condition */
  s->exitpc = vm.tok[dopc].link;
}

static void do_loop(void)
{
  int looppc = vm.pc - 1;
  int cond_kind = 0;
  Stk *s;

  while (vm.sp > 0 && vm.stk[vm.sp-1].kind != S_DO) pop_stk();
  if (vm.sp == 0) shk_error("LOOP without DO");
  s = &vm.stk[vm.sp-1];

  if (accept_kw(KW_WHILE)) cond_kind = 1;
  else if (accept_kw(KW_UNTIL)) cond_kind = 2;

  if (cond_kind) {
    Value c;
    int t;
    val_init(&c);
    eval_expr(&c);
    t = val_truth(&c);
    val_free(&c);
    if ((cond_kind == 1 && !t) || (cond_kind == 2 && t)) { pop_stk(); return; }
  }
  pop_stk();
  vm.pc = s->pc;        /* back to DO, which re-tests its own condition */
  (void)looppc;
}

static void do_while(void)
{
  int wpc = vm.pc - 1;
  Value c;
  int t;
  val_init(&c);
  eval_expr(&c);
  t = val_truth(&c);
  val_free(&c);
  if (!t) {
    vm.pc = vm.tok[wpc].link + 1;    /* past WEND */
    skip_stmt();
    return;
  }
  {
    Stk *s = push_stk(S_WHILE);
    s->pc = wpc;
    s->exitpc = vm.tok[wpc].link;
  }
}

static void do_wend(void)
{
  Stk *s;
  while (vm.sp > 0 && vm.stk[vm.sp-1].kind != S_WHILE) pop_stk();
  if (vm.sp == 0) shk_error("WEND without WHILE");
  s = &vm.stk[vm.sp-1];
  vm.pc = s->pc;
  pop_stk();
}

/* ------------------------------------------------------------------ */
/* SELECT CASE                                                         */
/* ------------------------------------------------------------------ */
static int values_equal(const Value *a, const Value *b)
{
  if (a->type == V_STR || b->type == V_STR) {
    if (a->type != V_STR || b->type != V_STR) shk_error("Cannot compare a string with a number");
    return strcmp(a->s ? a->s : "", b->s ? b->s : "") == 0;
  }
  if (a->type == V_INT && b->type == V_INT) return a->i == b->i;
  return val_f(a) == val_f(b);
}

static int value_cmp(const Value *a, const Value *b)
{
  if (a->type == V_STR || b->type == V_STR) {
    int r;
    if (a->type != V_STR || b->type != V_STR) shk_error("Cannot compare a string with a number");
    r = strcmp(a->s ? a->s : "", b->s ? b->s : "");
    return r < 0 ? -1 : (r > 0 ? 1 : 0);
  }
  {
    double x = val_f(a), y = val_f(b);
    return x < y ? -1 : (x > y ? 1 : 0);
  }
}

static void do_select(void)
{
  int spc = vm.pc - 1;
  Stk *s;
  accept_kw(KW_CASE);
  s = push_stk(S_SELECT);
  val_init(&s->sel);
  eval_expr(&s->sel);
  s->matched = 0;
  s->exitpc = vm.tok[spc].link2;
  vm.pc = vm.tok[spc].link;          /* first CASE */
}

static void do_case(void)
{
  int cpc = vm.pc - 1;
  Stk *s;
  int hit = 0;

  if (vm.sp == 0 || vm.stk[vm.sp-1].kind != S_SELECT) shk_error("CASE without SELECT");
  s = &vm.stk[vm.sp-1];

  if (s->matched) {                  /* falling out of a taken branch */
    vm.pc = vm.tok[cpc].link2;
    return;
  }

  if (accept_kw(KW_ELSE)) {
    hit = 1;
    s->matched = 1;
    return;
  }

  for (;;) {
    Value a;
    val_init(&a);
    if (at_op('<') || at_op('>') || at_op('=') || at_op(OP_LE) ||
        at_op(OP_GE) || at_op(OP_NE)) {
      /* CASE IS > x  (IS is optional in this dialect) */
      int op = cur()->op;
      advance();
      eval_expr(&a);
      {
        int r = value_cmp(&s->sel, &a);
        switch (op) {
          case '=':   hit |= (r == 0); break;
          case OP_NE: hit |= (r != 0); break;
          case '<':   hit |= (r <  0); break;
          case '>':   hit |= (r >  0); break;
          case OP_LE: hit |= (r <= 0); break;
          case OP_GE: hit |= (r >= 0); break;
        }
      }
    } else if (cur()->type == T_IDENT && !strcasecmp(cur()->str, "IS")) {
      advance();
      continue;
    } else {
      eval_expr(&a);
      if (accept_kw(KW_TO)) {
        Value b;
        val_init(&b);
        eval_expr(&b);
        hit |= (value_cmp(&s->sel, &a) >= 0 && value_cmp(&s->sel, &b) <= 0);
        val_free(&b);
      } else {
        hit |= values_equal(&s->sel, &a);
      }
    }
    val_free(&a);
    if (accept_op(',')) continue;
    break;
  }

  if (hit) { s->matched = 1; return; }
  vm.pc = vm.tok[cpc].link;      /* next CASE or END SELECT */
}

/* ------------------------------------------------------------------ */
/* procedures                                                          */
/* ------------------------------------------------------------------ */
typedef struct {
  int   byref;
  Var  *src;        /* byref source                                   */
  Value val;        /* byval value                                    */
} ArgSlot;

void call_proc(Proc *p, Value *result)
{
  ArgSlot slot[MAXPARAM];
  int nargs = 0;
  int i;
  int parens;
  Frame *fr;
  int fidx;
  int ret_pc;

  for (i = 0; i < MAXPARAM; i++) { slot[i].byref = 0; slot[i].src = NULL; val_init(&slot[i].val); }

  /* ---- collect arguments in the caller's scope ------------------- */
  parens = accept_op('(');
  if (!(parens && at_op(')')) && !(!parens && at_end_of_stmt())) {
    for (;;) {
      if (nargs >= MAXPARAM) shk_error("Too many arguments to %s", p->name);
      if (at_op(',')) { nargs++; goto next_arg; }   /* omitted argument */

      /* An argument that is nothing but a variable name is passed by
       * reference; anything else is an expression, passed by value.   */
      if (cur()->type == T_IDENT) {
        Var *v = var_find(cur()->str);
        Tok *nx = peek(1);
        int ends_at = -1;
        if (v) {
          int arr = (nx->type == T_OP && nx->op == '(') &&
                    (peek(2)->type == T_OP && peek(2)->op == ')');
          int after = arr ? 3 : 1;
          Tok *t2 = peek(after);
          int done = (t2->type == T_EOL || t2->type == T_EOF ||
                      t2->type == T_COLON ||
                      (t2->type == T_OP && (t2->op == ',' || t2->op == ')')) ||
                      (t2->type == T_KW && t2->kw == KW_ELSE));
          if (done) ends_at = after;
        }
        if (ends_at > 0) {
          int k;
          slot[nargs].byref = 1;
          slot[nargs].src = var_deref(v);
          for (k = 0; k < ends_at; k++) advance();
          nargs++;
          goto next_arg;
        }
      }
      eval_expr(&slot[nargs].val);
      nargs++;
next_arg:
      if (accept_op(',')) continue;
      break;
    }
  }
  if (parens) expect_op(')');
  if (nargs > p->nparam)
    shk_error("%s takes %d argument(s), got %d", p->name, p->nparam, nargs);

  /* ---- build the frame -------------------------------------------- */
  if (vm.ncall >= MAXCALL) shk_error("Too many nested calls (recursion too deep)");
  ret_pc = vm.pc;
  fidx = vm.ncall;
  fr = &vm.call[fidx];
  memset(fr, 0, sizeof *fr);
  fr->ret = ret_pc;
  fr->proc = p;
  fr->sp = vm.sp;
  val_init(&fr->retval);
  vm.ncall++;

  for (i = 0; i < p->nparam; i++) {
    Var *pv;
    if (i < nargs && slot[i].byref) {
      pv = var_create(&fr->locals, p->pname[i], slot[i].src->type);
      pv->flags |= VF_ALIAS;
      pv->alias = slot[i].src;
    } else {
      pv = var_create(&fr->locals, p->pname[i], p->ptype[i]);
      if (i < nargs && slot[i].val.type) {
        if (p->ptype[i] == V_STR && slot[i].val.type != V_STR)
          shk_error("Parameter %s of %s expects a string", p->pname[i], p->name);
        if (p->ptype[i] != V_STR && slot[i].val.type == V_STR)
          shk_error("Parameter %s of %s expects a number", p->pname[i], p->name);
        var_set(pv, -1, &slot[i].val);
      }
    }
  }
  for (i = 0; i < MAXPARAM; i++) val_free(&slot[i].val);

  /* the function's own name holds its return value */
  if (p->is_func) var_create(&fr->locals, p->name, p->rettype);

  /* ---- run the body ----------------------------------------------- */
  vm.pc = p->pc;
  while (vm.ncall > fidx) {
    if (vm.stopped) break;
    if (vm.pc >= vm.ntok || cur()->type == T_EOF)
      shk_error("Missing END %s in %s", p->is_func ? "FUNCTION" : "SUB", p->name);
    exec_statement();
  }

  if (result) {
    val_copy(result, &vm.call[fidx].retval);
    if (p->is_func && result->type == 0) val_setf(result, 0);
  }
  val_free(&vm.call[fidx].retval);
  vt_clear(&vm.call[fidx].locals);
}

static void do_end_proc(void)
{
  Frame *fr;
  int fidx = vm.ncall - 1;
  if (fidx < 0) shk_error("END SUB/FUNCTION without a call");
  fr = &vm.call[fidx];
  if (fr->proc && fr->proc->is_func) {
    Var *rv = var_find_in(&fr->locals, fr->proc->name);
    if (rv) var_get(rv, -1, &fr->retval);
  }
  while (vm.sp > fr->sp) pop_stk();
  vm.pc = fr->ret;
  vm.ncall--;
}

/* ------------------------------------------------------------------ */
/* graphics statements                                                 */
/* ------------------------------------------------------------------ */
static void gfx_require(void)
{
  if (!gfx_available()) gfx_init(1);
}

static void do_graphics(int kw)
{
  CmdArgs c;
  gfx_require();
  switch (kw) {
    case KW_CLS: {
      cmd_read(&c, 1);
      gfx_cls((uint32_t)cmd_int(&c, 0, (int64_t)cur_bg));
      cmd_free(&c);
      con_set_col(0);
      break;
    }
    case KW_PIXEL: {
      cmd_read(&c, 3);
      gfx_pixel((int)cmd_int(&c,0,0), (int)cmd_int(&c,1,0),
                (uint32_t)cmd_int(&c,2,(int64_t)cur_fg));
      cmd_free(&c);
      break;
    }
    case KW_LINE: {
      int x0,y0,x1,y1,lw;
      uint32_t col;
      cmd_read(&c, 6);
      x0=(int)cmd_int(&c,0,0); y0=(int)cmd_int(&c,1,0);
      x1=(int)cmd_int(&c,2,0); y1=(int)cmd_int(&c,3,0);
      lw=(int)cmd_int(&c,4,1);
      col=(uint32_t)cmd_int(&c,5,(int64_t)cur_fg);
      if (lw < 1) lw = 1;
      {
        int i;
        for (i = 0; i < lw; i++) {
          if (abs(x1-x0) >= abs(y1-y0)) gfx_line(x0, y0+i, x1, y1+i, col);
          else gfx_line(x0+i, y0, x1+i, y1, col);
        }
      }
      cmd_free(&c);
      break;
    }
    case KW_BOX: {
      int x,y,w,h,lw;
      uint32_t col, fill;
      int hasfill;
      cmd_read(&c, 7);
      x=(int)cmd_int(&c,0,0); y=(int)cmd_int(&c,1,0);
      w=(int)cmd_int(&c,2,0); h=(int)cmd_int(&c,3,0);
      lw=(int)cmd_int(&c,4,1);
      col=(uint32_t)cmd_int(&c,5,(int64_t)cur_fg);
      hasfill = cmd_has(&c,6);
      fill=(uint32_t)cmd_int(&c,6,(int64_t)cur_bg);
      gfx_box(x,y,w,h,hasfill,col,fill);
      if (lw > 1) {
        int i;
        for (i = 1; i < lw; i++) gfx_box(x+i,y+i,w-2*i,h-2*i,0,col,fill);
      }
      cmd_free(&c);
      break;
    }
    case KW_RBOX: {
      int x,y,w,h,r,hasfill;
      uint32_t col, fill;
      cmd_read(&c, 7);
      x=(int)cmd_int(&c,0,0); y=(int)cmd_int(&c,1,0);
      w=(int)cmd_int(&c,2,0); h=(int)cmd_int(&c,3,0);
      r=(int)cmd_int(&c,4,5);
      col=(uint32_t)cmd_int(&c,5,(int64_t)cur_fg);
      hasfill = cmd_has(&c,6);
      fill=(uint32_t)cmd_int(&c,6,(int64_t)cur_bg);
      gfx_rbox(x,y,w,h,r,hasfill,col,fill);
      cmd_free(&c);
      break;
    }
    case KW_CIRCLE: {
      int x,y,r,lw,hasfill;
      double aspect;
      uint32_t col, fill;
      cmd_read(&c, 7);
      x=(int)cmd_int(&c,0,0); y=(int)cmd_int(&c,1,0); r=(int)cmd_int(&c,2,1);
      lw=(int)cmd_int(&c,3,1);
      aspect=cmd_num(&c,4,1.0);
      col=(uint32_t)cmd_int(&c,5,(int64_t)cur_fg);
      hasfill = cmd_has(&c,6);
      fill=(uint32_t)cmd_int(&c,6,(int64_t)cur_bg);
      gfx_circle(x,y,r,hasfill,col,fill,aspect);
      if (lw > 1) {
        int i;
        for (i = 1; i < lw && r - i > 0; i++) gfx_circle(x,y,r-i,0,col,fill,aspect);
      }
      cmd_free(&c);
      break;
    }
    case KW_ARC: {
      cmd_read(&c, 7);
      gfx_arc((int)cmd_int(&c,0,0), (int)cmd_int(&c,1,0),
              (int)cmd_int(&c,2,0), (int)cmd_int(&c,3,0),
              cmd_num(&c,4,0), cmd_num(&c,5,360),
              (uint32_t)cmd_int(&c,6,(int64_t)cur_fg));
      cmd_free(&c);
      break;
    }
    case KW_TRIANGLE: {
      int hasfill;
      cmd_read(&c, 8);
      hasfill = cmd_has(&c,7);
      gfx_triangle((int)cmd_int(&c,0,0),(int)cmd_int(&c,1,0),
                   (int)cmd_int(&c,2,0),(int)cmd_int(&c,3,0),
                   (int)cmd_int(&c,4,0),(int)cmd_int(&c,5,0),
                   hasfill,
                   (uint32_t)cmd_int(&c,6,(int64_t)cur_fg),
                   (uint32_t)cmd_int(&c,7,(int64_t)cur_bg));
      cmd_free(&c);
      break;
    }
    case KW_TEXT: {
      int x, y;
      const char *s;
      const char *align = "LT";
      int scale;
      uint32_t fg, bg;
      cmd_read(&c, 8);
      x=(int)cmd_int(&c,0,0); y=(int)cmd_int(&c,1,0);
      s = cmd_has(&c,2) ? (c.v[2].type == V_STR ? val_s(&c.v[2]) : "") : "";
      if (cmd_has(&c,2) && c.v[2].type != V_STR) {
        char nb[64];
        num_to_str(&c.v[2], nb, sizeof nb);
        s = nb;
        if (cmd_has(&c,3) && c.v[3].type == V_STR) align = val_s(&c.v[3]);
        scale = (int)cmd_int(&c,5,cur_font_scale);
        fg = (uint32_t)cmd_int(&c,6,(int64_t)cur_fg);
        bg = (uint32_t)cmd_int(&c,7,(int64_t)cur_bg);
        gfx_text(x,y,s,align,scale,fg,bg);
        cmd_free(&c);
        break;
      }
      if (cmd_has(&c,3) && c.v[3].type == V_STR) align = val_s(&c.v[3]);
      scale = (int)cmd_int(&c,5,cur_font_scale);
      fg = (uint32_t)cmd_int(&c,6,(int64_t)cur_fg);
      bg = (uint32_t)cmd_int(&c,7,(int64_t)cur_bg);
      gfx_text(x,y,s,align,scale,fg,bg);
      cmd_free(&c);
      break;
    }
    case KW_COLOUR:
    case KW_COLOR: {
      int had_paren = accept_op('(');
      cmd_read(&c, 2);
      if (cmd_has(&c,0)) cur_fg = (uint32_t)cmd_int(&c,0,(int64_t)cur_fg);
      if (cmd_has(&c,1)) cur_bg = (uint32_t)cmd_int(&c,1,(int64_t)cur_bg);
      cmd_free(&c);
      if (had_paren) accept_op(')');
      break;
    }
    case KW_FONT: {
      cmd_read(&c, 2);
      cur_font_scale = (int)cmd_int(&c,1,1);
      if (cur_font_scale < 1) cur_font_scale = 1;
      if (cur_font_scale > 8) cur_font_scale = 8;
      cmd_free(&c);
      break;
    }
    case KW_REFRESH:
      gfx_present();
      skip_stmt();
      break;
  }
}

/* ------------------------------------------------------------------ */
/* files                                                               */
/* ------------------------------------------------------------------ */
static void do_open(void)
{
  Value path;
  int mode = 0;     /* 0 input, 1 output, 2 append, 3 random */
  int h;

  val_init(&path);
  eval_expect_str(&path);
  expect_kw(KW_FOR);
  if (accept_kw(KW_INPUT)) mode = 0;
  else if (accept_kw(KW_OUTPUT)) mode = 1;
  else if (accept_kw(KW_APPEND)) mode = 2;
  else if (accept_kw(KW_RANDOM)) mode = 3;
  else { val_free(&path); shk_error("Expected INPUT, OUTPUT, APPEND or RANDOM"); }
  expect_kw(KW_AS);
  accept_op('#');
  h = (int)eval_expect_int();
  if (h < 1 || h >= MAXFILE) { val_free(&path); shk_error("File number must be 1 to %d", MAXFILE-1); }
  if (vm.fh[h]) { val_free(&path); shk_error("File #%d is already open", h); }
  vm.fh[h] = fopen(val_s(&path),
                   mode == 0 ? "rb" : mode == 1 ? "wb" : mode == 2 ? "ab" : "r+b");
  if (!vm.fh[h] && mode == 3) vm.fh[h] = fopen(val_s(&path), "w+b");
  if (!vm.fh[h]) {
    char msg[300];
    snprintf(msg, sizeof msg, "Cannot open %s", val_s(&path));
    val_free(&path);
    shk_error("%s", msg);
  }
  val_free(&path);
}

static void do_close(void)
{
  if (at_end_of_stmt()) {
    int i;
    for (i = 1; i < MAXFILE; i++) if (vm.fh[i]) { fclose(vm.fh[i]); vm.fh[i] = NULL; }
    return;
  }
  for (;;) {
    int h;
    accept_op('#');
    h = (int)eval_expect_int();
    if (h < 1 || h >= MAXFILE || !vm.fh[h]) shk_error("File #%d is not open", h);
    fclose(vm.fh[h]);
    vm.fh[h] = NULL;
    if (accept_op(',')) continue;
    break;
  }
}

/* ------------------------------------------------------------------ */
/* DATA / READ                                                         */
/* ------------------------------------------------------------------ */
static void do_read(void)
{
  for (;;) {
    LValue lv = parse_lvalue();
    int save_pc;
    Value v;
    if (vm.datap >= vm.ndata) shk_error("Out of DATA");
    save_pc = vm.pc;
    vm.pc = vm.data[vm.datap++];
    val_init(&v);
    eval_expr(&v);
    if (lv.v->type == V_STR && v.type != V_STR) {
      char buf[64];
      num_to_str(&v, buf, sizeof buf);
      val_sets(&v, buf);
    } else if (lv.v->type != V_STR && v.type == V_STR) {
      val_free(&v);
      vm.pc = save_pc;
      shk_error("DATA item is a string but %s is numeric", lv.v->name);
    }
    var_set(lv.v, lv.idx, &v);
    val_free(&v);
    vm.pc = save_pc;
    if (accept_op(',')) continue;
    break;
  }
}

static void do_restore(void)
{
  if (at_end_of_stmt()) { vm.datap = 0; return; }
  /* RESTORE label / line number: find the first DATA item at or after it */
  {
    int target = -1;
    int i;
    if (cur()->type == T_INT) { target = prog_find_line((int32_t)cur()->inum); advance(); }
    else if (cur()->type == T_IDENT) { target = prog_find_label(cur()->str); advance(); }
    else shk_error("RESTORE expects a line number or label");
    if (target < 0) shk_error("RESTORE target not found");
    vm.datap = vm.ndata;
    for (i = 0; i < vm.ndata; i++)
      if (vm.data[i] >= target) { vm.datap = i; break; }
  }
}

/* ------------------------------------------------------------------ */
/* OPTION                                                              */
/* ------------------------------------------------------------------ */
static void do_option(void)
{
  if (accept_kw(KW_BASE)) {
    int64_t b = eval_expect_int();
    if (b != 0 && b != 1) shk_error("OPTION BASE must be 0 or 1");
    vm.opt_base = (int)b;
    return;
  }
  if (accept_kw(KW_EXPLICIT)) {
    vm.opt_explicit = 1;
    if (cur()->type == T_IDENT && !strcasecmp(cur()->str, "OFF")) { vm.opt_explicit = 0; advance(); }
    return;
  }
  if (accept_kw(KW_DEFAULT)) {
    if (accept_kw(KW_INTEGER))      vm.opt_default = V_INT;
    else if (accept_kw(KW_FLOAT))   vm.opt_default = V_FLOAT;
    else if (accept_kw(KW_STRINGT)) vm.opt_default = V_STR;
    else if (accept_kw(KW_NONE))    vm.opt_default = 0;
    else shk_error("Expected INTEGER, FLOAT, STRING or NONE");
    return;
  }
  if (accept_kw(KW_ANGLE)) {
    if (accept_kw(KW_DEGREES)) vm.opt_degrees = 1;
    else if (accept_kw(KW_RADIANS)) vm.opt_degrees = 0;
    else shk_error("Expected DEGREES or RADIANS");
    return;
  }
  if (accept_kw(KW_ERROR)) {
    if (cur()->type == T_IDENT && !strcasecmp(cur()->str, "SKIP")) { vm.on_error_skip = 1; advance(); }
    else if (cur()->type == T_IDENT && !strcasecmp(cur()->str, "ABORT")) { vm.on_error_skip = 0; advance(); }
    else vm.on_error_skip = 1;
    return;
  }
  /* unknown options are ignored so that MMBasic sources still load */
  skip_stmt();
}

/* ------------------------------------------------------------------ */
/* EXIT / CONTINUE                                                     */
/* ------------------------------------------------------------------ */
static void do_exit(void)
{
  if (accept_kw(KW_FOR)) {
    while (vm.sp > 0 && vm.stk[vm.sp-1].kind != S_FOR) pop_stk();
    if (vm.sp == 0) shk_error("EXIT FOR outside a FOR loop");
    vm.pc = vm.stk[vm.sp-1].exitpc + 1;
    pop_stk();
    skip_stmt();
    return;
  }
  if (accept_kw(KW_DO)) {
    while (vm.sp > 0 && vm.stk[vm.sp-1].kind != S_DO) pop_stk();
    if (vm.sp == 0) shk_error("EXIT DO outside a DO loop");
    vm.pc = vm.stk[vm.sp-1].exitpc + 1;
    pop_stk();
    skip_stmt();
    return;
  }
  if (accept_kw(KW_WHILE)) {
    while (vm.sp > 0 && vm.stk[vm.sp-1].kind != S_WHILE) pop_stk();
    if (vm.sp == 0) shk_error("EXIT WHILE outside a WHILE loop");
    vm.pc = vm.stk[vm.sp-1].exitpc + 1;
    pop_stk();
    skip_stmt();
    return;
  }
  if (accept_kw(KW_SUB) || accept_kw(KW_FUNCTION)) { do_end_proc(); return; }
  /* bare EXIT exits the innermost loop */
  while (vm.sp > 0) {
    int k = vm.stk[vm.sp-1].kind;
    if (k == S_FOR || k == S_DO || k == S_WHILE) {
      vm.pc = vm.stk[vm.sp-1].exitpc + 1;
      pop_stk();
      skip_stmt();
      return;
    }
    pop_stk();
  }
  shk_error("EXIT outside a loop");
}

static void do_continue(void)
{
  int want = 0;
  if (accept_kw(KW_FOR)) want = S_FOR;
  else if (accept_kw(KW_DO)) want = S_DO;
  else if (accept_kw(KW_WHILE)) want = S_WHILE;

  while (vm.sp > 0) {
    int k = vm.stk[vm.sp-1].kind;
    if ((want && k == want) || (!want && (k == S_FOR || k == S_DO || k == S_WHILE))) {
      vm.pc = vm.stk[vm.sp-1].exitpc;   /* the NEXT / LOOP / WEND token */
      return;
    }
    pop_stk();
  }
  shk_error("CONTINUE outside a loop");
}

/* ------------------------------------------------------------------ */
/* IF                                                                  */
/* ------------------------------------------------------------------ */
static void do_if(int ifpc)
{
  Value c;
  int truth;
  int isblock;

  val_init(&c);
  eval_expr(&c);
  truth = val_truth(&c);
  val_free(&c);

  isblock = (vm.tok[ifpc].link2 >= 0);

  if (at_kw(KW_THEN)) advance();

  if (isblock) {
    if (truth) return;                    /* fall into the body */
    vm.pc = vm.tok[ifpc].link;            /* next ELSEIF / ELSE / ENDIF */
    if (at_kw(KW_ELSEIF)) { int p = vm.pc; advance(); do_if(p); return; }
    if (at_kw(KW_ELSE)) { advance(); if (at_kw(KW_IF)) { int p = vm.pc - 1; advance(); do_if(p); return; } return; }
    if (at_kw(KW_ENDIF)) { advance(); return; }
    if (at_kw(KW_END)) { advance(); accept_kw(KW_IF); return; }
    return;
  }

  /* single line: IF c THEN stmt [ELSE stmt] */
  if (truth) {
    /* IF c THEN 100  - a bare line number means GOTO */
    if (cur()->type == T_INT) {
      int t = prog_find_line((int32_t)cur()->inum);
      if (t < 0) shk_error("Line %lld not found", (long long)cur()->inum);
      vm.pc = t;
      return;
    }
    return;     /* execute the rest of the line normally */
  }
  /* skip to ELSE on this line, or to the end of the line */
  {
    int depth = 0;
    while (cur()->type != T_EOL && cur()->type != T_EOF) {
      if (at_kw(KW_IF)) depth++;
      else if (at_kw(KW_ELSE) && depth == 0) {
        advance();
        if (cur()->type == T_INT) {
          int t = prog_find_line((int32_t)cur()->inum);
          if (t < 0) shk_error("Line %lld not found", (long long)cur()->inum);
          vm.pc = t;
        }
        return;
      }
      else if (at_kw(KW_ELSE)) depth--;
      advance();
    }
  }
}

/* ------------------------------------------------------------------ */
/* statement dispatch                                                  */
/* ------------------------------------------------------------------ */
void exec_statement(void)
{
  Tok *t = cur();

  switch (t->type) {
    case T_EOF:
      vm.stopped = 1;
      return;
    case T_EOL:
    case T_COLON:
    case T_LINENO:
    case T_LABEL:
      advance();
      return;
    default:
      break;
  }

  if (t->type == T_KW) {
    int kw = t->kw;
    int here = vm.pc;
    advance();

    switch (kw) {
      case KW_PRINT:    do_print(); return;
      case KW_INPUT:    do_input(0); return;
      case KW_LINE:
        if (at_kw(KW_INPUT)) { advance(); do_input(1); return; }
        do_graphics(KW_LINE);
        return;
      case KW_LET: {
        LValue lv = parse_lvalue();
        Value v;
        expect_op('=');
        val_init(&v);
        eval_expr(&v);
        var_set(lv.v, lv.idx, &v);
        val_free(&v);
        return;
      }
      case KW_DIM:      do_dim(0); return;
      case KW_LOCAL:    do_dim(1); return;
      case KW_STATIC:   do_dim(0); return;
      case KW_CONST:    do_const(); return;

      case KW_IF:       do_if(here); return;
      case KW_ELSEIF:
      case KW_ELSE:
        /* reached by falling out of a taken branch */
        if (vm.tok[here].link2 >= 0) vm.pc = vm.tok[here].link2;
        else skip_to_eol();            /* single-line IF ... ELSE ... */
        return;
      case KW_ENDIF:    return;
      case KW_THEN:     return;

      case KW_FOR:      do_for(); return;
      case KW_NEXT:     do_next(); return;
      case KW_DO:       do_do(); return;
      case KW_LOOP:     do_loop(); return;
      case KW_WHILE:    do_while(); return;
      case KW_WEND:     do_wend(); return;
      case KW_SELECT:   do_select(); return;
      case KW_CASE:     do_case(); return;

      case KW_EXIT:     do_exit(); return;
      case KW_CONTINUE: do_continue(); return;

      case KW_GOTO: {
        int target;
        if (cur()->type == T_INT) { target = prog_find_line((int32_t)cur()->inum); advance(); }
        else if (cur()->type == T_IDENT) { target = prog_find_label(cur()->str); advance(); }
        else shk_error("GOTO expects a line number or label");
        if (target < 0) shk_error("GOTO target not found");
        vm.pc = target;
        return;
      }
      case KW_GOSUB: {
        int target;
        Stk *s;
        if (cur()->type == T_INT) { target = prog_find_line((int32_t)cur()->inum); advance(); }
        else if (cur()->type == T_IDENT) { target = prog_find_label(cur()->str); advance(); }
        else shk_error("GOSUB expects a line number or label");
        if (target < 0) shk_error("GOSUB target not found");
        s = push_stk(S_GOSUB);
        s->pc = vm.pc;
        vm.pc = target;
        return;
      }
      case KW_RETURN: {
        if (vm.ncall > 0 && vm.sp == vm.call[vm.ncall-1].sp) {
          /* RETURN value inside a FUNCTION */
          Frame *fr = &vm.call[vm.ncall-1];
          if (fr->proc && fr->proc->is_func && !at_end_of_stmt()) {
            Value v;
            val_init(&v);
            eval_expr(&v);
            val_copy(&fr->retval, &v);
            val_free(&v);
            while (vm.sp > fr->sp) pop_stk();
            vm.pc = fr->ret;
            vm.ncall--;
            return;
          }
        }
        while (vm.sp > 0 && vm.stk[vm.sp-1].kind != S_GOSUB) pop_stk();
        if (vm.sp == 0) shk_error("RETURN without GOSUB");
        vm.pc = vm.stk[vm.sp-1].pc;
        pop_stk();
        return;
      }
      case KW_ON: {
        /* ON n GOTO a,b,c  |  ON n GOSUB a,b,c  |  ON ERROR ... */
        if (accept_kw(KW_ERROR)) {
          if (cur()->type == T_IDENT && !strcasecmp(cur()->str, "IGNORE")) { vm.on_error_skip = 1; advance(); }
          else if (cur()->type == T_IDENT && !strcasecmp(cur()->str, "ABORT")) { vm.on_error_skip = 0; advance(); }
          else if (cur()->type == T_IDENT && !strcasecmp(cur()->str, "SKIP")) { vm.on_error_skip = 1; advance(); }
          else skip_stmt();
          return;
        }
        {
          int64_t n = eval_expect_int();
          int isgosub;
          int targets[64];
          int ntarget = 0;
          if (accept_kw(KW_GOTO)) isgosub = 0;
          else if (accept_kw(KW_GOSUB)) isgosub = 1;
          else { shk_error("Expected GOTO or GOSUB after ON"); return; }
          for (;;) {
            int tgt;
            if (cur()->type == T_INT) { tgt = prog_find_line((int32_t)cur()->inum); advance(); }
            else if (cur()->type == T_IDENT) { tgt = prog_find_label(cur()->str); advance(); }
            else shk_error("Expected a line number or label");
            if (ntarget < 64) targets[ntarget++] = tgt;
            if (accept_op(',')) continue;
            break;
          }
          if (n >= 1 && n <= ntarget) {
            int tgt = targets[n-1];
            if (tgt < 0) shk_error("ON target not found");
            if (isgosub) {
              Stk *s = push_stk(S_GOSUB);
              s->pc = vm.pc;
            }
            vm.pc = tgt;
          }
          return;
        }
      }

      case KW_SUB:
      case KW_FUNCTION:
        /* definition reached while running: skip the whole body */
        vm.pc = vm.tok[here].link;
        advance();
        skip_stmt();
        return;

      case KW_END:
        if (at_kw(KW_SUB) || at_kw(KW_FUNCTION)) { advance(); do_end_proc(); return; }
        if (at_kw(KW_IF)) { advance(); return; }
        if (at_kw(KW_SELECT)) { advance(); pop_stk(); return; }
        vm.stopped = 1;
        return;

      case KW_STOP:
        vm.stopped = 1;
        return;

      case KW_CALL: {
        Value nv;
        Proc *p;
        val_init(&nv);
        if (cur()->type == T_IDENT) {
          p = prog_find_proc(cur()->str);
          if (!p) shk_error("SUB %s is not defined", cur()->str);
          advance();
        } else {
          eval_expect_str(&nv);
          p = prog_find_proc(val_s(&nv));
          if (!p) { char m[128]; snprintf(m,sizeof m,"%s", val_s(&nv)); val_free(&nv); shk_error("SUB %s is not defined", m); return; }
        }
        val_free(&nv);
        accept_op(',');
        call_proc(p, NULL);
        return;
      }

      case KW_DATA:     skip_to_eol(); return;
      case KW_READ:     do_read(); return;
      case KW_RESTORE:  do_restore(); return;

      case KW_RANDOMIZE: {
        if (at_end_of_stmt()) srand((unsigned)time(NULL));
        else srand((unsigned)eval_expect_int());
        return;
      }

      case KW_PAUSE: {
        double ms;
        eval_expect_num(&ms);
        con_pause_ms(ms);
        return;
      }

      case KW_TIMER:
        if (accept_op('=')) { (void)eval_expect_int(); con_timer_reset(); }
        return;

      case KW_INC: {
        LValue lv = parse_lvalue();
        Value cv, nv;
        val_init(&cv); val_init(&nv);
        var_get(lv.v, lv.idx, &cv);
        if (accept_op(',')) {
          Value d;
          val_init(&d);
          eval_expr(&d);
          if (cv.type == V_STR) {
            char *j = shk_alloc(strlen(cv.s?cv.s:"") + strlen(val_s(&d)) + 1);
            strcpy(j, cv.s?cv.s:""); strcat(j, val_s(&d));
            val_sets(&nv, j); free(j);
          } else if (cv.type == V_INT && d.type == V_INT) val_seti(&nv, cv.i + d.i);
          else val_setf(&nv, val_f(&cv) + val_f(&d));
          val_free(&d);
        } else {
          if (cv.type == V_INT) val_seti(&nv, cv.i + 1);
          else val_setf(&nv, val_f(&cv) + 1);
        }
        var_set(lv.v, lv.idx, &nv);
        val_free(&cv); val_free(&nv);
        return;
      }

      case KW_CLS: case KW_PIXEL: case KW_BOX: case KW_CIRCLE:
      case KW_RBOX: case KW_TEXT: case KW_COLOUR: case KW_COLOR:
      case KW_FONT: case KW_ARC: case KW_TRIANGLE: case KW_REFRESH:
        do_graphics(kw);
        return;

      case KW_SAVE: {
        Value p;
        if (!accept_kw(KW_IMAGE)) shk_error("Expected SAVE IMAGE");
        val_init(&p);
        eval_expect_str(&p);
        if (!gfx_save_bmp(val_s(&p))) { char m[300]; snprintf(m,sizeof m,"%s",val_s(&p)); val_free(&p); shk_error("Cannot write %s", m); }
        val_free(&p);
        return;
      }

      case KW_OPEN:     do_open(); return;
      case KW_CLOSE:    do_close(); return;
      case KW_SEEK: {
        int h;
        int64_t pos;
        accept_op('#');
        h = (int)eval_expect_int();
        expect_op(',');
        pos = eval_expect_int();
        if (h < 1 || h >= MAXFILE || !vm.fh[h]) shk_error("File #%d is not open", h);
        fseek(vm.fh[h], (long)(pos - 1), SEEK_SET);
        return;
      }
      case KW_KILL: {
        Value p;
        val_init(&p);
        eval_expect_str(&p);
        if (remove(val_s(&p)) != 0) { char m[300]; snprintf(m,sizeof m,"%s",val_s(&p)); val_free(&p); shk_error("Cannot delete %s", m); }
        val_free(&p);
        return;
      }
      case KW_MKDIR: case KW_RMDIR: case KW_CHDIR: {
        Value p;
        int r;
        val_init(&p);
        eval_expect_str(&p);
        if (kw == KW_MKDIR) r = mkdir_compat(val_s(&p));
        else if (kw == KW_RMDIR) r = rmdir(val_s(&p));
        else r = chdir(val_s(&p));
        if (r != 0) { char m[300]; snprintf(m,sizeof m,"%s",val_s(&p)); val_free(&p); shk_error("Directory operation failed on %s", m); }
        val_free(&p);
        return;
      }
      case KW_RENAME: {
        Value a, b;
        val_init(&a); val_init(&b);
        eval_expect_str(&a);
        if (!accept_kw(KW_AS)) expect_op(',');
        eval_expect_str(&b);
        if (rename(val_s(&a), val_s(&b)) != 0) { val_free(&a); val_free(&b); shk_error("Rename failed"); }
        val_free(&a); val_free(&b);
        return;
      }

      case KW_OPTION:   do_option(); return;

      case KW_ERROR: {
        Value m;
        val_init(&m);
        if (at_end_of_stmt()) shk_error("User error");
        eval_expect_str(&m);
        {
          char buf[256];
          snprintf(buf, sizeof buf, "%s", val_s(&m));
          val_free(&m);
          shk_error("%s", buf);
        }
        return;
      }

      default:
        shk_error("%s cannot be used here", kw_name[kw]);
    }
    return;
  }

  /* ---- identifier: assignment or SUB call ------------------------- */
  if (t->type == T_IDENT) {
    Proc *p = prog_find_proc(t->str);
    Var *v = var_find(t->str);

    if (p && !v) {
      if (p->is_func) {
        /* calling a FUNCTION as a statement: run it, discard the result */
        Value r;
        val_init(&r);
        advance();
        call_proc(p, &r);
        val_free(&r);
        return;
      }
      advance();
      call_proc(p, NULL);
      return;
    }
    {
      LValue lv = parse_lvalue();
      Value val;
      expect_op('=');
      val_init(&val);
      eval_expr(&val);
      var_set(lv.v, lv.idx, &val);
      val_free(&val);
      return;
    }
  }

  shk_error("Cannot understand %s", tok_text(t));
}

/* ------------------------------------------------------------------ */
void exec_run(void)
{
  vm.pc = 0;
  vm.stopped = 0;
  vm.err_code = 0;

  if (setjmp(vm.err_jmp)) {
    if (!vm.on_error_skip) { vm.err_code = 1; return; }
    /* OPTION ERROR SKIP: report, abandon any call in progress and
     * continue at the next line.                                   */
    fprintf(stderr, "shkbasic: line %d: %s (skipped)\n", vm.err_line, vm.err_msg);
    while (vm.ncall > 0) { vt_clear(&vm.call[vm.ncall-1].locals); vm.ncall--; }
    vm.sp = 0;
    skip_to_eol();
    if (cur()->type != T_EOF) advance();
  }

  while (!vm.stopped) {
    if (vm.pc >= vm.ntok) break;
    if (cur()->type == T_EOF) break;
    exec_statement();
  }
  gfx_present();
}
