' control structures
FOR i = 1 TO 5
  PRINT i;
NEXT i
PRINT

FOR i = 10 TO 1 STEP -3
  PRINT i;
NEXT
PRINT

FOR i = 1 TO 0
  PRINT "never"
NEXT i
PRINT "empty loop ok"

FOR i = 1 TO 3
  FOR j = 1 TO 3
    IF j = 2 THEN CONTINUE FOR
    IF i = 3 THEN EXIT FOR
    PRINT i; j;
  NEXT j
NEXT i
PRINT

n = 0
DO
  n = n + 1
LOOP UNTIL n >= 4
PRINT "do-until"; n

n = 0
DO WHILE n < 3
  n = n + 1
LOOP
PRINT "do-while"; n

n = 0
WHILE n < 5
  n = n + 2
WEND
PRINT "while"; n

n = 0
DO
  n = n + 1
  IF n = 3 THEN EXIT DO
LOOP
PRINT "exit do"; n

x = 7
IF x > 10 THEN
  PRINT "big"
ELSEIF x > 5 THEN
  PRINT "medium"
ELSE
  PRINT "small"
ENDIF

IF x = 7 THEN PRINT "single line yes" ELSE PRINT "single line no"
IF x = 8 THEN PRINT "wrong" ELSE PRINT "else branch"

IF x > 0 THEN
  IF x > 5 THEN
    PRINT "nested deep"
  END IF
  PRINT "nested outer"
END IF

FOR k = 1 TO 5
  SELECT CASE k
    CASE 1
      PRINT "one";
    CASE 2, 3
      PRINT "two-or-three";
    CASE 4 TO 5
      PRINT "four-to-five";
    CASE ELSE
      PRINT "other";
  END SELECT
NEXT k
PRINT

SELECT CASE "beta"
  CASE "alpha"
    PRINT "A"
  CASE "beta"
    PRINT "B"
  CASE ELSE
    PRINT "C"
END SELECT

SELECT CASE 42
  CASE < 10
    PRINT "small"
  CASE > 40
    PRINT "large"
END SELECT

GOTO skipper
PRINT "not printed"
skipper:
PRINT "label goto ok"

GOSUB helper
PRINT "back from gosub"
GOTO done

helper:
PRINT "in subroutine"
RETURN

done:
FOR i = 1 TO 3
  ON i GOTO l1, l2, l3
l1:
  PRINT "on-goto 1";
  GOTO cont
l2:
  PRINT "on-goto 2";
  GOTO cont
l3:
  PRINT "on-goto 3";
cont:
NEXT i
PRINT
PRINT "control done"
