' error reporting: each of these is caught and reported, then skipped
OPTION ERROR SKIP

PRINT 1 / 0
PRINT "after divide by zero"

DIM small(2)
PRINT small(9)
PRINT "after bad index"

PRINT SQR(-1)
PRINT "after sqr"

x = "text" + 1
PRINT "after type mix"

PRINT undefined_function_xyz(3)
PRINT "after unknown function"

GOTO nowhere_at_all
PRINT "after bad goto"

OPEN "/nonexistent/dir/file.txt" FOR INPUT AS #1
PRINT "after bad open"

PRINT nosuchfile$(1)
PRINT "after bad array"

ERROR "a user raised error"
PRINT "after user error"

CONST FIXED = 1
FIXED = 2
PRINT "after const write"

PRINT "errors done"
