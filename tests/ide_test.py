#!/usr/bin/env python3
"""End-to-end tests for shkide.

Each test drives the real binary inside a pseudo-terminal and asserts on
the screen a user would see, via the pyte terminal emulator.

    python3 tests/ide_test.py            run everything
    python3 tests/ide_test.py edit       run tests whose name contains "edit"
"""
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ide_harness import Ide  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

TESTS = []
FAILS = []


def test(fn):
    TESTS.append(fn)
    return fn


class Sandbox:
    """A scratch directory with a couple of programs in it."""

    def __enter__(self):
        self.dir = tempfile.mkdtemp(prefix="shkide-test-")
        with open(os.path.join(self.dir, "demo.bas"), "w") as f:
            f.write('PRINT "alpha"\nPRINT "beta"\nPRINT 6 * 7\n')
        with open(os.path.join(self.dir, "broken.bas"), "w") as f:
            f.write('PRINT "ok"\nPRINT 1 / 0\nPRINT "never"\n')
        os.mkdir(os.path.join(self.dir, "sub"))
        with open(os.path.join(self.dir, "sub", "inner.bas"), "w") as f:
            f.write('PRINT "inner file"\n')
        with open(os.path.join(self.dir, "notes.txt"), "w") as f:
            f.write("not a basic file\n")
        return self

    def __exit__(self, *a):
        shutil.rmtree(self.dir, ignore_errors=True)

    def path(self, *p):
        return os.path.join(self.dir, *p)

    def read(self, name):
        with open(self.path(name)) as f:
            return f.read()


def check(cond, label):
    if not cond:
        raise AssertionError(label)


# ------------------------------------------------------------------ #
@test
def test_opens_and_shows_file_and_browser():
    with Sandbox() as s:
        ide = Ide([s.path("demo.bas")])
        try:
            txt = ide.text()
            check("demo.bas" in ide.row(0), "header names the file")
            check("FILES" in ide.row(1), "file pane header")
            check("demo.bas" in txt, "file pane lists demo.bas")
            check("sub/" in txt, "file pane lists directories")
            check("notes.txt" not in txt, "non-BASIC files hidden by default")
            check('PRINT "alpha"' in txt, "first line shown")
            check("1:1" in ide.row(23), "cursor position in the status bar")
        finally:
            ide.close()


@test
def test_edit_insert_and_save():
    with Sandbox() as s:
        ide = Ide([s.path("demo.bas")])
        try:
            ide.send("end")
            ide.type("\nPRINT \"added\"")
            check("*" in ide.row(0), "dirty marker appears")
            ide.send("^s")
            check("*" not in ide.row(0), "dirty marker clears after save")
            body = s.read("demo.bas")
            check('PRINT "added"' in body, "edit reached disk")
            check(body.startswith('PRINT "alpha"'), "original content kept")
        finally:
            ide.close()


@test
def test_undo_and_redo():
    with Sandbox() as s:
        ide = Ide([s.path("demo.bas")])
        try:
            ide.send("end")
            ide.type("XYZ")
            check("XYZ" in ide.text(), "typed text visible")
            for _ in range(3):
                ide.send("^z")
            check("XYZ" not in ide.text(), "undo removed the text")
            ide.send("^y")
            check("X" in ide.text(), "redo brought it back")
        finally:
            ide.close()


@test
def test_syntax_highlighting_colours():
    with Sandbox() as s:
        with open(s.path("colour.bas"), "w") as f:
            f.write('PRINT "hi"\n')
        ide = Ide([s.path("colour.bas")])
        try:
            # row 1 is the first text line; find the column of the P in PRINT
            row = ide.row(1)
            col = row.index("PRINT")
            kw = ide.colour_at(1, col)
            strcol = ide.colour_at(1, row.index('"hi"'))
            check(kw != "default", "keyword is coloured (got %r)" % kw)
            check(strcol != "default", "string is coloured (got %r)" % strcol)
            check(kw != strcol, "keyword and string differ (%r vs %r)" % (kw, strcol))
        finally:
            ide.close()


