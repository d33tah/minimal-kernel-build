#!/usr/bin/env node
// Keep site/jscov-uncovered.tsv on the code it explains (no coverage run
// needed, no dependencies). Every row carries the hash of its lines; when
// lines were added or removed above a listed block, this finds the block
// again and rewrites the row's line numbers. A row whose block changed or
// is gone, or whose reason is still a TODO, is printed and the exit code
// is 1: read the code, then fix the row's lines and hash (--rehash FILE:LINES
// prints them), drop it, or write the reason.
//
// Usage: node site/jscov/relist.js [--check] [site/jscov-uncovered.tsv]
//        node site/jscov/relist.js --rehash app.js:120-123,125 [LIST]
// --check: write nothing, exit 1 when a row moved too (the site job runs this).

"use strict";

const fs = require("fs");
const path = require("path");
const L = require("./listed.js");

function main(argv) {
	let check = false, rehash = null;
	const rest = [];
	for (let i = 0; i < argv.length; i++) {
		if (argv[i] === "--check")
			check = true;
		else if (argv[i] === "--rehash")
			rehash = argv[++i];
		else if (argv[i].startsWith("--"))
			throw new Error("unknown option " + argv[i]);
		else
			rest.push(argv[i]);
	}
	const list = path.resolve(rest[0] || path.join(__dirname, "..", "jscov-uncovered.tsv"));
	const src = path.dirname(list);
	if (rehash) {
		const [file, nums] = rehash.split(":");
		const lines = L.splitLines(fs.readFileSync(path.join(src, file), "utf8"));
		const set = L.lineSet(nums);
		const code = L.codeOf(lines, set);
		if (!code)
			throw new Error(`${file} has ${lines.length} lines`);
		console.log(`${file}\t${L.ranges(set)}\t${code}\t`);
		return 0;
	}
	const { header, rows } = L.readListed(list);
	const sources = {};
	for (const f of new Set(rows.map((r) => r.file)))
		if (fs.existsSync(path.join(src, f)))
			sources[f] = fs.readFileSync(path.join(src, f), "utf8");
	const { moved, lost } = L.relocate(rows, sources);
	const todo = rows.filter(L.isTodo);
	for (const m of moved)
		console.log(`moved: ${m.row.file}:${m.from} -> ${L.ranges(m.row.lines)} (same code)`);
	for (const r of lost)
		console.log(`CHANGED: ${path.basename(list)} line ${r.at}: ${r.file}:${L.ranges(r.lines)} ` +
			(r.file in sources ? "no longer holds the listed code, and that code is nowhere else in the file"
				: "is no script of the site"));
	for (const r of todo)
		console.log(`TODO: ${path.basename(list)} line ${r.at}: ${r.file}:${L.ranges(r.lines)} has no reason yet`);
	if (!check && moved.length)
		L.writeListed(list, header, rows);
	const bad = lost.length + todo.length + (check ? moved.length : 0);
	console.log(`${rows.length} rows: ${moved.length} moved${check || !moved.length ? "" : " (rewritten)"}, ` +
		`${lost.length} changed, ${todo.length} TODO`);
	if (bad) {
		console.log([
			"",
			`FAIL: ${rest[0] || "site/jscov-uncovered.tsv"} does not match the code.`,
			moved.length && check ? "  Moved rows: run `node site/jscov/relist.js` and commit the list with the code." : "",
			lost.length ? "  Changed rows: look at the code. If those lines still never run in the tests, print the row's new" +
				" lines and hash with `node site/jscov/relist.js --rehash FILE:LINES` and keep the reason; if they" +
				" run now or are gone, delete the row." : "",
			todo.length ? "  TODO rows: add a test that runs those lines (best), or write why they cannot run." : "",
			"  README.md, section on e2e coverage, says how the list is kept.",
		].filter(Boolean).join("\n"));
		return 1;
	}
	return 0;
}

try {
	process.exit(main(process.argv.slice(2)));
} catch (e) {
	console.error(e.stack || String(e));
	process.exit(2);
}
