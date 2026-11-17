"use strict";

// C locations as links (file:line) for every view, and keeping the
// keyboard focus on one when a view is drawn again (source.js opens them).

import { $, escHtml } from "./util.js";
import { dbgHas } from "./dbginfo.js";

const showSource = () => dbgHas() && $("show_src").checked;

const srcName = (f) => f.file && f.line ? f.file.replace(/^.*\//, "") + ":" + f.line : "no line information";

// Every view showing C lines colours the location (file:line, line number)
// with .srcloc and the C text with .srctext (style.css).
const srcLocHtml = (s) => '<span class="srcloc">' + escHtml(s) + "</span>";

function srcLocEl(s) {
	const e = document.createElement("span");
	e.className = "srcloc";
	e.textContent = s;
	return e;
}

// A file:line that opens the Source panel at that line (srcOpenAt), as an
// element or as HTML for the listings built as strings. Every view showing
// a C location outside the Source panel makes it with one of these two;
// initSource handles a click, Enter, Space or tap on any .srclink once for
// all of them. pa (-1: none) is an address whose code belongs to that line
// or to the function declared there. text defaults to the short name.
const srcLinkTitle = (path, line) => "Open " + path + ":" + line + " in the Source panel";

function srcLinkEl(path, line, text, pa = -1) {
	const b = document.createElement("button");
	b.type = "button";
	b.className = "srcloc srclink";
	b.textContent = text || srcName({ file: path, line });
	b.dataset.tip = srcLinkTitle(path, line);
	Object.assign(b.dataset, { file: path, line, pa });
	return b;
}

const srcLinkHtml = (path, line, text, pa = -1) => '<button type="button" class="srcloc srclink" data-file="' + escHtml(path) +
	'" data-line="' + line + '" data-pa="' + pa + '" data-tip="' + escHtml(srcLinkTitle(path, line)) + '">' + escHtml(text || srcName({ file: path, line })) + "</button>";

// Views are drawn again when DWARF or source text arrives and at every
// stop, which replaces their links. srcFocusKeep(draw) keeps the keyboard
// focus on the same file:line link (by its place among equal ones), and
// srcLinkAgain finds the drawn-again copy of a link marked by srcLinkMark.
const srcLinksLike = (a) => [...document.querySelectorAll(".srclink")].filter((e) =>
	e.dataset.file === a.dataset.file && e.dataset.line === a.dataset.line && e.dataset.pa === a.dataset.pa);

function srcLinkMark(a) {
	a.srcNth = srcLinksLike(a).indexOf(a);
}

function srcLinkAgain(a) {
	return !a || a.isConnected ? a : srcLinksLike(a)[a.srcNth] || null;
}

function srcFocusKeep(draw) {
	const a = document.activeElement;
	if (!a || !a.classList || !a.classList.contains("srclink"))
		return draw();
	srcLinkMark(a);
	const out = draw();
	const b = srcLinkAgain(a);
	if (b && b !== a)
		b.focus({ preventScroll: true });
	return out;
}

export { showSource, srcName, srcLocHtml, srcLocEl, srcLinkTitle, srcLinkEl, srcLinkHtml, srcLinkMark,
	srcLinkAgain, srcFocusKeep };