@test
def test_run_captures_output():
    with Sandbox() as s:
        ide = Ide([s.path("demo.bas")], binary=os.path.join(ROOT, "shkide"))
        try:
            ide.send("f5", settle=2.0)
            txt = ide.text()
            check("alpha" in txt, "program stdout in the output pane")
            check("beta" in txt, "second line of output")
            check("42" in txt, "computed value shown")
            check("exit 0" in txt, "exit status reported")
        finally:
            ide.close()


@test
def test_run_reports_error_and_jumps():
    with Sandbox() as s:
        ide = Ide([s.path("broken.bas")])
        try:
            ide.send("f5", settle=2.0)
            txt = ide.text()
            check("Division by zero" in txt, "error text shown")
            check("line 2" in txt, "error names the line")
            ide.send("^e")
            check("2:1" in ide.row(23), "cursor jumped to the failing line")
        finally:
            ide.close()


@test
def test_run_saves_dirty_buffer_first():
    with Sandbox() as s:
        ide = Ide([s.path("demo.bas")])
        try:
            ide.send("end")
            ide.type('\nPRINT "fresh output"')
            ide.send("f5", settle=2.0)
            check("fresh output" in ide.text(), "edit was saved before running")
            check('PRINT "fresh output"' in s.read("demo.bas"), "buffer written")
        finally:
            ide.close()


@test
def test_interactive_run_gets_the_terminal():
    with Sandbox() as s:
        with open(s.path("ask.bas"), "w") as f:
            f.write('INPUT "Name: ", n$\nPRINT "hello " + n$\n')
        ide = Ide([s.path("ask.bas")])
        try:
            ide.send("f6", settle=1.5)
            check("Name:" in ide.text(), "program prompt reached the terminal")
            ide.type("Ian\n", settle=1.0)
            check("hello Ian" in ide.text(), "program saw the typed input")
            check("press any key" in ide.text(), "returns on a keypress")
            ide.send("x", settle=1.0)
            check("shkide" in ide.row(0), "back in the editor")
        finally:
            ide.close()


@test
def test_browser_navigation_and_open():
    with Sandbox() as s:
        ide = Ide([s.path("demo.bas")])
        try:
            ide.send("^o")               # focus the file pane
            check("Enter open" in ide.row(23), "browser key hints")
            # walk down to sub/ and enter it
            for _ in range(8):
                if "sub" in ide.text():
                    break
            # select "sub" by walking the list
            for _ in range(10):
                ide.send("down")
                row = [l for l in ide.lines() if l.strip().startswith("sub/")]
                if row:
                    break
            # move to the sub/ entry explicitly
            ide.send("home")
            for _ in range(10):
                cur = ide.cursor()[0]
                line = ide.row(cur)
                if "sub/" in line:
                    break
                ide.send("down")
            ide.send("enter")
            check("inner.bas" in ide.text(), "entered the subdirectory")
            ide.send("enter")           # open whatever is selected
            txt = ide.text()
            check("inner" in txt, "opened a file from the subdirectory")
        finally:
            ide.close()


@test
def test_browser_show_all_toggle():
    with Sandbox() as s:
        ide = Ide([s.path("demo.bas")])
        try:
            ide.send("^o")
            check("notes.txt" not in ide.text(), "txt hidden by default")
            ide.send("a")
            check("notes.txt" in ide.text(), "show-all reveals other files")
            check("(all)" in ide.text(), "pane header says all")
        finally:
            ide.close()


@test
def test_browser_new_file():
    with Sandbox() as s:
        ide = Ide([s.path("demo.bas")])
        try:
            ide.send("^o")
            ide.send("n")
            check("New file" in ide.row(23), "prompt appeared")
            ide.type("made.bas")
            ide.send("enter")
            check(os.path.exists(s.path("made.bas")), "file created on disk")
            check("made.bas" in ide.row(0), "new file opened in the editor")
        finally:
            ide.close()


@test
def test_find_and_goto():
    with Sandbox() as s:
        ide = Ide([s.path("demo.bas")])
        try:
            ide.send("^f")
            check("Find:" in ide.row(23), "find prompt")
            ide.type("beta")
            ide.send("enter")
            check("2:" in ide.row(23), "cursor moved to the match")
            ide.send("^g")
            ide.type("3")
            ide.send("enter")
            check("3:1" in ide.row(23), "goto line worked")
        finally:
            ide.close()


