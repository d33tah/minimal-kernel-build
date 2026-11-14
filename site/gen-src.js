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
const { kgenSrc } = require("./kgen.js");

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

// The generation itself is kgen.js kgenSrc (the page's Worker runs it for
// an imported vmlinux and a picked source tree).
async function main() {
	for (const f of fs.existsSync(outDir) ? fs.readdirSync(outDir) : [])
		fs.rmSync(path.join(outDir, f), { recursive: true });
	const r = await kgenSrc((name) => new Uint8Array(fs.readFileSync(path.join(dwarfDir, name))), (p) => {
		const src = path.join(root, p);
		return fs.existsSync(src) && fs.statSync(src).isFile() ? fs.readFileSync(src) : null;
	}, {
		origin, notes,
		put: (name, data) => {
			fs.mkdirSync(path.dirname(path.join(outDir, name)), { recursive: true });
			fs.writeFileSync(path.join(outDir, name), data);
		},
		gzip: (data) => zlib.gzipSync(data, { level: 9 }),
		log: (text) => console.log(outDir + ": " + text),
	});
	if (r.pastEnd.length) {
		console.error(`${r.pastEnd.length} files are shorter than the lines the DWARF names, not the tree the kernel was built from: ` +
			r.pastEnd.slice(0, 10).join(", "));
		process.exit(1);
	}
}

main().catch((err) => { console.error(err); process.exit(1); });
