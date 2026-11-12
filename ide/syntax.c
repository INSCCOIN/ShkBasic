/* syntax.c - BASIC highlighting
 *
 * The keyword and builtin tables come straight from the interpreter's
 * own headers, so the editor cannot colour something green that
 * shkbasic would then reject.
 */
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "../src/keywords.h"
#include "../src/builtins.h"
#include "syntax.h"

/* the IDE links none of the interpreter's objects, so it carries its
 * own copy of the name table generated from the shared list */
static const char *kw_text[KW_COUNT] = {
#define X(id,txt) txt,
  KEYWORD_LIST
#undef X
};

static const char *bi_name[] = {
#define B(n) n,
  BUILTIN_LIST
#undef B
  NULL
};

static int ident_char(int c) { return isalnum(c) || c == '_' || c == '.'; }

static int is_kw(const char *s, int len)
{
  int i, j;
  for (i = 0; i < KW_COUNT; i++) {
    const char *k = kw_text[i];
    for (j = 0; j < len; j++)
      if (!k[j] || toupper((unsigned char)s[j]) != k[j]) break;
    if (j == len && !k[len]) return 1;
  }
  return 0;
}

static int is_builtin(const char *s, int len)
{
  int i;
  for (i = 0; bi_name[i]; i++)
    if ((int)strlen(bi_name[i]) == len && strncasecmp(bi_name[i], s, (size_t)len) == 0)
      return 1;
  return 0;
}

void syn_line(const char *s, int len, unsigned char *attr)
{
  int i = 0;
  int stmt_start = 1;

  memset(attr, SYN_TEXT, (size_t)len);

  while (i < len) {
    int c = (unsigned char)s[i];

    if (c == ' ' || c == '\t') { i++; continue; }

    /* comments run to the end of the line */
    if (c == '\'') {
      memset(attr + i, SYN_COMMENT, (size_t)(len - i));
      return;
    }
    if ((len - i) >= 3 && strncasecmp(s + i, "REM", 3) == 0 &&
        (i + 3 >= len || !ident_char((unsigned char)s[i+3]))) {
      memset(attr + i, SYN_COMMENT, (size_t)(len - i));
      return;
    }

    if (c == '"') {
      int j = i + 1;
      while (j < len && s[j] != '"') j++;
      if (j < len) j++;
      memset(attr + i, SYN_STRING, (size_t)(j - i));
      i = j;
      stmt_start = 0;
      continue;
    }

    if (isdigit(c) || (c == '&' && i + 1 < len) ||
        (c == '.' && i + 1 < len && isdigit((unsigned char)s[i+1]))) {
      int j = i;
      if (s[j] == '&') {
        j++;
        while (j < len && isalnum((unsigned char)s[j])) j++;
      } else {
        while (j < len && (isdigit((unsigned char)s[j]) || s[j] == '.')) j++;
        if (j < len && (s[j] == 'e' || s[j] == 'E')) {
          int k = j + 1;
          if (k < len && (s[k] == '+' || s[k] == '-')) k++;
          if (k < len && isdigit((unsigned char)s[k])) {
            j = k;
            while (j < len && isdigit((unsigned char)s[j])) j++;
          }
        }
      }
      memset(attr + i, SYN_NUMBER, (size_t)(j - i));
      i = j;
      stmt_start = 0;
      continue;
    }

    if (isalpha(c) || c == '_') {
      int j = i;
      int cls;
      while (j < len && ident_char((unsigned char)s[j])) j++;
      if (j < len && (s[j] == '$' || s[j] == '%' || s[j] == '!')) j++;

      if (is_kw(s + i, j - i))            cls = SYN_KEYWORD;
      else if (is_builtin(s + i, j - i))  cls = SYN_BUILTIN;
      else if (stmt_start && j < len && s[j] == ':') cls = SYN_LABEL;
      else                                cls = SYN_TEXT;

      memset(attr + i, (unsigned char)cls, (size_t)(j - i));
      i = j;
      stmt_start = 0;
      continue;
    }

    if (c == ':') { attr[i++] = SYN_OP; stmt_start = 1; continue; }
    if (strchr("+-*/\\^=<>(),;#?", c)) { attr[i++] = SYN_OP; stmt_start = 0; continue; }

    i++;
    stmt_start = 0;
  }
}

/* leading-whitespace change a line implies for the line after it:
 * +1 to indent the next line, -1 to pull this one back */
int syn_indent_delta(const char *s, int len, int *dedent_self)
{
  int i = 0, j;
  char w[16];
  int n = 0;
  int delta = 0;

  *dedent_self = 0;
  while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
  for (j = i; j < len && n < 15 && (isalpha((unsigned char)s[j])); j++)
    w[n++] = (char)toupper((unsigned char)s[j]);
  w[n] = 0;
  if (!n) return 0;

  if (!strcmp(w, "FOR") || !strcmp(w, "DO") || !strcmp(w, "WHILE") ||
      !strcmp(w, "SUB") || !strcmp(w, "FUNCTION") || !strcmp(w, "SELECT"))
    delta = 1;

  /* a block IF is one whose line ends with THEN */
  if (!strcmp(w, "IF")) {
    int k = len;
    while (k > 0 && (s[k-1] == ' ' || s[k-1] == '\t')) k--;
    if (k >= 4 && strncasecmp(s + k - 4, "THEN", 4) == 0) delta = 1;
  }

  if (!strcmp(w, "NEXT") || !strcmp(w, "LOOP") || !strcmp(w, "WEND") ||
      !strcmp(w, "ENDIF") || !strcmp(w, "END") || !strcmp(w, "ELSE") ||
      !strcmp(w, "ELSEIF") || !strcmp(w, "CASE")) {
    *dedent_self = 1;
    if (!strcmp(w, "ELSE") || !strcmp(w, "ELSEIF") || !strcmp(w, "CASE"))
      delta = 1;
  }
  return delta;
}
