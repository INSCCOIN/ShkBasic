' arrays, DIM forms and types
OPTION BASE 0
DIM a(5)
FOR i = 0 TO 5
  a(i) = i * i
NEXT i
PRINT a(0); " "; a(3); " "; a(5)
PRINT "bound="; BOUND(a())

DIM g(2,3) AS INTEGER
FOR r = 0 TO 2
  FOR c = 0 TO 3
    g(r,c) = r * 10 + c
  NEXT c
NEXT r
PRINT g(0,0); " "; g(1,2); " "; g(2,3)
PRINT "dims="; BOUND(g(),0); ","; BOUND(g(),1)

DIM names$(3)
names$(0) = "shark"
names$(1) = "deck"
PRINT names$(0) + "-" + names$(1)
PRINT "empty["; names$(2); "]"

DIM INTEGER counters(3)
counters(1) = 7.8
PRINT counters(1)

DIM total AS INTEGER
total = 5 / 2
PRINT total

DIM pi2 AS FLOAT = 1.5
PRINT pi2

DIM tbl(3) = (10, 20, 30, 40)
PRINT tbl(0); " "; tbl(3)

CONST LIMIT = 99, TAG$ = "const"
PRINT LIMIT, TAG$

' suffixes make distinct variables
v = 1.5
v% = 7
v$ = "text"
PRINT v; " "; v%; " "; v$

' three dimensions
DIM cube(1,1,1)
cube(1,1,1) = 42
PRINT cube(1,1,1)

' OPTION BASE 1 style indexing on a fresh array
DIM z(4)
z(4) = 123
PRINT z(4)
PRINT "arrays done"
