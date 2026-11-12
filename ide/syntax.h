/* syntax.h - BASIC highlighting classes */
#ifndef SYNTAX_H
#define SYNTAX_H

enum {
  SYN_TEXT = 0,
  SYN_KEYWORD,
  SYN_BUILTIN,
  SYN_STRING,
  SYN_NUMBER,
  SYN_COMMENT,
  SYN_LABEL,
  SYN_OP,
  SYN_COUNT
};

/* fills attr[0..len-1] with one SYN_* class per character */
void syn_line(const char *s, int len, unsigned char *attr);

/* how this line should affect indentation of the next one */
int  syn_indent_delta(const char *s, int len, int *dedent_self);

#endif
