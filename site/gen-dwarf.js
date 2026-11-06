#!/usr/bin/env node
"use strict";
// Line tables and inline chains of a -g ELF as address shards for the page,
// and its variables with their types.
//
//   site/gen-dwarf.js ELF OUTDIR
//       OUTDIR/index.json and OUTDIR/<hex>.bin, one shard per 64 KiB of
//       physical address space that has code (dwarf.js: dwarfEncode);
//       OUTDIR/types.json, the variables at fixed addresses and the types
//       they reach (dwarf.js: DwarfTypes)
//   site/gen-dwarf.js --lookup OUTDIR < addresses
//       one JSON line per physical address (hex or decimal) on stdin:
//       the frames the page shows there (test_dwarf.py compares them with
//       llvm-symbolizer --inlines)
//   site/gen-dwarf.js --where OUTDIR < addresses
//       one JSON line per physical address: the variable and member path
//       there (dwarf.js dwWhere) with the leaf's type (test_dwarf.py --types
//       checks them with gdb)
//
// Reads only the .debug_* sections it needs, a unit at a time (a distro
// vmlinux is ~300 MB).

const fs = require("fs");
const path = require("path");
const { DwarfReader, DwarfTypes, dwarfElf, dwarfSectionOf, dwarfSections, dwarfTable, dwarfSlice, dwarfFrames, dwarfEncode, dwarfDecode,
	dwWhere, dwTypeName, dwTypeSize } = require("./dwarf.js");

const SHARD_BITS = 16;

function reader(file) {
	const fd = fs.openSync(file, "r");
	return (off, len) => {
		const b = Buffer.alloc(len);
		fs.readSync(fd, b, 0, len, off);
		return new Uint8Array(b.buffer, b.byteOffset, len);
	};
}

function generate(elf, outDir) {
	const t0 = Date.now();
	const read = reader(elf);
	const e = dwarfElf(read);
	const sec = dwarfSections(e, read);
	if (!sec)
		throw new Error(elf + " has no .debug_info");
	// Every unit is decoded, so .debug_aranges (an address index) is not needed.
	const dw = new DwarfReader({ ...sec, aranges: null });
	const units = [];
	const us = dw.units();
	let nrows = 0, nranges = 0;
	for (let i = 0; i < us.length; i++) {
		if (us[i].unitType !== 1 && us[i].unitType !== 3)
			continue;
		const u = dw.decodeUnit(i);
		nrows += u.rows.length / 4;
		nranges += u.ranges.length / 6;
		units.push(u);
	}
	const t1 = Date.now();
	const table = dwarfTable(units, e.toPhys);
	units.length = 0;
	const t2 = Date.now();
	fs.mkdirSync(outDir, { recursive: true });
	for (const f of fs.readdirSync(outDir))
		if (/^[0-9a-f]+\.bin$|^index\.json$/.test(f))
			fs.unlinkSync(path.join(outDir, f));
	const shards = new Set();
	for (let k = 0; k < table.la.length; k++)
		if (table.lf[k] >= 0)
			shards.add(Math.floor(table.la[k] / 2 ** SHARD_BITS));
	for (let k = 0; k < table.ra.length; k++)
		for (let s = Math.floor(table.ra[k] / 2 ** SHARD_BITS); s <= Math.floor((table.rh[k] - 1) / 2 ** SHARD_BITS); s++)
			shards.add(s);
	const list = [...shards].sort((a, b) => a - b);
	let bytes = 0;
	for (const s of list) {
		const b = dwarfEncode(dwarfSlice(table, s * 2 ** SHARD_BITS, (s + 1) * 2 ** SHARD_BITS));
		fs.writeFileSync(path.join(outDir, s.toString(16) + ".bin"), b);
		bytes += b.length;
	}
	const t3 = Date.now();
	const ty = types(dw, e);
	const tj = JSON.stringify(ty);
	fs.writeFileSync(path.join(outDir, "types.json"), tj);
	console.log(`${outDir}/types.json: ${ty.vars.length} variables, ${ty.types.length} types, ${tj.length} bytes; ${Date.now() - t3} ms`);
	const index = { format: 1, shardBits: SHARD_BITS, shards: list, units: us.length,
		version: [...new Set(us.map((u) => u.ver))], lineRows: table.la.length, ranges: table.ra.length,
		source: path.basename(elf), types: "types.json", variables: ty.vars.length, typeCount: ty.types.length, typeBytes: tj.length };
	fs.writeFileSync(path.join(outDir, "index.json"), JSON.stringify(index));
	console.log(`${outDir}: ${us.length} units, ${nrows} line rows -> ${table.la.length}, ${nranges} function/inline ranges, ` +
		`${list.length} shards, ${bytes} bytes; decode ${t1 - t0} ms, table ${t2 - t1} ms, write ${Date.now() - t2} ms`);
}

// Variables and their types, every unit (dwarf.js DwarfTypes).
function types(dw, e) {
	const acc = new DwarfTypes(dw);
	const sections = [];
	const secOf = dwarfSectionOf(e, sections);
	const us = dw.units();
	for (let i = 0; i < us.length; i++)
		if (us[i].unitType === 1 || us[i].unitType === 3)
			acc.addUnit(dw.decodeData(i), e.toPhys, secOf);
	return acc.finish(sections);
}

function where(dir) {
	const T = JSON.parse(fs.readFileSync(path.join(dir, "types.json"), "utf8"));
	const out = [];
	for (const w of fs.readFileSync(0, "utf8").split(/\s+/).filter(Boolean)) {
		const pa = Number(w);
		const r = dwWhere(T, pa);
		out.push(JSON.stringify(r ? { pa, var: r.var[0], scope: r.var[6] || "", path: r.path, note: r.note, type: dwTypeName(T, r.type),
			size: dwTypeSize(T, r.type), offset: r.offset, unions: r.unions, varType: dwTypeName(T, r.var[2]), varSize: r.var[3] } : { pa }));
	}
	process.stdout.write(out.join("\n") + "\n");
}

function lookup(dir) {
	const index = JSON.parse(fs.readFileSync(path.join(dir, "index.json"), "utf8"));
	const cache = new Map();
	const shard = (s) => {
		if (!cache.has(s))
			cache.set(s, index.shards.includes(s) ? dwarfDecode(new Uint8Array(fs.readFileSync(path.join(dir, s.toString(16) + ".bin")))) : null);
		return cache.get(s);
	};
	const out = [];
	for (const w of fs.readFileSync(0, "utf8").split(/\s+/).filter(Boolean)) {
		const pa = Number(w);
		const t = shard(Math.floor(pa / 2 ** index.shardBits));
		out.push(JSON.stringify({ pa, frames: t ? dwarfFrames(t, pa) : null }));
	}
	process.stdout.write(out.join("\n") + "\n");
}

const args = process.argv.slice(2);
if (args[0] === "--lookup" && args.length === 2)
	lookup(args[1]);
else if (args[0] === "--where" && args.length === 2)
	where(args[1]);
else if (args.length === 2)
	generate(args[0], args[1]);
else {
	console.error("usage: gen-dwarf.js ELF OUTDIR | gen-dwarf.js --lookup|--where OUTDIR < addresses");
	process.exit(2);
}
