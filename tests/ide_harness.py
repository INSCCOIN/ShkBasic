#!/usr/bin/env python3
"""Drive shkide inside a pseudo-terminal and read back the rendered screen.

Used by tests/ide_test.py.  pyte emulates the terminal, so the tests assert
on what a user would actually see rather than on raw escape bytes.
"""
import os
import pty
import select
import signal
import time

import pyte

KEYS = {
    "up": "\x1b[A", "down": "\x1b[B", "right": "\x1b[C", "left": "\x1b[D",
    "home": "\x1b[H", "end": "\x1b[F",
    "pgup": "\x1b[5~", "pgdn": "\x1b[6~",
    "del": "\x1b[3~", "ins": "\x1b[2~",
    "enter": "\r", "tab": "\t", "esc": "\x1b", "bksp": "\x7f",
    "f1": "\x1b[11~", "f3": "\x1b[13~", "f5": "\x1b[15~", "f6": "\x1b[17~",
}
for _c in "abcdefghijklmnopqrstuvwxyz":
    KEYS["^" + _c] = chr(ord(_c) - 96)


class Ide:
    def __init__(self, args=None, cols=80, rows=24, cwd=None, binary="./shkide"):
        self.cols, self.rows = cols, rows
        self.screen = pyte.Screen(cols, rows)
        self.stream = pyte.ByteStream(self.screen)
        argv = [binary] + (args or [])
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            if cwd:
                os.chdir(cwd)
            os.environ["TERM"] = "xterm"
            try:
                os.execv(argv[0], argv)
            finally:
                os._exit(127)
        self._set_size(cols, rows)
        self.pump(0.4)

    def _set_size(self, cols, rows):
        import fcntl
        import struct
        import termios
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ,
                    struct.pack("HHHH", rows, cols, 0, 0))

    def pump(self, timeout=0.25):
        """Read everything the program emits until it goes quiet."""
        end = time.time() + timeout
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if not r:
                continue
            try:
                data = os.read(self.fd, 65536)
            except OSError:
                break
            if not data:
                break
            self.stream.feed(data)
            end = time.time() + 0.12
        return self

    def send(self, *keys, settle=0.25):
        for k in keys:
            os.write(self.fd, KEYS.get(k, k).encode())
            time.sleep(0.02)
        self.pump(settle)
        return self

    def type(self, text, settle=0.25):
        for ch in text:
            os.write(self.fd, ("\r" if ch == "\n" else ch).encode())
            time.sleep(0.012)
        self.pump(settle)
        return self

    def lines(self):
        return [self.screen.display[i].rstrip() for i in range(self.rows)]

    def text(self):
        return "\n".join(self.lines())

    def row(self, i):
        return self.screen.display[i].rstrip()

    def cursor(self):
        return (self.screen.cursor.y, self.screen.cursor.x)

    def colour_at(self, y, x):
        return self.screen.buffer[y][x].fg

    def close(self):
        try:
            os.kill(self.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.close(self.fd)
        except OSError:
            pass
        try:
            os.waitpid(self.pid, 0)
        except ChildProcessError:
            pass
