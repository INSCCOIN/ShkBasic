' DATA / READ / RESTORE and file I/O
DATA 10, 20, "thirty"
DATA 40, 50

FOR i = 1 TO 3
  READ v$
  PRINT v$; " ";
NEXT i
PRINT
READ a, b
PRINT a + b

RESTORE
READ x
PRINT "restored"; x

RESTORE second
READ y
PRINT "from label"; y
GOTO filetest

second:
DATA 777

filetest:
f$ = "tests/out/scratch.txt"
OPEN f$ FOR OUTPUT AS #1
PRINT #1, "line one"
PRINT #1, "line two"
PRINT #1, 42; ","; 3.5
CLOSE #1

OPEN f$ FOR INPUT AS #2
LINE INPUT #2, l1$
LINE INPUT #2, l2$
PRINT "["; l1$; "]"
PRINT "["; l2$; "]"
PRINT "eof="; EOF(2)
LINE INPUT #2, l3$
PRINT "["; l3$; "]"
PRINT "eof="; EOF(2)
CLOSE #2

OPEN f$ FOR APPEND AS #3
PRINT #3, "appended"
CLOSE #3

OPEN f$ FOR INPUT AS #1
n = 0
DO WHILE NOT EOF(1)
  LINE INPUT #1, ln$
  n = n + 1
LOOP
CLOSE #1
PRINT "lines="; n
PRINT "last=["; ln$; "]"

' comma separated INPUT # parsing
OPEN "tests/out/csv.txt" FOR OUTPUT AS #1
PRINT #1, "alpha,7,2.5"
CLOSE #1
OPEN "tests/out/csv.txt" FOR INPUT AS #1
INPUT #1, name$, count, ratio
CLOSE #1
PRINT name$; "|"; count; "|"; ratio

KILL "tests/out/csv.txt"
PRINT "data done"
