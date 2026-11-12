/* shkbasic - an MMBasic-compatible interpreter for the SharkDeck
 * Core declarations: tokens, values, variables, program image.
 *
 * Target: aarch64 Linux (Allwinner H618), 480x320 framebuffer.
 * Builds and runs on any POSIX host; graphics fall back to an
 * in-memory canvas when /dev/fb0 is unavailable (e.g. over SSH).
 */
#ifndef SHK_H
#define SHK_H

#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */
#define MAXNAME      33      /* identifier chars + NUL                 */
#define MAXDIM       5       /* array dimensions (MMBasic allows 5)    */
#define MAXPARAM     16      /* SUB/FUNCTION parameters                */
#define MAXSTACK     64      /* FOR/GOSUB/DO/SELECT nesting            */
#define MAXCALL      48      /* SUB/FUNCTION recursion depth           */
#define MAXFILE      11      /* file handles #1..#10                   */
#define STR_MAX      255     /* MMBasic string length limit            */
#define VHASH        61     /* variable hash buckets                  */

/* ------------------------------------------------------------------ */
/* Keywords                                                            */
/* ------------------------------------------------------------------ */
/* X-macro keeps the enum, the name table and the lexer in sync.
 * Order matters only in that longer phrases are matched by the parser,
 * not the lexer (e.g. "END SUB" is END followed by SUB).             */
#define KEYWORD_LIST \
  X(PRINT,"PRINT") X(INPUT,"INPUT") X(LINE,"LINE") X(LET,"LET") \
  X(DIM,"DIM") X(LOCAL,"LOCAL") X(STATIC,"STATIC") X(CONST,"CONST") \
  X(IF,"IF") X(THEN,"THEN") X(ELSE,"ELSE") X(ELSEIF,"ELSEIF") \
  X(ENDIF,"ENDIF") X(FOR,"FOR") X(TO,"TO") X(STEP,"STEP") X(NEXT,"NEXT") \
  X(DO,"DO") X(LOOP,"LOOP") X(WHILE,"WHILE") X(UNTIL,"UNTIL") \
  X(WEND,"WEND") X(EXIT,"EXIT") X(CONTINUE,"CONTINUE") \
  X(SELECT,"SELECT") X(CASE,"CASE") X(GOTO,"GOTO") X(GOSUB,"GOSUB") \
  X(RETURN,"RETURN") X(ON,"ON") X(SUB,"SUB") X(FUNCTION,"FUNCTION") \
  X(END,"END") X(CALL,"CALL") X(STOP,"STOP") X(REM,"REM") \
  X(DATA,"DATA") X(READ,"READ") X(RESTORE,"RESTORE") \
  X(RANDOMIZE,"RANDOMIZE") X(CLS,"CLS") X(PAUSE,"PAUSE") \
  X(PIXEL,"PIXEL") X(BOX,"BOX") X(CIRCLE,"CIRCLE") X(RBOX,"RBOX") \
  X(TEXT,"TEXT") X(COLOUR,"COLOUR") X(COLOR,"COLOR") X(FONT,"FONT") \
  X(ARC,"ARC") X(TRIANGLE,"TRIANGLE") X(BLIT,"BLIT") X(REFRESH,"REFRESH") \
  X(OPEN,"OPEN") X(CLOSE,"CLOSE") X(SEEK,"SEEK") X(KILL,"KILL") \
  X(MKDIR,"MKDIR") X(RMDIR,"RMDIR") X(CHDIR,"CHDIR") X(RENAME,"RENAME") \
  X(OPTION,"OPTION") X(ERROR,"ERROR") X(AS,"AS") X(OUTPUT,"OUTPUT") \
  X(APPEND,"APPEND") X(RANDOM,"RANDOM") X(INC,"INC") X(DECR,"DECR") \
  X(SAVE,"SAVE") X(IMAGE,"IMAGE") X(BASE,"BASE") X(EXPLICIT,"EXPLICIT") \
  X(DEFAULT,"DEFAULT") X(INTEGER,"INTEGER") X(FLOAT,"FLOAT") \
  X(STRINGT,"STRING") X(NONE,"NONE") X(ANGLE,"ANGLE") X(DEGREES,"DEGREES") \
  X(RADIANS,"RADIANS") X(TIMER,"TIMER") X(CURSOR,"CURSOR") \
  X(AND,"AND") X(OR,"OR") X(XOR,"XOR") X(NOT,"NOT") X(MOD,"MOD") \
  X(STEPKW,"STEPKW")

enum {
#define X(id,txt) KW_##id,
  KEYWORD_LIST
#undef X
  KW_COUNT
};

extern const char *kw_name[KW_COUNT];

