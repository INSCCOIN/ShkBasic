' SUB and FUNCTION: values, references, recursion, scope
greet "SharkDeck"
greet "walnut"

PRINT square(7)
PRINT addup(3, 4)

x = 10
bump x
PRINT "byref x ="; x

y = 10
bumpval y + 0
PRINT "byval y ="; y

PRINT "fact(6) ="; fact(6)
PRINT "fib(10) ="; fib(10)

DIM list(4)
FOR i = 0 TO 4
  list(i) = i + 1
NEXT i
PRINT "sum ="; total(list())
scale list(), 10
PRINT "scaled ="; list(0); " "; list(4)

PRINT "upper = "; shout$("quiet")

g = 5
touchglobal
PRINT "global g ="; g

counter
counter
counter
PRINT "calls ="; ncalls

PRINT "early ="; early(3)
PRINT "early ="; early(99)

SUB greet name$
  PRINT "hello "; name$
END SUB

FUNCTION square(n)
  square = n * n
END FUNCTION

FUNCTION addup(a, b)
  RETURN a + b
END FUNCTION

SUB bump v
  v = v + 1
END SUB

SUB bumpval v
  v = v + 1
END SUB

FUNCTION fact(n)
  IF n <= 1 THEN
    fact = 1
  ELSE
    fact = n * fact(n - 1)
  ENDIF
END FUNCTION

FUNCTION fib(n)
  IF n < 2 THEN
    fib = n
  ELSE
    fib = fib(n-1) + fib(n-2)
  ENDIF
END FUNCTION

FUNCTION total(arr())
  LOCAL s, i
  s = 0
  FOR i = 0 TO BOUND(arr())
    s = s + arr(i)
  NEXT i
  total = s
END FUNCTION

SUB scale arr(), factor
  LOCAL i
  FOR i = 0 TO BOUND(arr())
    arr(i) = arr(i) * factor
  NEXT i
END SUB

FUNCTION shout$(s$)
  shout$ = UCASE$(s$)
END FUNCTION

SUB touchglobal
  g = g * 2
END SUB

SUB counter
  ncalls = ncalls + 1
END SUB

FUNCTION early(n)
  IF n > 10 THEN
    early = -1
    EXIT FUNCTION
  ENDIF
  early = n * 2
END FUNCTION
