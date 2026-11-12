' deckstat.bas - live SharkDeck status on the 480x320 screen
'
'   shkbasic -g examples/deckstat.bas          on the deck
'   shkbasic -n -o deckstat.bmp examples/deckstat.bas   over SSH
'
' Reads the H618's thermal zone and /proc directly, so it works on the
' stock WalnutPi image with nothing installed.  Press Q to quit.

CONST BG    = RGB(8, 10, 20)
CONST PANEL = RGB(18, 22, 40)
CONST INK   = RGB(220, 230, 255)
CONST DIM_  = RGB(120, 135, 170)
CONST COOL  = RGB(90, 200, 255)
CONST WARM  = RGB(255, 190, 70)
CONST HOT   = RGB(255, 90, 90)

CONST W = 480
CONST H = 320
CONST HIST = 110

DIM temps(HIST)
DIM n = 0

COLOUR INK, BG
CLS

DO
  t = cpu_temp()
  push_temp t

  CLS
  header
  big_temp t
  gauge "LOAD", 250, 60, loadavg(), 4
  meter "MEM", 250, 130, mem_used_pct()
  meter "DISK", 250, 170, 16
  graph
  footer

  ' "shkbasic examples/deckstat.bas once" draws one frame and exits,
  ' which is how you capture a BMP of it over SSH
  IF MM.CMDLINE$ = "once" THEN
    PRINT "one frame drawn"
    END
  ENDIF

  k$ = INKEY$
  IF k$ = "q" OR k$ = "Q" THEN EXIT DO
  PAUSE 1000
LOOP

CLS RGB(0,0,0)
PRINT "deckstat stopped"
END

SUB header
  BOX 0, 0, W, 34, 1, PANEL, PANEL
  TEXT 12, 17, "SHARKDECK", "LM", , 2, COOL, PANEL
  TEXT W - 12, 17, TIME$, "RM", , 1, DIM_, PANEL
END SUB

SUB big_temp t
  LOCAL c
  c = temp_colour(t)
  BOX 12, 46, 220, 120, 1, PANEL, PANEL
  TEXT 24, 60, "CPU TEMPERATURE", "LT", , 1, DIM_, PANEL
  TEXT 122, 112, FORMAT$(t, "%.1f") + "C", "CM", , 4, c, PANEL
  TEXT 122, 150, state$(t), "CM", , 1, c, PANEL
END SUB

SUB gauge label$, x, y, v, maxv
  LOCAL w
  TEXT x, y - 14, label$, "LT", , 1, DIM_, BG
  BOX x, y, 210, 18, 1, PANEL, PANEL
  w = 210 * v / maxv
  IF w > 210 THEN w = 210
  IF w < 1 THEN w = 1
  BOX x, y, w, 18, 1, COOL, COOL
  TEXT x + 214, y + 9, FORMAT$(v, "%.2f"), "RM", , 1, INK, BG
END SUB

SUB meter label$, x, y, pct
  LOCAL w
  TEXT x, y - 14, label$ + "  " + STR$(INT(pct)) + "%", "LT", , 1, DIM_, BG
  BOX x, y, 210, 14, 1, PANEL, PANEL
  w = 210 * pct / 100
  IF w < 1 THEN w = 1
  IF w > 210 THEN w = 210
  BOX x, y, w, 14, 1, WARM, WARM
END SUB

SUB graph
  LOCAL i, x, y, py, v
  BOX 12, 180, 220, 100, 1, PANEL, PANEL
  TEXT 20, 188, "LAST " + STR$(HIST) + " SAMPLES", "LT", , 1, DIM_, PANEL
  py = -1
  FOR i = 0 TO n - 1
    v = temps(i)
    IF v < 20 THEN v = 20
    IF v > 90 THEN v = 90
    x = 18 + i * 2
    y = 274 - (v - 20) * 70 / 70
    IF py >= 0 THEN LINE x - 2, py, x, y, 1, temp_colour(v)
    py = y
  NEXT i
END SUB

SUB footer
  TEXT 12, H - 14, "Q to quit", "LB", , 1, DIM_, BG
  TEXT W - 12, H - 14, "uptime " + uptime$(), "RB", , 1, DIM_, BG
END SUB

SUB push_temp t
  LOCAL i
  IF n < HIST THEN
    temps(n) = t
    n = n + 1
  ELSE
    FOR i = 0 TO HIST - 2
      temps(i) = temps(i + 1)
    NEXT i
    temps(HIST - 1) = t
  ENDIF
END SUB

FUNCTION temp_colour(t)
  IF t >= 75 THEN
    temp_colour = HOT
  ELSEIF t >= 60 THEN
    temp_colour = WARM
  ELSE
    temp_colour = COOL
  ENDIF
END FUNCTION

FUNCTION state$(t)
  IF t >= 75 THEN
    state$ = "THROTTLING SOON"
  ELSEIF t >= 60 THEN
    state$ = "WARM"
  ELSE
    state$ = "NOMINAL"
  ENDIF
END FUNCTION

' /sys/class/thermal/thermal_zone0/temp is in millidegrees
FUNCTION cpu_temp()
  LOCAL s$
  s$ = read_first$("/sys/class/thermal/thermal_zone0/temp")
  IF s$ = "" THEN
    cpu_temp = 0
  ELSE
    cpu_temp = VAL(s$) / 1000
  ENDIF
END FUNCTION

FUNCTION loadavg()
  LOCAL s$
  s$ = read_first$("/proc/loadavg")
  loadavg = VAL(FIELD$(s$, 1, " "))
END FUNCTION

FUNCTION mem_used_pct()
  LOCAL ln$, total, avail
  total = 0
  avail = 0
  IF NOT exists("/proc/meminfo") THEN
    mem_used_pct = 0
    EXIT FUNCTION
  ENDIF
  OPEN "/proc/meminfo" FOR INPUT AS #1
  DO WHILE NOT EOF(1)
    LINE INPUT #1, ln$
    IF INSTR(ln$, "MemTotal:") = 1 THEN total = VAL(MID$(ln$, 10))
    IF INSTR(ln$, "MemAvailable:") = 1 THEN avail = VAL(MID$(ln$, 14))
  LOOP
  CLOSE #1
  IF total > 0 THEN
    mem_used_pct = 100 * (total - avail) / total
  ELSE
    mem_used_pct = 0
  ENDIF
END FUNCTION

FUNCTION uptime$()
  LOCAL s$, secs, h, m
  s$ = read_first$("/proc/uptime")
  secs = VAL(FIELD$(s$, 1, " "))
  h = INT(secs / 3600)
  m = INT((secs - h * 3600) / 60)
  uptime$ = STR$(h) + "h" + STR$(m) + "m"
END FUNCTION

' DIR$ returns "" when the path is absent, which keeps this example
' runnable on a desktop where /sys/class/thermal does not exist.
FUNCTION read_first$(path$)
  LOCAL ln$
  ln$ = ""
  IF NOT exists(path$) THEN
    read_first$ = ""
    EXIT FUNCTION
  ENDIF
  OPEN path$ FOR INPUT AS #2
  IF NOT EOF(2) THEN LINE INPUT #2, ln$
  CLOSE #2
  read_first$ = ln$
END FUNCTION

FUNCTION exists(path$)
  exists = (DIR$(path$, "FILE") <> "")
END FUNCTION
