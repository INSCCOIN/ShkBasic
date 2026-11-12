/* prog.c - program image: load, index and link
 *
 * The link pass resolves every structured keyword to a token index so
 * that execution never has to scan forward looking for a matching
 * ENDIF or NEXT.  It also builds the line/label directory, the SUB and
 * FUNCTION table, and the DATA item list.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "shk.h"

/* ------------------------------------------------------------------ */
static int tk_is(int i, int type)
{
  return i >= 0 && i < vm.ntok && vm.tok[i].type == type;
}

static int tk_kw(int i, int kw)
{
  return i >= 0 && i < vm.ntok && vm.tok[i].type == T_KW && vm.tok[i].kw == kw;
}

static int tk_op(int i, int op)
{
  return i >= 0 && i < vm.ntok && vm.tok[i].type == T_OP && vm.tok[i].op == op;
}

/* index of the next token that starts a statement, from i (inclusive) */
static int skip_sep(int i)
{
  while (i < vm.ntok &&
         (vm.tok[i].type == T_EOL || vm.tok[i].type == T_COLON ||
          vm.tok[i].type == T_LINENO || vm.tok[i].type == T_LABEL))
    i++;
  return i;
}

/* index of the T_EOL / depth-0 T_COLON that ends the statement at k */
static int stmt_end(int k)
{
  int depth = 0;
  while (k < vm.ntok && vm.tok[k].type != T_EOL && vm.tok[k].type != T_EOF) {
    if (tk_op(k, '(')) depth++;
    else if (tk_op(k, ')')) depth--;
    else if (depth == 0 && vm.tok[k].type == T_COLON) return k;
    k++;
  }
  return k;
}

/* index of the end of the physical line containing k */
static int line_end(int k)
{
  while (k < vm.ntok && vm.tok[k].type != T_EOL && vm.tok[k].type != T_EOF) k++;
  return k;
}

static int line_of(int i)
{
  if (i < 0) i = 0;
  if (i >= vm.ntok) i = vm.ntok - 1;
  return vm.tok[i].line;
}

/* ------------------------------------------------------------------ */
/* directory building                                                  */
/* ------------------------------------------------------------------ */
static void add_line(int32_t n, int pc)
{
  vm.lmap = shk_realloc(vm.lmap, (size_t)(vm.nlmap + 1) * sizeof *vm.lmap);
  vm.lmap[vm.nlmap].num = n;
  vm.lmap[vm.nlmap].pc = pc;
  vm.nlmap++;
}

static void add_label(const char *name, int pc)
{
  int i;
  vm.labels = shk_realloc(vm.labels, (size_t)(vm.nlabels + 1) * sizeof *vm.labels);
  for (i = 0; name[i] && i < MAXNAME - 1; i++)
    vm.labels[vm.nlabels].name[i] = (char)toupper((unsigned char)name[i]);
  vm.labels[vm.nlabels].name[i] = 0;
  vm.labels[vm.nlabels].pc = pc;
  vm.nlabels++;
}

int prog_find_line(int32_t n)
{
  int i;
  for (i = 0; i < vm.nlmap; i++) if (vm.lmap[i].num == n) return vm.lmap[i].pc;
  return -1;
}

int prog_find_label(const char *name)
{
  char key[MAXNAME];
  int i, j;
  for (j = 0; name[j] && j < MAXNAME - 1; j++)
    key[j] = (char)toupper((unsigned char)name[j]);
  key[j] = 0;
  for (i = 0; i < vm.nlabels; i++)
    if (!strcmp(vm.labels[i].name, key)) return vm.labels[i].pc;
  return -1;
}

Proc *prog_find_proc(const char *name)
{
  int i, j;
  char key[MAXNAME];
  for (j = 0; name[j] && j < MAXNAME - 1; j++)
    key[j] = (char)toupper((unsigned char)name[j]);
  key[j] = 0;
  for (i = 0; i < vm.nproc; i++)
    if (!strcmp(vm.proc[i].name, key)) return &vm.proc[i];
  return NULL;
}

