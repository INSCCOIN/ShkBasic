' hello.bas - the small tour
'   shkbasic examples/hello.bas

PRINT "shkbasic on "; MM.DEVICE$
PRINT "screen is "; MM.HRES; " x "; MM.VRES
PRINT

' variables take their type from a suffix, or from DIM
count% = 3
name$ = "walnut"
ratio = 22 / 7

PRINT "count  "; count%
PRINT "name   "; name$
PRINT "ratio  "; FORMAT$(ratio, "%.4f")
PRINT

FOR i = 1 TO count%
  PRINT "  line "; i; " of "; count%
NEXT i
PRINT

DIM squares(9)
FOR i = 0 TO 9
  squares(i) = i * i
NEXT i
PRINT "squares:";
FOR i = 0 TO 9
  PRINT " "; squares(i);
NEXT i
PRINT
PRINT

PRINT "greeting: "; shout$("hello " + name$)
PRINT "fib(15) = "; fib(15)

FUNCTION shout$(s$)
  shout$ = UCASE$(s$) + "!"
END FUNCTION

FUNCTION fib(n)
  LOCAL a, b, t, i
  a = 0
  b = 1
  FOR i = 1 TO n
    t = a + b
    a = b
    b = t
  NEXT i
  fib = a
END FUNCTION
