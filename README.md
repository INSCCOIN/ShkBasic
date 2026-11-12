# shkbasic

An MMBasic-compatible interpreter written in C, plus a terminal IDE, for
the SharkDeck.

It exists so the MMBasic programs you already wrote for the PicoCalc have
somewhere to run on a WalnutPi-class board: same language, same graphics
verbs, but drawing to `/dev/fb0` on a 480×320 panel instead of to a Pico.

- One binary, no runtime dependencies beyond libc and libm.
- Peak RSS around 8 MB with a full-screen canvas — it does not swap on 981 MB.
- Graphics work with or without a framebuffer, so you can develop over SSH
  and save the screen as a BMP.
- Static-linkable, so a half-broken `dpkg` state on the SD card can't strand you.

```
$ shkbasic examples/hello.bas
$ shkbasic -g examples/deckstat.bas        # on the deck, uses /dev/fb0
$ shkbasic -n -o shot.bmp examples/life.bas 40   # headless, writes a BMP
$ shkide examples/life.bas                 # edit and run it in one place
```

## Build

```
make                 # shkbasic and shkide, for this machine
make test            # interpreter test suite
make test-ide        # IDE tests (needs python3 and pyte)
make deck            # cross-compile both, static, for aarch64
sudo make install    # /usr/local/bin/{shkbasic,shkide}
```

Cross-compiling needs `gcc-aarch64-linux-gnu`:

```
sudo apt install gcc-aarch64-linux-gnu
make deck
scp shkbasic-aarch64 pi@192.168.1.164:~/shkbasic
scp shkide-aarch64    pi@192.168.1.164:~/shkide
```

Building on the deck itself works (`make`), but it burns thermal headroom you
probably want for running things. Cross-compiling takes a second on a laptop.

## Command line

```
shkbasic [options] program.bas [args...]
shkbasic [options] -e 'PRINT 2+2'

  -e STMT      run one line of BASIC instead of a file
  -g           open the framebuffer at startup
  -n           never touch the framebuffer (draw to memory only)
  -o FILE.bmp  write the graphics screen to a BMP when the program ends
  -s SEED      seed the random number generator for repeatable runs
  -v, -h       version, help
```

Anything after the program name is passed to the program and readable as
`MM.CMDLINE$`.

Over SSH there is no framebuffer, so `PIXEL`, `LINE`, `BOX` and friends draw
into an in-memory canvas instead of failing. `-o` writes that canvas out, and
`PIXEL(x, y)` reads it back, which is how the graphics test suite checks
itself without a screen.

## The language

### Types and variables

Three types: 64-bit integer, double-precision float, and string (255 chars).

```basic
count% = 10          ' % integer
ratio! = 1.5         ' ! float
name$  = "deck"      ' $ string
plain  = 3.14        ' no suffix: float, or OPTION DEFAULT

DIM total AS INTEGER
DIM INTEGER a, b, c
DIM scores(9)                  ' 0..9 with OPTION BASE 0
DIM grid(7, 7) AS INTEGER
DIM names$(3)
DIM table(3) = (10, 20, 30, 40)
CONST LIMIT = 99, TAG$ = "x"
```

`a`, `a%` and `a$` are three different variables, as in MMBasic.

Arrays take up to 5 dimensions. `BOUND(a())` gives the top index of the first
dimension, `BOUND(a(), n)` of the nth. `OPTION BASE 0` or `1` sets the lowest
index and must come before any `DIM`.

### Operators

Highest binding first:

| Level | Operators |
|---|---|
| 1 | `^` (right associative) |
| 2 | unary `-` `+` |
| 3 | `*` `/` `\` `MOD` |
| 4 | `+` `-` |
| 5 | `<<` `>>` |
| 6 | `=` `<>` `<` `<=` `>` `>=` |
| 7 | `NOT` |
| 8 | `AND` |
| 9 | `OR` `XOR` |

`\` is integer division and `MOD` the remainder. `+` concatenates strings.
Comparisons return 1 or 0. `NOT` is logical (`NOT 5` is 0); `INV(x)` is the
bitwise complement. `AND`, `OR` and `XOR` are bitwise, which behaves as
logical when the operands are 1 and 0.

Numbers stay integers through integer-only arithmetic and become floats as
soon as a float or `/` is involved. Assigning a float to an integer variable
rounds half away from zero.

### Control flow

```basic
IF x > 10 THEN PRINT "big" ELSE PRINT "small"