/* ------------------------------------------------------------------ */
/* Tokens                                                              */
/* ------------------------------------------------------------------ */
enum {
  T_EOF = 0,
  T_EOL,      /* end of logical line                                  */
  T_COLON,    /* statement separator                                  */
  T_NUM,      /* float literal                                        */
  T_INT,      /* integer literal                                      */
  T_STR,      /* string literal                                       */
  T_IDENT,    /* variable / sub / function name (suffix kept)         */
  T_LABEL,    /* name followed by ':' at statement start              */
  T_KW,       /* reserved word                                        */
  T_OP,       /* operator or punctuation                              */
  T_LINENO    /* leading line number                                  */
};

/* multi-character operators get codes above ASCII */
enum {
  OP_LE = 256, OP_GE, OP_NE, OP_SHL, OP_SHR
};

typedef struct {
  uint8_t  type;
  uint8_t  kw;        /* when type == T_KW                            */
  int16_t  op;        /* when type == T_OP                            */
  int32_t  line;      /* source line number for diagnostics           */
  int32_t  link;      /* resolved jump target, filled by the linker   */
  int32_t  link2;     /* secondary target (branch chains, loop ends)  */
  double   num;
  int64_t  inum;
  char    *str;       /* literal text or identifier name              */
} Tok;

/* ------------------------------------------------------------------ */
/* Values                                                              */
/* ------------------------------------------------------------------ */
enum { V_FLOAT = 1, V_INT = 2, V_STR = 4 };

typedef struct {
  uint8_t type;
  double  f;
  int64_t i;
  char   *s;          /* heap, owned by the Value                     */
} Value;

/* ------------------------------------------------------------------ */
/* Variables                                                           */
/* ------------------------------------------------------------------ */
enum {
  VF_ARRAY = 1,
  VF_CONST = 2,
  VF_ALIAS = 4        /* byref parameter: storage lives elsewhere      */
};

typedef struct Var {
  char        name[MAXNAME];
  uint8_t     type;         /* V_FLOAT / V_INT / V_STR                */
  uint8_t     flags;
  int         ndim;
  int         dim[MAXDIM];  /* element count per dimension            */
  int         nelem;
  union {                   /* array storage                          */
    double  *f;
    int64_t *i;
    char   **s;
  } a;
  double      sf;           /* scalar storage                         */
  int64_t     si;
  char       *ss;
  struct Var *alias;        /* when VF_ALIAS                          */
  struct Var *next;
} Var;

typedef struct VarTab {
  Var *b[VHASH];
} VarTab;

/* ------------------------------------------------------------------ */
/* User SUB / FUNCTION                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
  char    name[MAXNAME];
  int     is_func;
  int     pc;                     /* first token after the header     */
  int     nparam;
  char    pname[MAXPARAM][MAXNAME];
  uint8_t ptype[MAXPARAM];
  uint8_t parray[MAXPARAM];       /* declared as name()               */
  uint8_t rettype;                /* functions                        */
} Proc;

/* ------------------------------------------------------------------ */
/* Runtime stacks                                                      */
/* ------------------------------------------------------------------ */
enum { S_FOR = 1, S_GOSUB, S_DO, S_WHILE, S_SELECT };

typedef struct {
  int   kind;
  int   pc;           /* loop top / return address                    */
  int   exitpc;       /* token after the loop end                     */
  Var  *var;          /* FOR control variable                         */
  int   idx;          /* FOR array index when the control var is a()  */
  double limit, step;
  int64_t ilimit, istep;
  int   isint;
  Value sel;          /* SELECT CASE subject                          */
  int   matched;      /* SELECT: a CASE has already run               */
} Stk;

typedef struct {
  VarTab locals;
  int    ret;         /* return pc                                    */
  Proc  *proc;
  Value  retval;
  int    sp;          /* stack depth on entry                         */
} Frame;

/* ------------------------------------------------------------------ */
/* Interpreter state                                                   */
/* ------------------------------------------------------------------ */
typedef struct {
  Tok     *tok;
  int      ntok;
  int      pc;

  VarTab   globals;
  Frame    call[MAXCALL];
  int      ncall;

  Stk      stk[MAXSTACK];
  int      sp;

  Proc    *proc;
  int      nproc, cproc;

  /* line number / label directory */
  struct { int32_t num; int pc; } *lmap;
  int      nlmap;
  struct { char name[MAXNAME]; int pc; } *labels;
  int      nlabels;

  /* DATA / READ */
  int     *data;      /* token indices of DATA items                  */
  int      ndata, datap;

  /* options */
  int      opt_base;
  int      opt_explicit;
  int      opt_default;   /* V_FLOAT / V_INT / V_STR / 0 = none       */
  int      opt_degrees;

  /* files */
  FILE    *fh[MAXFILE];

  /* error handling */
  jmp_buf  err_jmp;
  char     err_msg[256];
  int      err_line;
  int      err_code;
  int      on_error_skip;   /* OPTION ERROR SKIP / ON ERROR IGNORE    */
  int      stopped;
  int      exit_code;
} VM;

extern VM vm;
extern char *shk_cmdline;   /* arguments after the program name */

