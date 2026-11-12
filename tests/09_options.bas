' OPTION statements, colours and console formatting
OPTION BASE 1
DIM q(3)
q(1) = 11
q(3) = 33
PRINT q(1); " "; q(3); " "; BOUND(q())

OPTION ANGLE DEGREES
PRINT SIN(90); " "; COS(0); " "; ATN(1)
OPTION ANGLE RADIANS
PRINT FORMAT$(SIN(PI/2), "%.3f"); " "; FORMAT$(ATN(1), "%.4f")

PRINT HEX$(RGB(red)); " "; HEX$(RGB(white)); " "; HEX$(RGB("cyan"))
PRINT HEX$(RGB(255,128,0)); " "; HEX$(RGB(orange)); " "; HEX$(RGB(300,-5,0))

PRINT "A"; TAB(10); "B"; TAB(20); "C"
PRINT "pos="; POS

FOR k = 1 TO 5
  SELECT CASE k
    CASE 1          : PRINT "one";
    CASE 2, 3       : PRINT "few";
    CASE 4 TO 9     : PRINT "many";
    CASE ELSE       : PRINT "other";
  END SELECT
NEXT k
PRINT

' omitted optional arguments keep their commas
CLS RGB(0,0,0)
LINE 0, 0, 10, 10, , RGB(255,0,0)
PRINT "omitted="; PIXEL(5,5) = RGB(255,0,0)

' integer rounding on assignment is half away from zero
DIM n AS INTEGER
n = 2.5
PRINT n;
n = -2.5
PRINT n;
n = 3.5
PRINT n

' TIMER advances and can be reset
TIMER = 0
PAUSE 25
PRINT "timer>=20 "; TIMER >= 20

PRINT "cmdline=["; MM.CMDLINE$; "]"
PRINT "options done"