IF x > 10 THEN
  PRINT "big"
ELSEIF x > 5 THEN
  PRINT "medium"
ELSE
  PRINT "small"
ENDIF

FOR i = 1 TO 10 STEP 2
  IF i = 5 THEN CONTINUE FOR
  IF i = 9 THEN EXIT FOR
NEXT i

DO                  DO WHILE c < 10        WHILE c < 10
  c = c + 1           c = c + 1              c = c + 1
LOOP UNTIL c > 5    LOOP                   WEND

SELECT CASE k
  CASE 1          : PRINT "one"
  CASE 2, 3       : PRINT "a few"
  CASE 4 TO 9     : PRINT "several"
  CASE > 100      : PRINT "lots"
  CASE ELSE       : PRINT "other"
END SELECT

GOTO label  /  GOSUB label ... RETURN  /  ON n GOTO a, b, c
```

Labels are `name:` at the start of a line. Classic line numbers work too.

### SUB and FUNCTION

```basic
greet "world"
PRINT area(3, 4)

SUB greet name$
  PRINT "hello "; name$
END SUB

FUNCTION area(w, h)
  LOCAL a
  a = w * h
  area = a            ' assign to the function's own name
END FUNCTION          ' or: RETURN w * h
```

Parameters may be parenthesised or not. An argument that is nothing but a
variable name is passed **by reference**, so the sub can change it; anything
else is an expression and goes by value. Arrays pass by reference as `a()`:

```basic
SUB scale arr(), factor
  LOCAL i
  FOR i = 0 TO BOUND(arr())
    arr(i) = arr(i) * factor
  NEXT i
END SUB
```

`LOCAL` declares variables private to the call. Undeclared variables inside a
sub are global, as in MMBasic. Recursion works; the depth limit is 48 frames.
`EXIT SUB` and `EXIT FUNCTION` return early.

### Console and files

```basic
PRINT "a", "b"        ' comma tabs to the next 10-column zone
PRINT "no newline";
INPUT "Name: ", n$
LINE INPUT raw$
k$ = INKEY$           ' non-blocking, "" when nothing is pressed

OPEN "log.txt" FOR OUTPUT AS #1     ' INPUT / OUTPUT / APPEND / RANDOM
PRINT #1, "a line"
CLOSE #1
OPEN "log.txt" FOR INPUT AS #1
DO WHILE NOT EOF(1)
  LINE INPUT #1, ln$
