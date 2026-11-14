#!/usr/bin/env python3
"""Fail when a colour or mark of the page is missing from the About tab's legend.

Operator 2026-11-09: "why is Code at EIP sometimes yellow? a legend in the
About tab would help". The legend (index.html, inside id="about_legend")
shows a sample of every colour and mark, drawn with the classes the
listings, the Debugger, the Source panel, the Coverage tab and the warnings
use. This check keeps it complete: every class that style.css gives a
colour, background, outline, stroke, fill, dash, decoration, opacity or
italics must have a sample in the legend, unless CHROME below says why it
is no mark (page furniture: tabs, buttons, popovers, muted notes). Every
colour token on :root must be used by some rule or by the scripts. The
legend's generated parts (memory regions and sections, Graph subsystems)
are checked in the browser (test_browser.py, suite legend).
"""
import html.parser
import re
import sys
from pathlib import Path

site = Path(__file__).resolve().parent

# Classes with colours that mark nothing: the reason for each.
CHROME = {
    "controls": "labels of the controls above the screen (muted)",
    "mm-map": "the frame around the live memory map (a grey border)",
    "runbar": "labels of the run controls (muted)",
    "srcpick": "labels of the source picker (muted)",
    "note": "notes under controls (muted text)",
    "dim": "secondary text, e.g. symbol+offset after an instruction (muted)",
    "symindex": "the symbol index row of the Page tab (muted)",
    "syminfo": "the Debugger's symbol rows (muted labels, readable disabled buttons)",
    "top": "the sticky header's background",
    "tabbar": "tab buttons",
    "rbmenu": "the frame of the menu toggle's menu",
    "rbwords": "the run state in words in that menu (the text colour)",
    "rbpwa": "the new version notice in that menu (a warning's colours)",
    "tip": "the ? buttons",
    "tip-pop": "the popover",
    "jump-pop": "the jump paths popover",
    "navhist": "the Back and Forward buttons (in the menu toggle's menu)",
    "srcpanel": "the Source panel's frame",
    "crumb": "the directories and file name of the Source panel's title (link colour, like every button that goes somewhere)",
    "srcopt": "a row of the Source panel's file switcher (its hover background)",
    "active": "the file switcher's row that Enter opens (the focus ring's colour)",
    "current": "the file switcher's row of the file shown (underlined name)",
    "srcswhead": "the group headings of the file switcher (muted)",
    "srcswn": "the line counts in the file switcher (muted)",
    "listing-small": "the box of Code at EIP",
    "legend": "the Memory tab's legend list (its swatches are checked in the browser)",
    "tipmark": "the focus ring of a mark whose popover is open",
    "jstubhit": "the invisible pointer target of a gutter stub",
    "ins": "every instruction line (its marks are .cur, .mid, .jhit, .bad, .nx)",
    "covrow": "rows of the Coverage tree (function names in the link colour)",
    "covname": "names in the Coverage tree",
    "covcell": "the counts in the Coverage tree (muted)",
    "ctree": "the frame of a tree",
    "trow": "rows of a tree",
    "thead": "a tree's header row",
    "ttog": "a tree's fold button",
    "tnum": "a tree's numbers (muted)",
    "bar": "the Memory tab's region bars (their colours are the region swatches)",
    "fnfacts": "function facts text",
    "fnhead": "a function's name in the Debugger",
    "symlink": "a symbol button (link colour, like every button that goes somewhere)",
    "regs": "register names (muted)",
    "watches": "the Watch list's notes",
    "lhead": "scope headings in Parameters and locals (muted)",
    "lloc": "where a local lives (muted note)",
    "fmeth": "the label naming how a frame was found (EIP, CFI, ...)",
    "fnframe": "a function's frame facts (muted)",
    "inl": "rows of inlined functions in the Debugger (muted)",
    "legendlink": "the Legend links themselves",
    "snapother": "rows of snapshots for another kernel (muted text, the row also says so)",
    "bmacts": "the actions of a bookmark in the menu's list (link, rename, delete)",
    "bmadd": "the ☆ buttons that add a bookmark (muted where they sit in a row)",
    "cframe": "a call stack frame (its ☆ button is muted)",
    "covbm": "the Source tab's bookmark column (its ☆ button is muted)",
    "bmdlg": "the add-a-bookmark dialog's frame",
    "bmempty": "the empty bookmarks list's note (muted)",
    "bmghead": "the group headings of the bookmarks list (muted)",
    "edarea": "the frame of the source editor",
    "edgutter": "the source editor's line numbers (muted)",
    "edtext": "the source editor's text area (page colours)",
    "edlist": "the Source tab's list of edited files",
    "eddiffbtn": "a Diff toggle of that list, pressed while its diff is shown (the focus ring colour)",
    "edopen": "an edited file's name in that list (link colour, like every button that goes somewhere; struck through when deleted)",
    "bmgo": "a bookmark's name in the list (link colour; its ★ is the legend's gold star)",
    "bmwhat": "what a bookmark points at, under its name (muted)",
    "snapcol": "the hexdump columns of a snapshot diff (their yellow is <mark>, in the legend)",
}


