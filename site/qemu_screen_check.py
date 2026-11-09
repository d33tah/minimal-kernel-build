#!/usr/bin/env python3
"""Boot a kernel in qemu's curses display and wait for lines on its screen.

The site workflow's boot check. An expect pattern on qemu's terminal output
fails when curses draws a line in pieces: run 42539920959 printed "H", a
backspace, color escapes, a cursor-right and then "ello, World!", so
"Hello, World!" never occurred in the byte stream although the screen showed
it, and the step timed out. This script replays the output into an 80x25
screen (the cursor movement, erase and scroll sequences ncurses sends to an
xterm) and looks for the strings in the screen's text, in the given order.

  qemu_screen_check.py --qemu=qemu-system-x86_64 --kernel=bzImage --mem=18M \
      --expect="Hello, World!" --expect="Still alive"
  qemu_screen_check.py --replay=captured.txt --expect=...   (no qemu)

Exit 0 when every string is on the screen in order, 1 otherwise (timeout or
qemu's exit first); the screen is printed in both cases.
"""
import argparse
import codecs
import fcntl
import os
import pty
import select
import struct
import subprocess
import sys
import termios
import time

ROWS = 25
COLS = 80
ESC = "\x1b"


class Screen:
    """The part of an xterm that ncurses uses to draw qemu's VGA text screen."""

    def __init__(self, rows=ROWS, cols=COLS):
        self.rows = rows
        self.cols = cols
        self.cells = [[" "] * cols for _ in range(rows)]
        self.row = 0
        self.col = 0
        self.top = 0
        self.bottom = rows - 1
        self.wrap_pending = False
        self.autowrap = True
        self.saved = (0, 0)
        self.decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
        self.pending = ""  # an escape sequence split across two reads

    def feed(self, data):
        text = self.pending + self.decoder.decode(data)
        self.pending = ""
        i = 0
        n = len(text)
        while i < n:
            ch = text[i]
            if ch == ESC:
                end = self._escape(text, i)
                if end is None:
                    self.pending = text[i:]
                    return
                i = end
                continue
            self._control_or_print(ch)
            i += 1

    def text(self):
        return "\n".join("".join(r).rstrip() for r in self.cells)

    def contains_in_order(self, strings):
        text = self.text()
        pos = 0
        for s in strings:
            at = text.find(s, pos)
            if at < 0:
                return False
            pos = at + len(s)
        return True

    # Escape sequences. Returns the index after the sequence, or None when
    # the text ends inside it.
    def _escape(self, text, i):
        if i + 1 >= len(text):
            return None
        kind = text[i + 1]
        if kind == "[":
            j = i + 2
            while j < len(text) and not ("\x40" <= text[j] <= "\x7e"):
                j += 1
            if j >= len(text):
                return None
            self._csi(text[i + 2:j], text[j])
            return j + 1
        if kind == "]":  # OSC, ends with BEL or ESC \
            j = i + 2
            while j < len(text):
                if text[j] == "\x07":
                    return j + 1
                if text[j] == ESC and j + 1 < len(text) and text[j + 1] == "\\":
                    return j + 2
                j += 1
            return None
        if kind in "()*+#":  # charset designation: one more character
            return i + 3 if i + 2 < len(text) else None
        if kind == "7":
            self.saved = (self.row, self.col)
        elif kind == "8":
            self.row, self.col = self.saved
            self.wrap_pending = False
        elif kind == "D":
            self._linefeed()
        elif kind == "E":
            self.col = 0
            self._linefeed()
        elif kind == "M":
            self._reverse_index()
        elif kind == "c":
            self.__init__(self.rows, self.cols)
        # "=" / ">" (keypad modes) and anything else: no effect on the screen.
        return i + 2

    def _csi(self, params, final):
        private = params[:1] in ("?", ">", "<", "=")
        if private:
            # Modes (?25l cursor, ?1049h alternate screen, ?7h autowrap, ...).
            if final in "hl" and "7" in params[1:].split(";"):
                self.autowrap = final == "h"
            return
        nums = []
        for p in params.split(";"):
            try:
                nums.append(int(p))
            except ValueError:
                nums.append(0)

        def arg(k, default=1):
            v = nums[k] if k < len(nums) else 0
            return v if v else default

        self.wrap_pending = False
        if final in "Hf":
            self.row = self._clamp_row(arg(0) - 1)
            self.col = self._clamp_col(arg(1) - 1)
        elif final == "A":
            self.row = max(self.top if self.row >= self.top else 0, self.row - arg(0))
        elif final == "B":
            self.row = min(self.bottom if self.row <= self.bottom else self.rows - 1, self.row + arg(0))
        elif final == "C":
            self.col = self._clamp_col(self.col + arg(0))
        elif final == "D":
            self.col = self._clamp_col(self.col - arg(0))
        elif final == "d":
            self.row = self._clamp_row(arg(0) - 1)
        elif final in "G`":
            self.col = self._clamp_col(arg(0) - 1)
        elif final == "J":
            self._erase_display(arg(0, 0))
        elif final == "K":
            self._erase_line(arg(0, 0))
        elif final == "X":
            for c in range(self.col, min(self.cols, self.col + arg(0))):
                self.cells[self.row][c] = " "
        elif final == "P":
            line = self.cells[self.row]
            k = min(arg(0), self.cols - self.col)
            del line[self.col:self.col + k]
            line.extend([" "] * k)
        elif final == "@":
            line = self.cells[self.row]
            k = min(arg(0), self.cols - self.col)
            line[self.col:self.col] = [" "] * k
            del line[self.cols:]
        elif final == "L":
            self._insert_lines(arg(0))
        elif final == "M":
            self._delete_lines(arg(0))
        elif final == "S":
            for _ in range(arg(0)):
                self._scroll_up()
        elif final == "T":
            for _ in range(arg(0)):
                self._scroll_down()
        elif final == "r":
            top = arg(0) - 1
            bottom = arg(1, self.rows) - 1
            if 0 <= top < bottom < self.rows:
                self.top, self.bottom = top, bottom
            else:
                self.top, self.bottom = 0, self.rows - 1
            self.row, self.col = 0, 0
        # m (colors), h/l (ANSI modes), t (window ops), n, c, ...: no effect.

    def _control_or_print(self, ch):
        if ch == "\r":
            self.col = 0
            self.wrap_pending = False
        elif ch in "\n\x0b\x0c":
            self._linefeed()
        elif ch == "\b":
            if self.col > 0:
                self.col -= 1
            self.wrap_pending = False
        elif ch == "\t":
            self.col = min(self.cols - 1, (self.col // 8 + 1) * 8)
        elif ch < " " or ch == "\x7f":
            pass  # BEL, SO/SI, NUL, ...
        else:
            if self.wrap_pending:
                self.col = 0
                self._linefeed()
                self.wrap_pending = False
            self.cells[self.row][self.col] = ch
            if self.col == self.cols - 1:
                self.wrap_pending = self.autowrap
            else:
                self.col += 1

    def _clamp_row(self, r):
        return max(0, min(self.rows - 1, r))

    def _clamp_col(self, c):
        return max(0, min(self.cols - 1, c))

    def _linefeed(self):
        self.wrap_pending = False
        if self.row == self.bottom:
            self._scroll_up()
        elif self.row < self.rows - 1:
            self.row += 1

    def _reverse_index(self):
        if self.row == self.top:
            self._scroll_down()
        elif self.row > 0:
            self.row -= 1

    def _blank(self):
        return [" "] * self.cols

    def _scroll_up(self):
        del self.cells[self.top]
        self.cells.insert(self.bottom, self._blank())

    def _scroll_down(self):
        del self.cells[self.bottom]
        self.cells.insert(self.top, self._blank())

    def _insert_lines(self, k):
        if not self.top <= self.row <= self.bottom:
            return
        for _ in range(min(k, self.bottom - self.row + 1)):
            del self.cells[self.bottom]
            self.cells.insert(self.row, self._blank())

    def _delete_lines(self, k):
        if not self.top <= self.row <= self.bottom:
            return
        for _ in range(min(k, self.bottom - self.row + 1)):
            del self.cells[self.row]
            self.cells.insert(self.bottom, self._blank())

    def _erase_display(self, mode):
        if mode == 0:
            self._erase_line(0)
            for r in range(self.row + 1, self.rows):
                self.cells[r] = self._blank()
        elif mode == 1:
            self._erase_line(1)
            for r in range(0, self.row):
                self.cells[r] = self._blank()
        else:
            self.cells = [self._blank() for _ in range(self.rows)]

    def _erase_line(self, mode):
        line = self.cells[self.row]
        if mode == 0:
            cols = range(self.col, self.cols)
        elif mode == 1:
            cols = range(0, self.col + 1)
        else:
            cols = range(self.cols)
        for c in cols:
            line[c] = " "


def replay(path, expect):
    screen = Screen()
    with open(path, "rb") as f:
        screen.feed(f.read())
    return screen.contains_in_order(expect), screen


def boot(qemu, kernel, mem, expect, timeout, log):
    """Run qemu on a pty of ROWS x COLS until the strings are on its screen."""
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
    env = dict(os.environ, TERM=os.environ.get("TERM") or "xterm")
    cmd = [qemu, "-kernel", kernel, "-display", "curses", "-m", mem]
    print("spawn " + " ".join(cmd), flush=True)
    proc = subprocess.Popen(cmd, stdin=slave, stdout=slave, stderr=slave,
                            env=env, start_new_session=True, close_fds=True)
    os.close(slave)
    screen = Screen()
    found = False
    deadline = time.monotonic() + timeout
    logf = open(log, "wb") if log else None
    try:
        while not found:
            left = deadline - time.monotonic()
            if left <= 0:
                print(f"timeout: not on the screen after {timeout} s", flush=True)
                break
            ready, _, _ = select.select([master], [], [], min(left, 1.0))
            if not ready:
                continue
            try:
                data = os.read(master, 65536)
            except OSError:  # EIO: qemu exited and closed the pty
                data = b""
            if not data:
                print(f"qemu exited ({proc.poll()}) before the strings were on the screen", flush=True)
                break
            if logf:
                logf.write(data)
            screen.feed(data)
            found = screen.contains_in_order(expect)
    finally:
        if logf:
            logf.close()
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        os.close(master)
    return found, screen


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--expect", action="append", required=True,
                    help="a string that must be on the screen; repeat, in order")
    ap.add_argument("--replay", help="feed this captured terminal output instead of running qemu")
    ap.add_argument("--qemu", default="qemu-system-x86_64")
    ap.add_argument("--kernel")
    ap.add_argument("--mem", default="18M")
    ap.add_argument("--timeout", type=float, default=60.0,
                    help="seconds (60: under load the TCG boot without KVM can take well over 10 s)")
    ap.add_argument("--log", help="write qemu's raw terminal output here")
    a = ap.parse_args()
    if a.replay:
        ok, screen = replay(a.replay, a.expect)
    else:
        if not a.kernel:
            ap.error("--kernel is required without --replay")
        ok, screen = boot(a.qemu, a.kernel, a.mem, a.expect, a.timeout, a.log)
    print("screen:")
    print(screen.text().rstrip("\n"))
    print(("PASS: on the screen in order: " if ok else "FAIL: not on the screen in order: ")
          + ", ".join(repr(s) for s in a.expect), flush=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