/* ------------------------------------------------------------------ */
/* lex.c                                                               */
/* ------------------------------------------------------------------ */
void  lex_init(void);
int   lex_source(const char *src, Tok **out, int *nout);
const char *tok_text(const Tok *t);

/* ------------------------------------------------------------------ */
/* prog.c                                                              */
/* ------------------------------------------------------------------ */
void  prog_load(const char *src);
void  prog_link(void);
int   prog_find_line(int32_t n);
int   prog_find_label(const char *name);
Proc *prog_find_proc(const char *name);
void  prog_free(void);

/* ------------------------------------------------------------------ */
/* var.c                                                               */
/* ------------------------------------------------------------------ */
void  var_init(void);
void  var_clear_all(void);
void  vt_clear(VarTab *t);
Var  *var_find(const char *name);
Var  *var_find_in(VarTab *t, const char *name);
Var  *var_create(VarTab *t, const char *name, int type);
Var  *var_lookup_or_create(const char *name, int type);
Var  *var_deref(Var *v);
int   name_type(const char *name, char *base);
void  var_dim(Var *v, int ndim, const int *dims);
int   var_index(Var *v, const int *subs, int nsub);

void  var_get(Var *v, int idx, Value *out);
void  var_set(Var *v, int idx, const Value *val);

/* ------------------------------------------------------------------ */
/* value helpers (val.c)                                               */
/* ------------------------------------------------------------------ */
void    val_init(Value *v);
void    val_free(Value *v);
void    val_copy(Value *dst, const Value *src);
void    val_setf(Value *v, double d);
void    val_seti(Value *v, int64_t i);
void    val_sets(Value *v, const char *s);
void    val_setsn(Value *v, const char *s, int n);
double  val_f(const Value *v);
int64_t val_i(const Value *v);
const char *val_s(const Value *v);
int     val_truth(const Value *v);
char   *shk_strdup(const char *s);
void   *shk_alloc(size_t n);
void   *shk_realloc(void *p, size_t n);

/* ------------------------------------------------------------------ */
/* eval.c                                                              */
/* ------------------------------------------------------------------ */
void  eval_expr(Value *out);
void  eval_expect_num(double *out);
int64_t eval_expect_int(void);
void  eval_expect_str(Value *out);
int   is_builtin_func(const char *name);
void  call_builtin(const char *name, Value *out);

/* ------------------------------------------------------------------ */
/* exec.c                                                              */
/* ------------------------------------------------------------------ */
void  exec_run(void);
void  exec_statement(void);
int   mkdir_compat(const char *p);
void  call_proc(Proc *p, Value *result);
void  shk_error(const char *fmt, ...);

/* token cursor helpers shared by eval.c / exec.c */
Tok  *cur(void);
Tok  *peek(int n);
void  advance(void);
int   at_op(int op);
int   at_kw(int kw);
int   accept_op(int op);
int   accept_kw(int kw);
void  expect_op(int op);
void  expect_kw(int kw);
int   at_end_of_stmt(void);
void  skip_to_eol(void);

/* ------------------------------------------------------------------ */
/* gfx.c                                                               */
/* ------------------------------------------------------------------ */
int   gfx_init(int want_fb);
void  gfx_shutdown(void);
void  gfx_cls(uint32_t rgb);
void  gfx_pixel(int x, int y, uint32_t rgb);
uint32_t gfx_getpixel(int x, int y);
void  gfx_line(int x0, int y0, int x1, int y1, uint32_t rgb);
void  gfx_box(int x, int y, int w, int h, int fill, uint32_t rgb, uint32_t fc);
void  gfx_rbox(int x, int y, int w, int h, int r, int fill, uint32_t rgb, uint32_t fc);
void  gfx_circle(int x, int y, int r, int fill, uint32_t rgb, uint32_t fc, double aspect);
void  gfx_arc(int x, int y, int r0, int r1, double a0, double a1, uint32_t rgb);
void  gfx_triangle(int x0,int y0,int x1,int y1,int x2,int y2,int fill,uint32_t rgb,uint32_t fc);
void  gfx_text(int x, int y, const char *s, const char *align, int fontscale,
               uint32_t fg, uint32_t bg);
void  gfx_present(void);
int   gfx_width(void);
int   gfx_height(void);
int   gfx_save_bmp(const char *path);
int   gfx_available(void);

/* ------------------------------------------------------------------ */
/* con.c                                                               */
/* ------------------------------------------------------------------ */
void  con_init(void);
void  con_shutdown(void);
void  con_raw(int on);
int   con_key(void);              /* non-blocking, 0 if none          */
int   con_getkey_block(void);
char *con_readline(const char *prompt);
void  con_out(const char *s);
void  con_outn(const char *s, int n);
void  con_putc(int c);
int   con_col(void);
void  con_set_col(int c);
void  con_flush(void);
double con_timer_ms(void);
void  con_timer_reset(void);
void  con_pause_ms(double ms);

#endif /* SHK_H */