/* ------------------------------------------------------------------ */
/* SUB / FUNCTION headers                                              */
/* ------------------------------------------------------------------ */
static int parse_proc_header(int i, Proc *p)
{
  int is_func = tk_kw(i, KW_FUNCTION);
  int j;
  memset(p, 0, sizeof *p);
  p->is_func = is_func;
  i++;
  if (!tk_is(i, T_IDENT)) {
    fprintf(stderr, "shkbasic: line %d: expected a name after %s\n",
            line_of(i), is_func ? "FUNCTION" : "SUB");
    exit(2);
  }
  for (j = 0; vm.tok[i].str[j] && j < MAXNAME - 1; j++)
    p->name[j] = (char)toupper((unsigned char)vm.tok[i].str[j]);
  p->name[j] = 0;
  p->rettype = (uint8_t)name_type(p->name, NULL);
  i++;

  {
    int parens = tk_op(i, '(');
    int have_list = parens || tk_is(i, T_IDENT);
    if (parens) i++;
    if (have_list && !(parens && tk_op(i, ')'))) {
      for (;;) {
        int t;
        if (p->nparam >= MAXPARAM) {
          fprintf(stderr, "shkbasic: line %d: too many parameters\n", line_of(i));
          exit(2);
        }
        if (!tk_is(i, T_IDENT)) {
          fprintf(stderr, "shkbasic: line %d: bad parameter list\n", line_of(i));
          exit(2);
        }
        for (j = 0; vm.tok[i].str[j] && j < MAXNAME - 1; j++)
          p->pname[p->nparam][j] = (char)toupper((unsigned char)vm.tok[i].str[j]);
        p->pname[p->nparam][j] = 0;
        t = name_type(p->pname[p->nparam], NULL);
        i++;
        if (tk_op(i, '(')) {               /* array parameter: a()    */
          p->parray[p->nparam] = 1;
          i++;
          while (i < vm.ntok && !tk_op(i, ')')) i++;
          if (tk_op(i, ')')) i++;
        }
        if (tk_kw(i, KW_AS)) {             /* AS INTEGER / FLOAT / STRING */
          i++;
          if (tk_kw(i, KW_INTEGER))      { t = V_INT;   i++; }
          else if (tk_kw(i, KW_FLOAT))   { t = V_FLOAT; i++; }
          else if (tk_kw(i, KW_STRINGT)) { t = V_STR;   i++; }
        }
        p->ptype[p->nparam] = (uint8_t)(t ? t : (vm.opt_default ? vm.opt_default : V_FLOAT));
        p->nparam++;
        if (tk_op(i, ',')) { i++; continue; }
        break;
      }
    }
    if (parens && tk_op(i, ')')) i++;
  }
  if (tk_kw(i, KW_AS)) {
    i++;
    if (tk_kw(i, KW_INTEGER))      { p->rettype = V_INT;   i++; }
    else if (tk_kw(i, KW_FLOAT))   { p->rettype = V_FLOAT; i++; }
    else if (tk_kw(i, KW_STRINGT)) { p->rettype = V_STR;   i++; }
  }
  if (!p->rettype) p->rettype = (uint8_t)(vm.opt_default ? vm.opt_default : V_FLOAT);
  /* body starts after the end of this line */
  while (i < vm.ntok && vm.tok[i].type != T_EOL) i++;
  p->pc = i + 1;
  return i;
}

/* ------------------------------------------------------------------ */
/* link pass                                                           */
/* ------------------------------------------------------------------ */
typedef struct {
  int kind;       /* KW_IF / KW_FOR / KW_DO / KW_WHILE / KW_SELECT     */
  int start;      /* token index of the opening keyword                */
  int prev_alt;   /* IF/SELECT: last branch token awaiting a link      */
  int nalt;
  int alt[64];    /* IF/SELECT: every branch token, for the end link   */
} Open;

static void link_fail(int i, const char *what)
{
  fprintf(stderr, "shkbasic: line %d: %s\n", line_of(i), what);
  exit(2);
}

