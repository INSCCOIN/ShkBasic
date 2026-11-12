/* builtins.h - the names of every builtin function, in one place.
 *
 * eval.c uses this to decide what it will dispatch, and the IDE's
 * syntax highlighter uses the same list, so the editor can never
 * colour a function the interpreter does not actually have.
 */
#ifndef BUILTINS_H
#define BUILTINS_H

#define BUILTIN_LIST \
  B("ABS") B("ACOS") B("ASC") B("ASIN") B("ATN") B("ATAN2") B("BIN$") \
  B("BOUND") B("CHOICE") B("CHR$") B("CINT") B("COS") B("CWD$") B("DATE$") \
  B("DEG") B("DIR$") B("EOF") B("EXP") B("FIELD$") B("FIX") B("FORMAT$") \
  B("HEX$") B("INKEY$") B("INPUT$") B("INSTR") B("INT") B("INV") \
  B("LCASE$") B("LEFT$") B("LEN") B("LOC") B("LOF") B("LOG") B("LTRIM$") \
  B("MAX") B("MID$") B("MIN") B("OCT$") B("PI") B("POS") B("RAD") \
  B("REPLACE$") B("RGB") B("RIGHT$") B("RND") B("RTRIM$") B("SGN") B("SIN") \
  B("SPACE$") B("SQR") B("STR$") B("STRING$") B("TAB") B("TAN") B("TIME$") \
  B("TIMER") B("TRIM$") B("UCASE$") B("VAL") \
  B("MM.HRES") B("MM.VRES") B("MM.DEVICE$") B("MM.VER") B("MM.CMDLINE$")

#endif
