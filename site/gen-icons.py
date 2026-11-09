#!/usr/bin/env python3
"""Draw the web app icons (site/icons/*.png, committed; run again after a change).

A chip with a text prompt on it: the page's dark background (style.css,
--bg of the dark theme), pins on four sides, ">_" in the green of lines
that ran (--cov-run, dark theme). icon-192/512 have transparent corners;
icon-maskable-512 fills the whole square and keeps the drawing inside the
central 80 % (the safe zone of a maskable icon).
"""
from pathlib import Path

from PIL import Image, ImageDraw

BG = (23, 24, 26)          # --bg (dark)
CHIP = (58, 59, 62)        # --line (dark)
PIN = (163, 161, 154)      # --muted (dark)
PROMPT = (102, 187, 106)   # --cov-run (dark)
OUT = Path(__file__).resolve().parent / "icons"


def draw(size, maskable):
    s = 4 * size  # drawn 4x, then scaled down (smooth edges)
    im = Image.new("RGBA", (s, s), BG + (255,) if maskable else (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    # Content box: the whole icon, or the safe zone of a maskable one.
    m = s * 0.1 if maskable else 0
    box = s - 2 * m
    if not maskable:
        d.rounded_rectangle((0, 0, s - 1, s - 1), radius=s * 0.18, fill=BG)
    u = box / 16
    x0, y0 = m + 3 * u, m + 3 * u
    x1, y1 = m + 13 * u, m + 13 * u
    # Pins: 4 per side.
    for i in range(4):
        c = m + (4.6 + 2.2 * i) * u
        w = 0.55 * u
        d.rectangle((c - w, m + 1.4 * u, c + w, y0), fill=PIN)
        d.rectangle((c - w, y1, c + w, m + 14.6 * u), fill=PIN)
        d.rectangle((m + 1.4 * u, c - w, x0, c + w), fill=PIN)
        d.rectangle((x1, c - w, m + 14.6 * u, c + w), fill=PIN)
    d.rounded_rectangle((x0, y0, x1, y1), radius=1.1 * u, fill=CHIP)
    # ">" as a thick polyline, "_" as a bar.
    t = 0.9 * u
    d.line([(m + 5.2 * u, m + 6 * u), (m + 7.6 * u, m + 8 * u), (m + 5.2 * u, m + 10 * u)], fill=PROMPT, width=round(t), joint="curve")
    d.rectangle((m + 8.4 * u, m + 9.6 * u, m + 11 * u, m + 9.6 * u + t), fill=PROMPT)
    return im.resize((size, size), Image.LANCZOS)


OUT.mkdir(exist_ok=True)
for name, size, maskable in (("icon-192.png", 192, False), ("icon-512.png", 512, False),
                             ("icon-maskable-512.png", 512, True)):
    draw(size, maskable).save(OUT / name, optimize=True)
    print(name, (OUT / name).stat().st_size, "bytes")
