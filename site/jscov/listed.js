// site/jscov-uncovered.tsv: why lines of the page's scripts never run in
// the e2e tests. One row per block:
//
//     file<TAB>line ranges<TAB>code<TAB>reason
//
// "code" is the first 8 hex digits of the SHA-256 of the line before the
// block and the block's lines (each trimmed, joined by "\n"; codeOf), so a
// row knows its code: when lines above
// it are added or removed, relist.js finds the block again and fixes the
// line numbers; when the block itself changed, the row needs a person.
// A reason starting with "sometimes:" names lines that run in some CI runs
// and not in others (timing); they are not reported when they run.
// A reason starting with "TODO" is a candidate row written by report.js
// and fails the check until a person writes the reason (or a test).
// Lines starting with "#" are comments. No dependencies (Node only).

"use strict";

const crypto = require("crypto");
const fs = require("fs");

// "a-b,c" -> Set of a..b and c
function lineSet(text) {
	const out = new Set();
	for (const part of String(text).split(",").map((x) => x.trim()).filter(Boolean)) {
		const [lo, hi] = part.split("-").map(Number);
		for (let n = lo; n <= (hi || lo); n++)
			out.add(n);
	}
	return out;
}

// [numbers] -> "a-b,c"
function ranges(nums) {
	const out = [];
	for (const n of [...nums].sort((x, y) => x - y)) {
		const last = out[out.length - 1];
		if (last && last[1] === n - 1)
			last[1] = n;
		else
			out.push([n, n]);
	}
	return out.map(([a, b]) => (a === b ? String(a) : `${a}-${b}`)).join(",");
}

// The code hash of the lines `nums` (1-based) of a source split in lines,
// and of the line before them (so that a lone "return;" is told apart from
// the other ones); null when a line is past the end.
function codeOf(lines, nums) {
	const sorted = [...nums].sort((x, y) => x - y);
	if (!sorted.length || sorted[0] < 1 || sorted[sorted.length - 1] > lines.length)
		return null;
	const text = [sorted[0] > 1 ? lines[sorted[0] - 2].trim() : ""];
	for (const n of sorted) {
		if (n < 1 || n > lines.length)
			return null;
		text.push(lines[n - 1].trim());
	}
	return crypto.createHash("sha256").update(text.join("\n")).digest("hex").slice(0, 8);
}

function splitLines(source) {
	return source.split("\n");
}

// Rows of the file in order: { file, lines (Set), code, reason, at (line
// number in the .tsv) }, and the comment lines that come first (header).
function readListed(path) {
	const rows = [], header = [];
	fs.readFileSync(path, "utf8").split("\n").forEach((line, k) => {
		if (!line.trim())
			return;
		if (line.startsWith("#")) {
			if (!rows.length)
				header.push(line);
			return;
		}
		const [file, lines, code, reason, extra] = line.split("\t");
		if (!file || !lines || !/^[0-9a-f]{8}$/.test(code || "") || !reason || !reason.trim() || extra !== undefined)
			throw new Error(`${path}:${k + 1}: want file<TAB>line ranges<TAB>8 hex digits of code<TAB>reason, got ${JSON.stringify(line.slice(0, 80))}`);
		rows.push({ file, lines: lineSet(lines), code, reason, at: k + 1 });
	});
	return { header, rows };
}

function writeListed(path, header, rows) {
	const body = rows.map((r) => `${r.file}\t${ranges(r.lines)}\t${r.code}\t${r.reason}`);
	fs.writeFileSync(path, [...header, ...body].join("\n") + "\n");
}

const isTodo = (r) => /^TODO\b/.test(r.reason);
const isSometimes = (r) => /^sometimes:/.test(r.reason);

// Find each row's code in `sources` ({ file: text }). A row whose lines
// still hold its code stays. Otherwise the same shape of lines (the same
// gaps) is searched for the code, nearest to where the previous row of the
// file moved (lines added above a block move it and the rows below it
// alike). Returns { moved: [{row, from}], lost: [row] }; moved rows get
// their new lines.
function relocate(rows, sources) {
	const moved = [], lost = [];
	const shift = {};
	// Lines a row still holds are taken: a moved row whose code is the same
	// (two identical `return;` blocks) must not land on them.
	const taken = {};
	for (const r of rows)
		if (r.file in sources && codeOf(splitLines(sources[r.file]), [...r.lines].sort((x, y) => x - y)) === r.code)
			for (const n of r.lines)
				(taken[r.file] ||= new Set()).add(n);
	const free = (file, nums) => !nums.some((n) => taken[file] && taken[file].has(n));
	for (const r of rows) {
		if (!(r.file in sources)) {
			lost.push(r);
			continue;
		}
		const lines = splitLines(sources[r.file]);
		const nums = [...r.lines].sort((x, y) => x - y);
		if (codeOf(lines, nums) === r.code) {
			shift[r.file] = 0;
			continue;
		}
		const rel = nums.map((n) => n - nums[0]);
		const want = nums[0] + (shift[r.file] || 0);
		// Outward from `want`: the first match is the nearest (above first).
		let best = null;
		for (let d = 0; best === null && d <= lines.length; d++)
			for (const s of d ? [want - d, want + d] : [want])
				if (best === null && s >= 1 && s + rel[rel.length - 1] <= lines.length &&
					codeOf(lines, rel.map((x) => s + x)) === r.code && free(r.file, rel.map((x) => s + x)))
					best = s;
		if (best === null) {
			lost.push(r);
			continue;
		}
		const from = ranges(nums);
		shift[r.file] = best - nums[0];
		r.lines = new Set(rel.map((d) => best + d));
		for (const n of r.lines)
			(taken[r.file] ||= new Set()).add(n);
		moved.push({ row: r, from });
	}
	return { moved, lost };
}

module.exports = { lineSet, ranges, codeOf, splitLines, readListed, writeListed, relocate, isTodo, isSometimes };
