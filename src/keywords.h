/* keywords.h - every reserved word, in one place.
 *
 * The interpreter's lexer and the IDE's syntax highlighter both read
 * this list, so the editor cannot colour a word as a keyword that
 * shkbasic would not recognise.
 */
#ifndef KEYWORDS_H
#define KEYWORDS_H

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

#endif
