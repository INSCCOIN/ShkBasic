' string library
s$ = "SharkDeck"
PRINT LEN(s$)
PRINT LEFT$(s$, 5); "|"; RIGHT$(s$, 4); "|"; MID$(s$, 6, 4)
PRINT MID$(s$, 6)
PRINT UCASE$(s$); " "; LCASE$(s$)
PRINT ASC("A"); " "; CHR$(66)
PRINT INSTR(s$, "Deck")
PRINT INSTR(s$, "zzz")
PRINT INSTR(3, "abcabc", "a")
PRINT "["; SPACE$(4); "]"
PRINT STRING$(5, "-")
PRINT STRING$(3, 65)
PRINT "["; TRIM$("  pad  "); "]"
PRINT "["; LTRIM$("  pad  "); "]"
PRINT "["; RTRIM$("  pad  "); "]"
PRINT STR$(42); "|"; STR$(3.5)
PRINT VAL("42"); " "; VAL("3.5"); " "; VAL("abc"); " "; VAL("&H1F")
PRINT HEX$(255); " "; BIN$(10); " "; OCT$(64)
PRINT HEX$(255, 6)
PRINT FORMAT$(3.14159, "%.2f")
PRINT FORMAT$(42, "%06.1f")
PRINT FORMAT$(255, "%X")
PRINT "val=" + FORMAT$(7, "%03d") + "!"
PRINT REPLACE$("a-b-c", "-", "+")
PRINT FIELD$("one,two,three", 2)
PRINT FIELD$("a:b:c", 3, ":")
PRINT CHOICE(1 > 0, "yes", "no")
PRINT CHOICE(0, 10, 20)

' building strings in a loop
out$ = ""
FOR i = 1 TO 5
  out$ = out$ + STR$(i)
NEXT i
PRINT out$

' comparisons and concatenation
a$ = "abc"
b$ = "abd"
PRINT a$ < b$, a$ = "abc", a$ <> b$
PRINT a$ + b$

' INC on a string
t$ = "grow"
INC t$, "ing"
PRINT t$
n = 5
INC n
INC n, 10
PRINT n
PRINT "strings done"