LOOP
CLOSE #1
```

Also `SEEK`, `LOC`, `LOF`, `KILL`, `MKDIR`, `RMDIR`, `CHDIR`, `RENAME`,
`DIR$(pattern$ [, "FILE"|"DIR"])`, and `INPUT$(n [, #h])`.

### Graphics

Colours are 24-bit; build them with `RGB(r, g, b)` or `RGB(red)` and the other
named colours.

```basic
CLS [colour]
PIXEL x, y [, colour]
LINE x1, y1, x2, y2 [, width] [, colour]
BOX x, y, w, h [, width] [, colour] [, fill]
RBOX x, y, w, h [, radius] [, colour] [, fill]
CIRCLE x, y, r [, width] [, aspect] [, colour] [, fill]
ARC x, y, r1, r2, a1, a2 [, colour]
TRIANGLE x1,y1, x2,y2, x3,y3 [, colour] [, fill]
TEXT x, y, text$ [, align$] [, font] [, scale] [, colour] [, bg]
COLOUR fg [, bg]
FONT n [, scale]
REFRESH
SAVE IMAGE "file.bmp"
```

Omit an optional argument by leaving the comma in place:
`LINE 0, 0, 10, 10, , RGB(red)`. `align$` is a horizontal letter `L`/`C`/`R`
and an optional vertical `T`/`M`/`B`, e.g. `"CM"` to centre on the point.
The built-in font is 6×8; `scale` multiplies it.

`PIXEL(x, y)` as a function reads a colour back. `MM.HRES` and `MM.VRES` give
the screen size.

The screen is pushed to the framebuffer at most 60 times a second, and always
at `PAUSE`, `REFRESH` and program exit, so a tight drawing loop does not spend
all its time blitting.

### Built-in functions

**Maths** — `ABS ACOS ASIN ATN ATAN2 CINT COS DEG EXP FIX INT LOG MAX MIN PI
RAD RND SGN SIN SQR TAN INV`

**Strings** — `ASC BIN$ CHR$ FIELD$ FORMAT$ HEX$ INSTR LCASE$ LEFT$ LEN
LTRIM$ MID$ OCT$ REPLACE$ RIGHT$ RTRIM$ SPACE$ STR$ STRING$ TRIM$ UCASE$ VAL
CHOICE TAB`

**System** — `DATE$ TIME$ TIMER CWD$ DIR$ EOF LOC LOF INKEY$ INPUT$ POS BOUND
RGB PIXEL MM.HRES MM.VRES MM.DEVICE$ MM.CMDLINE$ MM.VER`

`FORMAT$(n, "%.2f")` takes one printf conversion from `diouxXfFeEgG`; anything
else is rejected rather than passed to `printf`.

### Other statements

`DATA` / `READ` / `RESTORE [label]`, `RANDOMIZE [seed]`, `PAUSE ms`,
`TIMER = 0`, `INC var [, amount]`, `END`, `STOP`, `ERROR "message"`.

`OPTION BASE 0|1`, `OPTION EXPLICIT`, `OPTION DEFAULT INTEGER|FLOAT|STRING|NONE`,
`OPTION ANGLE DEGREES|RADIANS`, `OPTION ERROR SKIP|ABORT`.


## shkide

A terminal IDE: editor, file browser, and a run pane, in one screen.


```
 shkide  life.bas *                                        F1 for keys
FILES                 |  9 CONST CELL = 6
 ../                  | 10 CONST GW = 78      ' 78 * 6 = 468 pixels wide
 deckstat.bas         | 11 CONST GH = 48
 hello.bas            | 12
 life.bas             | 13 DIM grid(GW, GH)
 OUTPUT
--- /home/pi/life.bas ---
stopped after 12 generations
--- exit 0 ---
 ^S save  F5 run  F6 run+input  ^O files  ^F find  ^G goto  F1 help  ^Q quit
```

It is termios and ANSI escapes only, no ncurses, so it works over PuTTY and
on the deck's own console with nothing installed. The layout follows the
terminal: at 70 columns or more the file pane is a sidebar, below that it
becomes a full-screen overlay you bring up with `^O` — which is what the
deck's 60-column console wants.

### Keys

| | |
|---|---|
| `^S` save, `^A` save as, `^N` new | `^O` file pane / switch panes |
| `^Z` undo, `^Y` redo | `^K` cut line, `^D` duplicate line |
| `Tab` indent, `^T` unindent | `^F` find, `F3` find next, `^G` goto line |
| `F5` run into the output pane | `F6` run with the real terminal |
| `^E` jump to the reported error | `^L` cycle the output pane height |
| `F1` help | `^Q` quit |

In the file pane: `Enter` open, `Backspace` parent directory, `n` new file,
`d` delete, `r` rename, `a` show non-BASIC files, `Tab` back to the editor.

### Running from the editor

`F5` saves the buffer, runs it with `shkbasic -n`, and streams stdout and
stderr into the output pane. Saving first is deliberate: it means the line
numbers in an error message point at the lines you are looking at. The
program runs with its own directory as the working directory, so relative
paths like `OPEN "data.txt"` behave the same as from a shell. `Esc` or `^C`
stops a program that is taking too long.

If the run reports an error, `^E` jumps the cursor to that line and the
gutter marks it in red.

`F6` is for programs that read the keyboard. It hands over the real terminal,
clears it, and runs `shkbasic` with no capture, so `INPUT`, `INKEY$` and
`LINE INPUT` behave normally; press any key to come back to the editor.

Graphics programs run fine under `F5` — they draw to the in-memory canvas,
and `SAVE IMAGE` still writes a BMP. To see them on the panel, run them from
the console with `shkbasic -g`.

### Editing

Syntax highlighting reads the interpreter's own keyword and builtin tables
(`src/keywords.h` and `src/builtins.h`, shared by both programs), so the
editor cannot colour something as a builtin that `shkbasic` would then
reject.

Indentation follows the block structure: `FOR`, `DO`, `WHILE`, `SUB`,
`FUNCTION`, `SELECT` and a block `IF` indent the next line, and typing
`NEXT`, `LOOP`, `WEND`, `ENDIF`, `ELSE` or `CASE` pulls the current line back
one level, once per line so `END` becoming `ENDIF` does not dedent twice.

Undo keeps 64 whole-file snapshots. Saving writes to a neighbouring `.tmp`
file and renames it, so a full SD card or a power cut cannot leave a
half-written program behind.

## Errors

By default an error prints the line and stops with exit status 1:

```
shkbasic: error on line 12: Division by zero
```

`OPTION ERROR SKIP` reports each error and continues on the next line. Note
that skipping unwinds any SUB or FUNCTION call in progress back to the top
level — it is a way to survive bad input, not a general exception system.

## What is not here

Honest list, so you don't go looking:

- No `SETPIN`/`PIN`/`PWM`/`I2C`/`SPI`/`ONEWIRE`. On Linux these belong to
  `libgpiod` and `/dev/i2c-*` rather than to the interpreter; shelling out or
  reading `/sys` covers most of it for now.
- No `SPRITE`, `BLIT`, `LOAD IMAGE`, or `MODE` switching.
- No `SETTICK`/interrupts, no `ON KEY`.
- No `SAVE`/`LOAD`/`LIST`/`RUN` immediate mode inside the interpreter — use
  `shkide`, or write files and run them.
- shkide has no block selection, no copy/paste between files, and no
  multi-file tabs. One buffer at a time.
- No `MID$(a$,n,m) = x$` assignment form, no `LONGSTRING`, no `MATH` or
  `SORT` statements.
- `GOTO` out of a `FOR` or `DO` block leaves its stack entry behind; jump to a
  label in the same block level, as you would in MMBasic anyway.

## Layout

```
src/shk.h      types, limits, the whole data model
src/lex.c      source text to one flat token array
src/prog.c     link pass: resolves IF/FOR/DO/SELECT/SUB to token indices,
               builds the line, label, procedure and DATA directories
src/var.c      variables, arrays, scopes, by-reference aliases
src/val.c      value construction and conversion
src/eval.c     expression parser and the builtin function library
src/exec.c     statement execution and control flow
src/gfx.c      framebuffer and in-memory canvas, BMP writer
src/con.c      termios raw keys, line input, millisecond timer
src/keywords.h every reserved word; shared with the IDE
src/builtins.h every builtin function name; shared with the IDE
src/font6x8.h  generated by tools/mkfont.py from hand-drawn glyph art

ide/term.c     raw mode, key decoding, one-write-per-frame drawing
ide/edit.c     text buffer, editing, undo, search
ide/browser.c  directory pane
ide/syntax.c   highlighting, driven by the shared keyword tables
ide/shkide.c   layout, event loop, dialogs, running programs

tests/         .bas programs with expected output; sh tests/run.sh
tests/ide_test.py   drives shkide in a pty and asserts on the screen
examples/      hello.bas, deckstat.bas, life.bas
```

The whole program is one `Tok[]`, so every jump target is a single integer
index. The link pass resolves matching keywords once at load time, which is
why execution never scans forward looking for an `ENDIF`.

To change the font, edit the glyph art at the top of `tools/mkfont.py` and run
`make font`.

## Performance

On this interpreter, measured on a desktop; the deck's Cortex-A53 at 1.5 GHz
is roughly 5–8× slower:

- bare loop with arithmetic: ~0.5 µs per iteration
- SUB/FUNCTION call with three locals: ~2.6 µs per call
- Conway's Life, 78×48 cells, redrawing every cell: ~170 ms per generation

If a drawing loop feels slow on the deck, redraw only the cells that changed
rather than the whole grid.
