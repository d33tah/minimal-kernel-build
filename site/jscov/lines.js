// Which source lines ran, from the raw V8 ranges themselves (report.js).
//
// MCR marks a line "partly run" (count "1/n", which report.js reads as
// run) whenever no single unrun range covers all of it, also when nothing
// on it ran: `case 0x12: st.push(x); break; // dup` (the trailing comment
// is outside the unrun range) or `case 0x0e: case 0x0f: return p + 8;`
// (two unrun ranges, one per case clause). And it maps the bundle's
// ranges to the sources by their ends, which are not tokens, so a line
// that ran can come out unrun. Here a line ran when a byte of code on it
// (not blank, not a comment) has a count above 0 under V8's own rule (the
// innermost range holding a byte gives its count); for the bundle each
// byte of code counts for the source line its source map segment names.
// A line that no code byte of any copy (bundle, Worker, unbundled script)
// lands on (a lone `}`: esbuild maps no token there) gets no verdict here:
// report.js keeps MCR's for it.

"use strict";

const acorn = require("acorn");

const B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const B64VAL = new Int8Array(128).fill(-1);
for (let k = 0; k < B64.length; k++)
	B64VAL[B64.charCodeAt(k)] = k;

// A source map's "mappings": per generated line, its segments as flat
// [column, source, line, column] (0-based), in column order.
function decodeMappings(mappings) {
	const out = [];
	let src = 0, line = 0, col = 0, i = 0;
	const n = mappings.length;
	let cur = [], gen = 0;
	const field = () => {
		let v = 0, shift = 0, d;
		do {
			d = B64VAL[mappings.charCodeAt(i++)];
			v += (d & 31) * 2 ** shift;
			shift += 5;
		} while (d & 32);
		return v & 1 ? -(v - 1) / 2 : v / 2;
	};
	while (i <= n) {
		const c = i < n ? mappings[i] : ";";
		if (c === ";") {
			out.push(cur);
			cur = [];
			gen = 0;
			i++;
			if (i > n)
				break;
			continue;
		}
		if (c === ",") {
			i++;
			continue;
		}
		gen += field();
		if (i < n && mappings[i] !== "," && mappings[i] !== ";") {
			src += field();
			line += field();
			col += field();
			if (i < n && mappings[i] !== "," && mappings[i] !== ";")
				field(); // a name
			cur.push(gen, src, line, col);
		}
	}
	return out;
}

// The count of every character under V8's rule: ranges painted from the
// longest to the shortest, so a nested range (a block, an inner function)
// wins over the one around it.
function paint(len, functions) {
	const counts = new Float64Array(len);
	const all = [];
	for (const fn of functions)
		for (const r of fn.ranges)
			all.push(r);
	all.sort((x, y) => (y.endOffset - y.startOffset) - (x.endOffset - x.startOffset) || x.startOffset - y.startOffset);
	for (const r of all)
		counts.fill(r.count, Math.max(0, r.startOffset), Math.min(len, r.endOffset));
	return counts;
}

// Where the comments of a script are (true at every character inside one),
// or null when acorn cannot parse it.
function commentMask(source) {
	const mask = new Uint8Array(source.length);
	const mark = (block, text, start, end) => mask.fill(1, start, end);
	for (const sourceType of ["module", "script"]) {
		try {
			acorn.parse(source, { ecmaVersion: "latest", sourceType, allowHashBang: true, onComment: mark });
			return mask;
		} catch (e) {
			mask.fill(0);
		}
	}
	return null;
}

// MKB_JSCOV_LINE=file:line prints the bundle's bytes that count for it.
const DEBUG = process.env.MKB_JSCOV_LINE || "";

// The characters that count: those of words, numbers and strings. Not
// blanks, and not punctuation: a statement's `;` or a call's `)` after a
// part that never ran belongs to the range around it (which ran), and
// would make `a || neverRun(x);` a line that ran.
const wordy = (c) => (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122) || c === 95 || c === 36 ||
	c === 34 || c === 39 || c === 96 || c > 127;

// What the copies saw of a line, as bits: RAN (a copy ran a word of it),
// BUNDLE (the bundle has a word of it), DIRECT (an unbundled copy has).
const RAN = 1, BUNDLE = 2, DIRECT = 4;

// Merge one copy's verdict into `into` (file -> Map(line -> bits)).
function note(into, file, line, ran, from) {
	const m = into[file] || (into[file] = new Map());
	m.set(line, (m.get(line) || 0) | from | (ran ? RAN : 0));
}

// The verdict on a line: true (ran), false (no copy ran it), or null (no
// word of it to judge by). A file in the bundle is judged "not run" only
// where the bundle has a word of the line: a Worker that never runs a
// function says nothing of the page's copy (esbuild maps no `continue`,
// so the page's run of one would be lost).
function verdict(bits, inBundle) {
	if (!bits)
		return null;
	if (bits & RAN)
		return true;
	return !inBundle || bits & BUNDLE ? false : null;
}

// A script that ran as it is (a Worker's, an unbundled page script).
function directLines(into, file, source, functions) {
	const mask = commentMask(source);
	if (!mask)
		return;
	const counts = paint(source.length, functions);
	let line = 1;
	for (let i = 0; i < source.length; i++) {
		const c = source.charCodeAt(i);
		if (c === 10) {
			line++;
			continue;
		}
		if (wordy(c) && !mask[i])
			note(into, file, line, counts[i] > 0, DIRECT);
	}
}

// The bundle: the words from the one a map segment starts at (up to the
// next punctuation) count for the source line the segment names. Words
// after punctuation without a segment of their own (esbuild maps no
// `else`, for one) are left out: the segment before them may be of another
// line.
function bundleLines(into, source, map, functions, keep) {
	const mask = commentMask(source);
	if (!mask)
		return;
	const counts = paint(source.length, functions);
	const lines = decodeMappings(map.mappings);
	let gl = 0, col = 0, seg = -4, word = false, lead = false, last = "";
	let segs = lines[0] || [];
	for (let i = 0; i < source.length; i++) {
		const c = source.charCodeAt(i);
		if (c === 10) {
			gl++;
			col = 0;
			seg = -4;
			word = lead = false;
			segs = lines[gl] || [];
			continue;
		}
		// A segment starts the word at its column, or after blanks there
		// (esbuild's segment of a line's first statement is at column 0).
		// One that repeats the place of the segment before it (esbuild
		// starts a line with one) starts nothing: `else` after a line.
		while (seg + 4 < segs.length && segs[seg + 4] <= col) {
			seg += 4;
			const at = segs[seg + 1] + ":" + segs[seg + 2] + ":" + segs[seg + 3];
			lead = at !== last;
			last = at;
		}
		col++;
		if (c === 32 || c === 9)
			continue;
		if (!wordy(c))
			lead = word = false;
		else if (lead) {
			lead = false;
			word = true;
		}
		if (seg < 0 || !word || mask[i])
			continue;
		const file = map.sources[segs[seg + 1]];
		if (DEBUG === file + ":" + (segs[seg + 2] + 1))
			console.log("lines.js:", DEBUG, "bundle offset", i, JSON.stringify(source[i]), "count", counts[i]);
		if (keep(file))
			note(into, file, segs[seg + 2] + 1, counts[i] > 0, BUNDLE);
	}
}

module.exports = { decodeMappings, paint, commentMask, directLines, bundleLines, verdict };
