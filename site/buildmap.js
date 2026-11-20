"use strict";
// Places across builds (plan E8). A kernel rebuilt in the page (rebuild.jsx)
// or hot patched (hotpatch.jsx) has other addresses and other line numbers
// than the build a bookmark, a snapshot, a Run to target or a coverage
// record was made on. They are kept by what stays: a function's name and
// the offset into it, a file's path and line, and the text of that line
// with the version of the file (a hash of its text) it was counted in. A
// line moves to another build through the diff of the two texts of its
// file (edpatch.js's, Myers), so lines above an edit keep their number,
// lines below move by what the edit added or removed, and a changed or
// removed line says so. Without the old text (a reload forgot the build's
// edits) the line's own text is looked for near its old number.
//
// A kernel's family is the kernel it was built from: a shipped kernel is
// its own, "hello (rebuilt here)" is hello's (image.from). Each Rebuild and
// boot of a family is a numbered build of it (image.build), so a record can
// say which build it belongs to.

import { image, initrd } from "./page.js";
import { edDiff } from "./edpatch.js";

const BLD = {
	n: new Map(),       // family -> builds made in this page
	texts: new Map(),   // text hash -> text (the versions seen in this page)
	maps: new Map(),    // "a>b" hashes -> Int32Array line map
};
const BLD_TEXTS_MAX = 400;
const BLD_MAPS_MAX = 64;
const BLD_NEAR = 400;   // lines searched both ways for a line's own text

const RB_SUFFIX = " (rebuilt here)";

// FNV-1a over the UTF-16 code units, 32 bits, with the length: names a
// version of a file's text (not a security hash: two texts that collide
// map a line wrongly, nothing else).
function textHash(s) {
	let h = 0x811c9dc5;
	for (let i = 0; i < s.length; i++) {
		h ^= s.charCodeAt(i);
		h = Math.imul(h, 0x01000193);
	}
	return (h >>> 0).toString(16).padStart(8, "0") + ":" + s.length.toString(36);
}

// A text kept for mapping lines out of it; its hash.
function textKeep(s) {
	const h = textHash(s);
	if (!BLD.texts.has(h)) {
		BLD.texts.set(h, s);
		if (BLD.texts.size > BLD_TEXTS_MAX)
			BLD.texts.delete(BLD.texts.keys().next().value);
	}
	return h;
}

const textOf = (h) => BLD.texts.get(h);

// The family of a kernel (with its initrd, like snapKernelName).
const buildFamily = (img = image, ird = initrd) => (img ? (img.from || img.name) + (ird ? " + " + ird.name : "") : "");

// A record's family (a bookmark, a snapshot, a run): its kernel's name
// without the rebuilt suffix (snapKernelName: "hello (rebuilt here)" and
// "hello" are hello's).
const recFamily = (r) => String(r.kernel || "").replace(RB_SUFFIX, "");

// A new build of from with these edits ({ path: text }): its number and
// what it was made from.
function buildNew(from, edits) {
	const fam = buildFamily({ name: from }, initrd);
	const n = (BLD.n.get(fam) || 0) + 1;
	BLD.n.set(fam, n);
	const files = {};
	for (const [p, t] of Object.entries(edits))
		files[p] = textKeep(t);
	return { n, from, at: Date.now(), files };
}

// What a build is called: "hello (shipped)" or "build 2 of hello (made
// here at 14:05:09 from your boot/hello.c)". b: image.build, or null for
// the kernel as the site ships it.
function buildLabel(b, name) {
	if (!b)
		return name + " (shipped)";
	const files = Object.keys(b.files || {});
	return "build " + b.n + " of " + b.from + " (made here at " + new Date(b.at).toLocaleTimeString("en-GB") +
		(files.length ? " from your " + files.join(", ") : " with no edit") + ")";
}

const imageLabel = (img = image) => (img ? buildLabel(img.build || null, img.from || img.name) : "");

