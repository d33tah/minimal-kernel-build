#!/usr/bin/env python3
"""Tests for qemu_screen_check.py, the site workflow's boot check.

The fixture is the terminal output of the kernels job's -O0 boot in run
42539920959: curses drew "H", a backspace, escapes, a cursor-right and
"ello, World!". The expect check timed out on it; the screen check must not.
"""
import importlib.util
import shutil
import subprocess
import unittest
from pathlib import Path

SITE = Path(__file__).resolve().parent
FIXTURE = SITE / "testdata" / "vmtest-split-run42539920959.tty"
EXPECT = ["Hello, World!", "Still alive"]

spec = importlib.util.spec_from_file_location("qemu_screen_check", SITE / "qemu_screen_check.py")
qsc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qsc)


def screen_of(data, chunk=None):
    s = qsc.Screen()
    if chunk is None:
        s.feed(data)
    else:
        for i in range(0, len(data), chunk):
            s.feed(data[i:i + chunk])
    return s


class CapturedSplitOutput(unittest.TestCase):
    def setUp(self):
        self.data = FIXTURE.read_bytes()

    def test_bytes_do_not_hold_the_string(self):
        # Why the expect pattern timed out.
        self.assertNotIn(b"Hello, World!", self.data)
        self.assertIn(b"H\x08\x1b(B\x1b[m", self.data)

    @unittest.skipUnless(shutil.which("expect"), "expect not installed")
    def test_expect_pattern_times_out(self):
        # vmtest.tcl's first expect, on the captured output. The pattern list
        # needs its newlines: a one-line braced list is one pattern to expect.
        script = ("set timeout 3\n"
                  "spawn -noecho cat {%s}\n"
                  "expect {\n"
                  "    \"Hello, World!\" { exit 0 }\n"
                  "    timeout { exit 1 }\n"
                  "    eof { exit 2 }\n"
                  "}\n" % FIXTURE)
        rc = subprocess.run(["expect", "-c", script], capture_output=True).returncode
        self.assertEqual(rc, 2)

    def test_screen_shows_both_lines(self):
        s = screen_of(self.data)
        lines = s.text().split("\n")
        self.assertEqual(lines[8], "Hello, World!")
        self.assertEqual(lines[9], "Still alive")
        self.assertTrue(s.contains_in_order(EXPECT))

    def test_any_read_size(self):
        # pty reads end anywhere: inside an escape or a UTF-8 character.
        for chunk in (1, 2, 3, 7, 64, 4096):
            self.assertTrue(screen_of(self.data, chunk).contains_in_order(EXPECT), chunk)

    def test_before_still_alive_is_not_enough(self):
        cut = self.data[:self.data.index(b"Still alive")]
        s = screen_of(cut)
        self.assertTrue(s.contains_in_order(EXPECT[:1]))
        self.assertFalse(s.contains_in_order(EXPECT))

    def test_cli_replay(self):
        ok = subprocess.run([str(SITE / "qemu_screen_check.py"), f"--replay={FIXTURE}",
                             "--expect=Hello, World!", "--expect=Still alive"],
                            capture_output=True, text=True)
        self.assertEqual(ok.returncode, 0, ok.stdout)
        bad = subprocess.run([str(SITE / "qemu_screen_check.py"), f"--replay={FIXTURE}",
                              "--expect=Still alive", "--expect=Hello, World!"],
                             capture_output=True, text=True)
        self.assertEqual(bad.returncode, 1, bad.stdout)


class ScreenSemantics(unittest.TestCase):
    def test_order(self):
        s = screen_of(b"\x1b[1;1HStill alive\x1b[2;1HHello, World!")
        self.assertFalse(s.contains_in_order(EXPECT))

    def test_overwritten_text_is_gone(self):
        s = screen_of(b"Hello, World!\rJello")
        self.assertFalse(s.contains_in_order(EXPECT[:1]))

    def test_cleared_screen_is_gone(self):
        s = screen_of(b"Hello, World!\r\nStill alive\x1b[H\x1b[2J")
        self.assertFalse(s.contains_in_order(EXPECT[:1]))

    def test_cursor_moves_between_pieces(self):
        # Other ways curses can draw the same line in pieces.
        for data in (b"Hel\x1b[1;4Hlo, World!",
                     b"Hello,\x1b[CWorld!",
                     b"Hello, Wor\x1b[3D\x1b[3Cld!",
                     b"\x1b[5;20Hlo, World!\x1b[5;17HHel",
                     b"Hello, World\x1b[1G\x1b[12C!"):
            self.assertTrue(screen_of(data).contains_in_order(EXPECT[:1]), data)

    def test_erase_line(self):
        s = screen_of(b"Hello, World!\x1b[6G\x1b[K")
        self.assertEqual(s.text().split("\n")[0], "Hello")

    def test_wrap_and_scroll(self):
        s = screen_of(b"x" * 79 + b"ab" + b"\r\n" * 30 + b"last")
        lines = s.text().split("\n")
        self.assertEqual(lines[-1], "last")
        self.assertEqual(len(lines), qsc.ROWS)


if __name__ == "__main__":
    unittest.main(verbosity=2)
