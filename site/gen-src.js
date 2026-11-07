#!/usr/bin/env node
"use strict";
// The C source files a kernel's DWARF names, for the page's source view.
//
//   site/gen-src.js DWARF_DIR SRC_ROOT OUTDIR --origin TEXT [--note PATH=TEXT ...]
//       DWARF_DIR is gen-dwarf.js's output; every path its line rows,
//       inlined-call sites and variables (types.json: where each is
//       defined) name is looked up under SRC_ROOT (the tree the
//       kernel was built from) and written gzipped to OUTDIR/<path>.gz.
//       OUTDIR/index.json lists them, says why the others are missing and
//       carries --origin (where the tree comes from) and --note (how a file
//       was made, e.g. a regenerated header).
//
// Fails when a line the DWARF names is past the end of its file: the tree
// is not the one the kernel was built from. Also counts the inlined calls
// whose call line names the inlined function (a version mismatch makes
// that rare; macros and multi-line calls keep it below 100%).

const fs = require("fs");
const path = require("path");
const zlib = require("zlib");
const { dwarfDecode } = require("./dwarf.js");

function usage() {
	console.error("usage: gen-src.js DWARF_DIR SRC_ROOT OUTDIR --origin TEXT [--note PATH=TEXT ...]");
	process.exit(2);
}

const args = process.argv.slice(2);
const pos = [], notes = {};
let origin = null;
for (let k = 0; k < args.length; k++) {
	if (args[k] === "--origin")
		origin = args[++k];
	else if (args[k] === "--note") {
		const m = /^([^=]+)=(.+)$/.exec(args[++k] || "");
		if (!m)
			usage();
		notes[m[1]] = m[2];
	} else
		pos.push(args[k]);
}
if (pos.length !== 3 || !origin)
	usage();
const [dwarfDir, root, outDir] = pos;

// path -> highest line named; inlined calls: [call file, call line, callee].
const maxLine = new Map(), calls = [];
const index = JSON.parse(fs.readFileSync(path.join(dwarfDir, "index.json"), "utf8"));
for (const s of index.shards) {
	const t = dwarfDecode(new Uint8Array(fs.readFileSync(path.join(dwarfDir, s.toString(16) + ".bin"))));
	const note = (f, l) => {
		if (f >= 0)
			maxLine.set(t.strings[f], Math.max(maxLine.get(t.strings[f]) || 0, l));
	};
	for (let k = 0; k < t.la.length; k++)
		note(t.lf[k], t.ll[k]);
	for (let k = 0; k < t.ra.length; k++) {
		note(t.rf[k], t.rl[k]);
		// A range is in every shard it overlaps: count it in the one it starts in.
		if (t.rf[k] >= 0 && t.rl[k] && Math.floor(t.ra[k] / 2 ** index.shardBits) === s)
			calls.push([t.strings[t.rf[k]], t.rl[k], t.strings[t.rn[k]]]);
	}
}
// Where the variables are defined ("defined at" opens the file), also
// files without code (data only).
if (index.types) {
	const T = JSON.parse(fs.readFileSync(path.join(dwarfDir, index.types), "utf8"));
	const note = (f, l) => maxLine.set(T.files[f], Math.max(maxLine.get(T.files[f]) || 0, l));
	for (const v of T.vars)
		if (T.files && v.length > 7 && v[7] >= 0) {
			note(v[7], v[8]);
			for (const [f, l] of v[9] || [])
				note(f, l);
		}
}

for (const f of fs.existsSync(outDir) ? fs.readdirSync(outDir) : [])
	fs.rmSync(path.join(outDir, f), { recursive: true });
const files = [], missing = {}, text = new Map();
let raw = 0, packed = 0;
const pastEnd = [];
for (const p of [...maxLine.keys()].sort()) {
	if (p.startsWith("/") || p.split("/").includes("..")) {
		missing[p] = "outside the kernel source tree (a compiler or system header)";
		continue;
	}
	const src = path.join(root, p);
	if (!fs.existsSync(src) || !fs.statSync(src).isFile()) {
		missing[p] = "not in the source tree: made during the build";
		continue;
	}
	const b = fs.readFileSync(src);
	const lines = b.toString("utf8").split("\n");
	if (maxLine.get(p) > lines.length)
		pastEnd.push(`${p}:${maxLine.get(p)} (${lines.length} lines)`);
	text.set(p, lines);
	const z = zlib.gzipSync(b, { level: 9 });
	fs.mkdirSync(path.dirname(path.join(outDir, p)), { recursive: true });
	fs.writeFileSync(path.join(outDir, p + ".gz"), z);
	files.push(p);
	raw += b.length;
	packed += z.length;
}
let named = 0, checked = 0;
for (const [f, l, fn] of calls) {
	const lines = text.get(f);
	if (!lines || !fn)
		continue;
	checked++;
	// The call may span lines; DWARF names one of them.
	if (lines.slice(Math.max(0, l - 3), l + 2).some((s) => s.includes(fn)))
		named++;
}
fs.writeFileSync(path.join(outDir, "index.json"), JSON.stringify({ format: 1, origin, files, missing, notes, bytes: raw, gzipBytes: packed }));
console.log(`${outDir}: ${files.length} files (${raw} bytes, ${packed} gzipped), ${Object.keys(missing).length} missing` +
	(Object.keys(missing).length ? ": " + Object.keys(missing).join(", ") : "") +
	`; inlined calls whose call line names the function: ${named}/${checked}`);
if (pastEnd.length) {
	console.error(`${pastEnd.length} files are shorter than the lines the DWARF names, not the tree the kernel was built from: ` +
		pastEnd.slice(0, 10).join(", "));
	process.exit(1);
}
