' life.bas - Conway's Life on the SharkDeck screen
'
'   shkbasic -g examples/life.bas              run on the deck, Q quits
'   shkbasic -n -o life.bmp examples/life.bas 40    run 40 generations
'                                                   headless and save it
'
' Pass a number on the command line to stop after that many generations.

CONST CELL = 6
CONST GW = 78            ' 78 * 6 = 468 pixels wide
CONST GH = 48            ' 48 * 6 = 288 pixels tall
CONST OX = 6
CONST OY = 26

CONST BG   = RGB(6, 8, 16)
CONST LIVE = RGB(120, 230, 160)
CONST FADE = RGB(30, 70, 55)
CONST INK  = RGB(200, 215, 240)

DIM grid(GW, GH)
DIM nxt(GW, GH)

maxgen = VAL(MM.CMDLINE$)
RANDOMIZE 7
seed_random
CLS BG
gen = 0

DO
  draw
  step_gen
  gen = gen + 1
  IF maxgen > 0 AND gen >= maxgen THEN EXIT DO
  k$ = INKEY$
  IF k$ = "q" OR k$ = "Q" THEN EXIT DO
  IF k$ = "r" OR k$ = "R" THEN seed_random
  PAUSE 40
LOOP

TEXT 240, 160, "generation " + STR$(gen), "CM", , 2, INK, BG
PRINT "stopped after"; gen; " generations"
END

SUB seed_random
  LOCAL x, y
  FOR y = 0 TO GH - 1
    FOR x = 0 TO GW - 1
      IF RND() < 0.28 THEN
        grid(x, y) = 1
      ELSE
        grid(x, y) = 0
      ENDIF
    NEXT x
  NEXT y
END SUB

SUB draw
  LOCAL x, y
  BOX 0, 0, 480, 20, 1, RGB(14, 18, 34), RGB(14, 18, 34)
  TEXT 6, 10, "LIFE  gen " + STR$(gen) + "   pop " + STR$(population()), "LM", , 1, INK, RGB(14, 18, 34)
  TEXT 474, 10, "Q quit  R reseed", "RM", , 1, FADE, RGB(14, 18, 34)
  FOR y = 0 TO GH - 1
    FOR x = 0 TO GW - 1
      IF grid(x, y) = 1 THEN
        BOX OX + x * CELL, OY + y * CELL, CELL - 1, CELL - 1, 1, LIVE, LIVE
      ELSE
        BOX OX + x * CELL, OY + y * CELL, CELL - 1, CELL - 1, 1, BG, BG
      ENDIF
    NEXT x
  NEXT y
END SUB

SUB step_gen
  LOCAL x, y, n
  FOR y = 0 TO GH - 1
    FOR x = 0 TO GW - 1
      n = neighbours(x, y)
      IF grid(x, y) = 1 THEN
        IF n = 2 OR n = 3 THEN
          nxt(x, y) = 1
        ELSE
          nxt(x, y) = 0
        ENDIF
      ELSE
        IF n = 3 THEN
          nxt(x, y) = 1
        ELSE
          nxt(x, y) = 0
        ENDIF
      ENDIF
    NEXT x
  NEXT y
  FOR y = 0 TO GH - 1
    FOR x = 0 TO GW - 1
      grid(x, y) = nxt(x, y)
    NEXT x
  NEXT y
END SUB

' the grid wraps, so gliders run off one edge and back on the other
FUNCTION neighbours(x, y)
  LOCAL dx, dy, nx, ny, c
  c = 0
  FOR dy = -1 TO 1
    FOR dx = -1 TO 1
      IF dx <> 0 OR dy <> 0 THEN
        nx = (x + dx + GW) MOD GW
        ny = (y + dy + GH) MOD GH
        c = c + grid(nx, ny)
      ENDIF
    NEXT dx
  NEXT dy
  neighbours = c
END FUNCTION

FUNCTION population()
  LOCAL x, y, c
  c = 0
  FOR y = 0 TO GH - 1
    FOR x = 0 TO GW - 1
      c = c + grid(x, y)
    NEXT x
  NEXT y
  population = c
END FUNCTION