void prog_link(void)
{
  Open st[MAXSTACK];
  int top = 0;
  int i = 0;
  int in_proc = -1;
  int proc_tok = -1;

  while (i < vm.ntok && vm.tok[i].type != T_EOF) {
    int s;

    /* record line numbers and labels wherever they appear */
    if (vm.tok[i].type == T_LINENO) { add_line((int32_t)vm.tok[i].inum, i + 1); i++; continue; }
    if (vm.tok[i].type == T_LABEL)  { add_label(vm.tok[i].str, i + 1); i++; continue; }
    if (vm.tok[i].type == T_EOL || vm.tok[i].type == T_COLON) { i++; continue; }

    s = i;   /* statement start */

    /* ---- SUB / FUNCTION ------------------------------------------ */
    if (tk_kw(s, KW_SUB) || tk_kw(s, KW_FUNCTION)) {
      Proc p;
      int end;
      if (in_proc >= 0) link_fail(s, "nested SUB/FUNCTION is not allowed");
      end = parse_proc_header(s, &p);
      vm.proc = shk_realloc(vm.proc, (size_t)(vm.nproc + 1) * sizeof(Proc));
      vm.proc[vm.nproc] = p;
      in_proc = vm.nproc;
      proc_tok = s;
      vm.nproc++;
      vm.tok[s].link = -1;        /* filled in at END SUB             */
      i = end;
      continue;
    }

    if (tk_kw(s, KW_END) &&
        (tk_kw(s+1, KW_SUB) || tk_kw(s+1, KW_FUNCTION))) {
      if (in_proc < 0 || proc_tok < 0) link_fail(s, "END SUB without SUB");
      vm.tok[proc_tok].link = s;
      vm.tok[s].link = s;
      in_proc = -1;
      proc_tok = -1;
      i = s + 2;
      continue;
    }

    /* ---- IF ------------------------------------------------------- */
    if (tk_kw(s, KW_IF)) {
      /* block IF only when THEN is the last token on the line */
      int k = s + 1, depth = 0, isblock = 0;
      while (k < vm.ntok && vm.tok[k].type != T_EOL) {
        if (tk_op(k, '(')) depth++;
        else if (tk_op(k, ')')) depth--;
        else if (depth == 0 && tk_kw(k, KW_THEN)) {
          if (vm.tok[k+1].type == T_EOL) isblock = 1;
          break;
        }
        k++;
      }
      if (!isblock && k < vm.ntok && vm.tok[k].type == T_EOL) {
        /* IF cond GOTO n  - also a single line form */
        isblock = 0;
      }
      if (isblock) {
        if (top >= MAXSTACK) link_fail(s, "constructs nested too deeply");
        memset(&st[top], 0, sizeof st[top]);
        st[top].kind = KW_IF;
        st[top].start = s;
        st[top].prev_alt = s;
        st[top].alt[st[top].nalt++] = s;
        top++;
        i = k + 1;
        continue;
      }
      i = line_end(s);
      continue;
    }

    if (tk_kw(s, KW_ELSEIF) || (tk_kw(s, KW_ELSE) && tk_kw(s+1, KW_IF))) {
      int tokidx = s;
      if (!top || st[top-1].kind != KW_IF) link_fail(s, "ELSEIF without IF");
      vm.tok[st[top-1].prev_alt].link = tokidx;
      st[top-1].prev_alt = tokidx;
      if (st[top-1].nalt < 64) st[top-1].alt[st[top-1].nalt++] = tokidx;
      i = line_end(s);
      continue;
    }

    if (tk_kw(s, KW_ELSE)) {
      if (!top || st[top-1].kind != KW_IF) link_fail(s, "ELSE without IF");
      vm.tok[st[top-1].prev_alt].link = s;
      st[top-1].prev_alt = s;
      if (st[top-1].nalt < 64) st[top-1].alt[st[top-1].nalt++] = s;
      i = line_end(s);
      continue;
    }

    if (tk_kw(s, KW_ENDIF) || (tk_kw(s, KW_END) && tk_kw(s+1, KW_IF))) {
      int n;
      if (!top || st[top-1].kind != KW_IF) link_fail(s, "ENDIF without IF");
      vm.tok[st[top-1].prev_alt].link = s;
      for (n = 0; n < st[top-1].nalt; n++) vm.tok[st[top-1].alt[n]].link2 = s;
      vm.tok[s].link = s;
      top--;
      i = s + (tk_kw(s, KW_ENDIF) ? 1 : 2);
      continue;
    }

    /* ---- FOR / NEXT ------------------------------------------------ */
    if (tk_kw(s, KW_FOR)) {
      if (top >= MAXSTACK) link_fail(s, "constructs nested too deeply");
      memset(&st[top], 0, sizeof st[top]);
      st[top].kind = KW_FOR;
      st[top].start = s;
      top++;
      i = stmt_end(s);
      continue;
    }
    if (tk_kw(s, KW_NEXT)) {
      if (!top || st[top-1].kind != KW_FOR) link_fail(s, "NEXT without FOR");
      vm.tok[st[top-1].start].link = s;
      vm.tok[s].link = st[top-1].start;
      top--;
      i = stmt_end(s);
      continue;
    }

    /* ---- DO / LOOP -------------------------------------------------- */
    if (tk_kw(s, KW_DO)) {
      if (top >= MAXSTACK) link_fail(s, "constructs nested too deeply");
      memset(&st[top], 0, sizeof st[top]);
      st[top].kind = KW_DO;
      st[top].start = s;
      top++;
      i = stmt_end(s);
      continue;
    }
    if (tk_kw(s, KW_LOOP)) {
      if (!top || st[top-1].kind != KW_DO) link_fail(s, "LOOP without DO");
      vm.tok[st[top-1].start].link = s;
      vm.tok[s].link = st[top-1].start;
      top--;
      i = stmt_end(s);
      continue;
    }

    /* ---- WHILE / WEND ----------------------------------------------- */
    if (tk_kw(s, KW_WHILE)) {
      /* WHILE belonging to DO...LOOP WHILE is consumed by DO/LOOP above */
      if (top && st[top-1].kind == KW_DO && st[top-1].start == s - 1) { i = s + 1; continue; }
      if (top >= MAXSTACK) link_fail(s, "constructs nested too deeply");
      memset(&st[top], 0, sizeof st[top]);
      st[top].kind = KW_WHILE;
      st[top].start = s;
      top++;
      i = stmt_end(s);
      continue;
    }
    if (tk_kw(s, KW_WEND)) {
      if (!top || st[top-1].kind != KW_WHILE) link_fail(s, "WEND without WHILE");
      vm.tok[st[top-1].start].link = s;
      vm.tok[s].link = st[top-1].start;
      top--;
      i = stmt_end(s);
      continue;
    }

    /* ---- SELECT CASE -------------------------------------------------- */
    if (tk_kw(s, KW_SELECT)) {
      if (top >= MAXSTACK) link_fail(s, "constructs nested too deeply");
      memset(&st[top], 0, sizeof st[top]);
      st[top].kind = KW_SELECT;
      st[top].start = s;
      st[top].prev_alt = s;
      st[top].alt[st[top].nalt++] = s;
      top++;
      i = stmt_end(s);
      continue;
    }
    if (tk_kw(s, KW_CASE)) {
      if (!top || st[top-1].kind != KW_SELECT) link_fail(s, "CASE without SELECT");
      vm.tok[st[top-1].prev_alt].link = s;
      st[top-1].prev_alt = s;
      if (st[top-1].nalt < 64) st[top-1].alt[st[top-1].nalt++] = s;
      i = stmt_end(s);
      continue;
    }
    if (tk_kw(s, KW_END) && tk_kw(s+1, KW_SELECT)) {
      int n;
      if (!top || st[top-1].kind != KW_SELECT) link_fail(s, "END SELECT without SELECT");
      vm.tok[st[top-1].prev_alt].link = s;
      for (n = 0; n < st[top-1].nalt; n++) vm.tok[st[top-1].alt[n]].link2 = s;
      vm.tok[s].link = s;
      top--;
      i = s + 2;
      continue;
    }

    /* ---- DATA --------------------------------------------------------- */
    if (tk_kw(s, KW_DATA)) {
      int k = s + 1;
      for (;;) {
        if (k >= vm.ntok || vm.tok[k].type == T_EOL) break;
        vm.data = shk_realloc(vm.data, (size_t)(vm.ndata + 1) * sizeof(int));
        vm.data[vm.ndata++] = k;
        /* advance past this item to the next comma at depth 0 */
        {
          int depth = 0;
          while (k < vm.ntok && vm.tok[k].type != T_EOL) {
            if (tk_op(k, '(')) depth++;
            else if (tk_op(k, ')')) depth--;
            else if (depth == 0 && tk_op(k, ',')) break;
            k++;
          }
        }
        if (k < vm.ntok && tk_op(k, ',')) { k++; continue; }
        break;
      }
      i = k;
      continue;
    }

    /* nothing structural: skip to the next statement separator */
    {
      int depth = 0;
      int k = s;
      while (k < vm.ntok && vm.tok[k].type != T_EOL) {
        if (tk_op(k, '(')) depth++;
        else if (tk_op(k, ')')) depth--;
        else if (depth == 0 && vm.tok[k].type == T_COLON) break;
        k++;
      }
      i = (k == s) ? s + 1 : k;
      continue;
    }
  }

  if (top) {
    const char *w = "block";
    switch (st[top-1].kind) {
      case KW_IF:     w = "IF without ENDIF"; break;
      case KW_FOR:    w = "FOR without NEXT"; break;
      case KW_DO:     w = "DO without LOOP"; break;
      case KW_WHILE:  w = "WHILE without WEND"; break;
      case KW_SELECT: w = "SELECT without END SELECT"; break;
    }
    link_fail(st[top-1].start, w);
  }
  if (in_proc >= 0)
    link_fail(vm.proc[in_proc].pc, "SUB/FUNCTION without END SUB/END FUNCTION");

  (void)skip_sep;
}

/* ------------------------------------------------------------------ */
void prog_load(const char *src)
{
  lex_source(src, &vm.tok, &vm.ntok);
  prog_link();
}

void prog_free(void)
{
  int i;
  for (i = 0; i < vm.ntok; i++) free(vm.tok[i].str);
  free(vm.tok);   vm.tok = NULL;   vm.ntok = 0;
  free(vm.lmap);  vm.lmap = NULL;  vm.nlmap = 0;
  free(vm.labels); vm.labels = NULL; vm.nlabels = 0;
  free(vm.proc);  vm.proc = NULL;  vm.nproc = 0;
  free(vm.data);  vm.data = NULL;  vm.ndata = 0;
}