// The new line of each line of text a in text b: m[l] for l in 1..lines
// of a, 0 where the line was changed or removed.
function lineMap(a, b) {
	const ha = textKeep(a), hb = textKeep(b), key = ha + ">" + hb;
	let m = BLD.maps.get(key);
	if (m)
		return m;
	const A = a.split("\n"), B = b.split("\n");
	m = new Int32Array(A.length + 1);
	let i = 0, j = 0;
	for (const [k] of edDiff(A, B)) {
		if (k === " ")
			m[++i] = ++j;
		else if (k === "-")
			m[++i] = 0;
		else
			j++;
	}
	BLD.maps.set(key, m);
	if (BLD.maps.size > BLD_MAPS_MAX)
		BLD.maps.delete(BLD.maps.keys().next().value);
	return m;
}

// Line `line` through map m: { line, how } with how "same", "moved"
// (by the edit), "changed" (the line itself was edited or removed: the
// next line that stayed, else the one before), or null past the text.
function mapThrough(m, line) {
	if (line < 1 || line >= m.length)
		return null;
	if (m[line])
		return { line: m[line], how: m[line] === line ? "same" : "moved" };
	for (let l = line + 1; l < m.length; l++)
		if (m[l])
			return { line: Math.max(1, m[l] - 1), how: "changed" };
	for (let l = line - 1; l >= 1; l--)
		if (m[l])
			return { line: m[l] + 1, how: "changed" };
	return { line: 1, how: "changed" };
}

// Where line `line` of the file version `from` (a hash; its line's text
// `txt`) is in text `cur`: { line, how } (how as mapThrough, or "text":
// found by its own text, "kept": the same number, nothing to go by).
function mapLine(line, from, txt, cur) {
	const hc = textHash(cur);
	if (!from || from === hc)
		return { line, how: "same" };
	const old = textOf(from);
	if (old !== undefined)
		return mapThrough(lineMap(old, cur), line) || { line, how: "kept" };
	const L = cur.split("\n");
	if (typeof txt === "string" && txt.trim()) {
		const want = txt.trim();
		for (let d = 0; d <= BLD_NEAR; d++)
			for (const l of d ? [line - d, line + d] : [line])
				if (l >= 1 && l <= L.length && L[l - 1].trim() === want)
					return { line: l, how: d ? "text" : "same" };
	}
	return { line: Math.min(line, L.length), how: "kept" };
}

// The other direction for a Run to target typed against text a: the
// line of b. Same as mapLine with a known old text.
function moveLine(a, b, line) {
	return a === b ? { line, how: "same" } : mapThrough(lineMap(a, b), line) || { line, how: "kept" };
}

// The Debugger's Run to field (its breakpoint) when the text of files
// changes under it (a build, a hot patch): a file:line target of one of
// them moves with the edit. texts: Map(path -> [old text, new text]).
// Returns what the Debugger's status says, or "".
function moveRunTo(texts) {
	const el = document.getElementById("dbg_addr");
	const m = el ? /^(.*\D.*):(\d+)$/.exec(el.value.trim()) : null;
	if (!m)
		return "";
	const q = m[1].replace(/^\.?\//, "");
	const path = [...texts.keys()].find((p) => p === q || p.endsWith("/" + q));
	if (!path)
		return "";
	const [a, b] = texts.get(path);
	const r = moveLine(a, b, Number(m[2]));
	if (r.how === "same")
		return "";
	el.value = m[1] + ":" + r.line;
	return "Run to " + m[1] + ":" + m[2] + " is " + el.value + " now: " + (r.how === "moved" ? "the edit moved the line" :
		r.how === "changed" ? "the edit changed that line, this is the next line it kept" : "its place in the new text is not known") + ".";
}

export { BLD, RB_SUFFIX, moveRunTo, textHash, textKeep, textOf, buildFamily, recFamily, buildNew, buildLabel, imageLabel, lineMap,
	mapThrough, mapLine, moveLine };