def colour_classes(css):
    css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
    props = re.compile(r"[{;]\s*(color|background(?:-color)?|outline(?:-style)?|stroke(?:-dasharray)?|fill|"
                       r"text-decoration|opacity|font-style|box-shadow|border(?:-color)?)\s*:")
    out = {}
    for m in re.finditer(r"([^{}]+)\{([^{}]*)\}", css):
        sel, body = m.group(1).strip(), m.group(2)
        if sel.startswith("@") or ":root" in sel or not props.search("{" + body):
            continue
        for c in re.findall(r"\.([a-zA-Z][\w-]*)", sel):
            out.setdefault(c, []).append(sel)
    return out


class Legend(html.parser.HTMLParser):
    """Class tokens inside the element with id="about_legend"."""
    def __init__(self):
        super().__init__()
        self.depth = 0
        self.classes = set()
        self.found = False

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if self.depth == 0 and a.get("id") == "about_legend":
            self.depth = 1
            self.found = True
        elif self.depth:
            if tag not in ("br", "img", "input", "wbr"):
                self.depth += 1
        else:
            return
        self.classes.update((a.get("class") or "").split())
        # an SVG sample: <path class="jarc taken">
        if tag in ("path", "line", "polygon"):
            self.classes.update((a.get("class") or "").split())

    def handle_startendtag(self, tag, attrs):
        if self.depth:
            self.classes.update((dict(attrs).get("class") or "").split())

    def handle_endtag(self, tag):
        if self.depth and tag not in ("br", "img", "input", "wbr"):
            self.depth -= 1


def main():
    css = (site / "style.css").read_text()
    html_text = (site / "index.html").read_text()
    js = "\n".join(p.read_text() for p in sorted(site.glob("*.js")))
    problems = []
    lg = Legend()
    lg.feed(html_text)
    if not lg.found:
        sys.exit('check-legend: index.html has no element with id="about_legend" (the About tab\'s legend)')
    cls = colour_classes(css)
    for c in sorted(cls):
        if c in CHROME or c in lg.classes:
            continue
        problems.append(f".{c} ({cls[c][0]}) has a colour or mark but no sample in the legend and no CHROME reason")
    for c in sorted(CHROME):
        if c not in cls:
            problems.append(f"CHROME lists .{c}, which style.css no longer colours: drop it")
    # the <mark> element of the listings (bytes that differ)
    if "<mark>" not in html_text.split('id="about_legend"', 1)[1].split("</section>", 1)[0]:
        problems.append("the legend has no <mark> sample (yellow bytes)")
    # colour tokens: each one used somewhere
    root = re.search(r":root\s*\{([^}]*)\}", css).group(1)
    tokens = re.findall(r"--([\w-]+)\s*:", root)
    body = css.replace(root, "")
    for t in tokens:
        if f"var(--{t})" not in body and f'"{t}"' not in js and f"--{t}" not in js and f"'{t}'" not in js:
            problems.append(f"the colour token --{t} is defined but nothing uses it")
    if problems:
        sys.exit("check-legend:\n  " + "\n  ".join(problems))
    print(f"check-legend: {len(cls)} coloured classes, {len(cls) - len(CHROME)} in the legend, {len(CHROME)} page furniture")


if __name__ == "__main__":
    main()