@test
def test_auto_indent_block():
    with Sandbox() as s:
        with open(s.path("ind.bas"), "w") as f:
            f.write("")
        ide = Ide([s.path("ind.bas")])
        try:
            ide.type("FOR i = 1 TO 3\n")
            ide.type('PRINT i\n')
            ide.send("^s")
            body = s.read("ind.bas")
            lines = body.split("\n")
            check(lines[1].startswith("  PRINT"), "body indented, got %r" % lines[1])
        finally:
            ide.close()


@test
def test_dedent_on_next():
    with Sandbox() as s:
        with open(s.path("ded.bas"), "w") as f:
            f.write("")
        ide = Ide([s.path("ded.bas")])
        try:
            ide.type("FOR i = 1 TO 3\n")
            ide.type("PRINT i\n")
            ide.type("NEXT i")
            ide.send("^s")
            lines = s.read("ded.bas").split("\n")
            check(lines[2].startswith("NEXT"),
                  "NEXT pulled back to column 0, got %r" % lines[2])
        finally:
            ide.close()


@test
def test_quit_warns_about_unsaved_changes():
    with Sandbox() as s:
        ide = Ide([s.path("demo.bas")])
        try:
            ide.type("x")
            ide.send("^q")
            check("Discard unsaved changes?" in ide.row(23), "confirmation shown")
            ide.send("n")
            check("demo.bas" in ide.row(0), "still in the editor after declining")
            check("x" not in s.read("demo.bas"), "nothing written")
        finally:
            ide.close()


@test
def test_narrow_screen_layout():
    with Sandbox() as s:
        ide = Ide([s.path("demo.bas")], cols=53, rows=20)
        try:
            txt = ide.text()
            check("FILES" not in txt, "sidebar hidden on a narrow screen")
            check('PRINT "alpha"' in txt, "editor still shows the program")
            ide.send("^o")
            check("FILES" in ide.text(), "^O brings up the file pane as an overlay")
        finally:
            ide.close()


@test
def test_help_overlay():
    with Sandbox() as s:
        ide = Ide([s.path("demo.bas")])
        try:
            ide.send("f1")
            txt = ide.text()
            check("shkide" in txt and "keys" in txt, "help overlay opened")
            check("F5 run" in txt, "help lists the run key")
            ide.send("esc")
            check("F5 run" not in ide.text() or "^S save" in ide.row(23),
                  "overlay dismissed")
        finally:
            ide.close()


@test
def test_output_pane_toggle():
    with Sandbox() as s:
        ide = Ide([s.path("demo.bas")])
        try:
            check("OUTPUT" in ide.text(), "output pane visible by default")
            ide.send("^l")
            ide.send("^l")
            check("OUTPUT" not in ide.text(), "third state hides the pane")
            ide.send("^l")
            check("OUTPUT" in ide.text(), "cycles back")
        finally:
            ide.close()


@test
def test_delete_line_and_paths():
    with Sandbox() as s:
        ide = Ide([s.path("demo.bas")])
        try:
            ide.send("^k")
            ide.send("^s")
            body = s.read("demo.bas")
            check("alpha" not in body, "first line deleted")
            check("beta" in body, "rest of the file intact")
        finally:
            ide.close()


# ------------------------------------------------------------------ #
def main():
    pattern = sys.argv[1] if len(sys.argv) > 1 else ""
    if not os.access(os.path.join(ROOT, "shkide"), os.X_OK):
        print("build shkide first (make)")
        return 1
    ran = 0
    for fn in TESTS:
        name = fn.__name__.replace("test_", "")
        if pattern and pattern not in name:
            continue
        ran += 1
        try:
            fn()
            print("ok   %s" % name)
        except Exception as exc:  # noqa: BLE001
            print("FAIL %s: %s" % (name, exc))
            FAILS.append(name)
    print()
    print("%d passed, %d failed" % (ran - len(FAILS), len(FAILS)))
    return 1 if FAILS else 0


sys.exit(main())
