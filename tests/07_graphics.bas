' graphics drawn to the in-memory canvas and read back with PIXEL()
CONST RED = RGB(255,0,0)
CONST GRN = RGB(0,255,0)
CONST BLU = RGB(0,0,255)
CONST WHT = RGB(white)

PRINT "res="; MM.HRES; "x"; MM.VRES

CLS RGB(0,0,0)
PRINT "cls="; PIXEL(10,10)

PIXEL 5, 6, RED
PRINT "pixel="; PIXEL(5,6) = RED
PRINT "neighbour="; PIXEL(6,6)

LINE 0, 0, 10, 0, 1, GRN
PRINT "line="; PIXEL(0,0) = GRN; PIXEL(5,0) = GRN; PIXEL(10,0) = GRN

BOX 20, 20, 10, 10, 1, BLU
PRINT "box edge="; PIXEL(20,20) = BLU; PIXEL(29,29) = BLU
PRINT "box hollow="; PIXEL(25,25) = 0

BOX 40, 40, 10, 10, 1, BLU, RED
PRINT "box fill="; PIXEL(45,45) = RED; PIXEL(40,40) = BLU

CIRCLE 100, 100, 20, 1, 1, WHT
PRINT "circle="; PIXEL(120,100) = WHT; PIXEL(100,100) = 0

CIRCLE 200, 100, 15, 1, 1, WHT, GRN
PRINT "circle fill="; PIXEL(200,100) = GRN

TRIANGLE 300,100, 340,100, 320,60, WHT, RED
PRINT "triangle="; PIXEL(320,90) = RED

RBOX 10, 200, 60, 40, 8, WHT
PRINT "rbox="; PIXEL(40,200) = WHT

TEXT 100, 200, "Hi", "LT", , 1, WHT, 0
got = 0
FOR x = 100 TO 111
  FOR y = 200 TO 207
    IF PIXEL(x,y) = WHT THEN got = got + 1
  NEXT y
NEXT x
PRINT "text pixels>10="; got > 10

TEXT 240, 200, "CENTRE", "CM", , 2, WHT, 0
PRINT "centred ok"

COLOUR RGB(255,255,0), RGB(0,0,64)
CLS
PRINT "cls to bg="; PIXEL(1,1) = RGB(0,0,64)
TEXT 10, 10, "uses current colour"
hits = 0
FOR x = 10 TO 60
  FOR y = 10 TO 17
    IF PIXEL(x,y) = RGB(255,255,0) THEN hits = hits + 1
  NEXT y
NEXT x
PRINT "fg from COLOUR="; hits > 20

SAVE IMAGE "tests/out/gfx.bmp"
PRINT "saved"
PRINT "graphics done"
