#!/usr/bin/env node
"use strict";
// Line tables and inline chains of a -g ELF as address shards for the page,
// and its variables with their types.
//
//   site/gen-dwarf.js ELF OUTDIR
//       OUTDIR/index.json and OUTDIR/<hex>.bin, one shard per 64 KiB of
//       physical address space that has code (dwarf.js: dwarfEncode);
//       OUTDIR/types.json, the variables at fixed addresses and the types
//       they reach (dwarf.js: DwarfTypes); OUTDIR/files.json, every source
//       file with its lines that have code and every function with its
//       lines and instructions, for the page's Coverage tab (see coverage())
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
const { spawn } = require("child_process");
const { DwarfReader, DwarfTypes, dwarfElf, dwarfSectionOf, dwarfSections, dwarfTable, dwarfSlice, dwarfFrames, dwarfEachLine, dwarfEncode, dwarfDecode,
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

async function generate(elf, outDir) {
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
	const insns = insnStarts(elf, e.toPhys);
	const cov = new Coverage(table);
	for (const s of list) {
		const slice = dwarfSlice(table, s * 2 ** SHARD_BITS, (s + 1) * 2 ** SHARD_BITS);
		const b = dwarfEncode(slice);
		fs.writeFileSync(path.join(outDir, s.toString(16) + ".bin"), b);
		bytes += b.length;
		cov.addShard(s, slice);
	}
	const fj = cov.finish(await insns);
	fs.writeFileSync(path.join(outDir, "files.json"), JSON.stringify(fj));
	console.log(`${outDir}/files.json: ${fj.files.length} files, ${fj.funcs.length} functions, ` +
		`${fj.files.reduce((n, f) => n + f[1], 0)} lines with code, ${fj.insns ? fj.insns.total + " instructions (" + fj.insns.inFunctions +
		" in functions)" : "no instruction counts (" + fj.insnsWhy + ")"}; ${Date.now() - t2} ms with the shards`);
	const t3 = Date.now();
	const ty = types(dw, e);
	const tj = JSON.stringify(ty);
	fs.writeFileSync(path.join(outDir, "types.json"), tj);
	console.log(`${outDir}/types.json: ${ty.vars.length} variables, ${ty.types.length} types, ${tj.length} bytes; ${Date.now() - t3} ms`);
	const index = { format: 1, shardBits: SHARD_BITS, shards: list, units: us.length,
		version: [...new Set(us.map((u) => u.ver))], lineRows: table.la.length, ranges: table.ra.length,
		source: path.basename(elf), files: "files.json", types: "types.json", variables: ty.vars.length, typeCount: ty.types.length, typeBytes: tj.length };
	fs.writeFileSync(path.join(outDir, "index.json"), JSON.stringify(index));
	console.log(`${outDir}: ${us.length} units, ${nrows} line rows -> ${table.la.length}, ${nranges} function/inline ranges, ` +
		`${list.length} shards, ${bytes} bytes; decode ${t1 - t0} ms, table ${t2 - t1} ms, write ${Date.now() - t2} ms`);
}

// The physical address of every instruction start of the ELF's code, from
// a linear sweep of llvm-objdump -d ($OBJDUMP), sorted; or { why } when
// it cannot run.
function insnStarts(elf, toPhys) {
	const tool = process.env.OBJDUMP || "llvm-objdump";
	return new Promise((resolve) => {
		const p = spawn(tool, ["-d", "--no-show-raw-insn", elf], { stdio: ["ignore", "pipe", "ignore"] });
		const out = [];
		let rest = "";
		p.stdout.setEncoding("latin1");
		p.stdout.on("data", (chunk) => {
			const lines = (rest + chunk).split("\n");
			rest = lines.pop();
			for (const l of lines) {
				const m = /^ *([0-9a-f]+): *\t/.exec(l);
				if (m) {
					const pa = toPhys(parseInt(m[1], 16));
					if (pa !== null)
						out.push(pa);
				}
			}
		});
		p.on("error", (err) => resolve({ why: tool + ": " + err.message }));
		p.on("close", (code) => {
			if (code !== 0)
				return resolve({ why: tool + " exited with " + code });
			const a = Float64Array.from(out).sort();
			resolve({ starts: a, tool: tool + " -d" });
		});
	});
}

// files.json for the page's Coverage tab. Code lines are counted the way
// the page counts what ran (dwarf.js dwarfEachLine over the shards):
//   files  [[path, lines with code, [shards holding them]], ...] sorted
//   funcs  [[name, file index or -1, lines, instructions, lo, length,
//            lo2, length2, ...], ...]: each DWARF function (an outermost
//          range; the parts of one function, e.g. GCC's .cold copies, are
//          one entry), its file (where most of its bytes' outermost frame
//          is), its lines with code in that file and its instruction starts
//   insns  { total, inFunctions, tool } (instruction starts in the image,
//          and in DWARF functions), or insnsWhy when not counted
class Coverage {
	constructor(table) {
		// Outermost ranges, sorted: a row belongs to the last one starting at
		// or below it that still covers it.
		this.fr = [];
		for (let k = 0; k < table.ra.length; k++)
			if (table.rd[k] === 0)
				this.fr.push([table.ra[k], table.rh[k], table.strings[table.rn[k]] || "?"]);
		this.fr.sort((x, y) => x[0] - y[0]);
		this.maxLen = this.fr.reduce((m, r) => Math.max(m, r[1] - r[0]), 0);
		this.lines = new Map();   // path -> Set of lines
		this.shards = new Map();  // path -> Set of shards
		this.fnLines = this.fr.map(() => new Set());   // "path\0line"
		this.fnBytes = this.fr.map(() => new Map());   // path -> bytes
	}

	rangeAt(pa) {
		const fr = this.fr;
		let a = 0, b = fr.length;
		while (a < b) { const m = (a + b) >> 1; if (fr[m][0] <= pa) a = m + 1; else b = m; }
		for (let q = a - 1; q >= 0 && fr[q][0] >= pa - this.maxLen; q--)
			if (fr[q][1] > pa)
				return q;
		return -1;
	}

	addShard(s, t) {
		dwarfEachLine(t, s * 2 ** SHARD_BITS, (s + 1) * 2 ** SHARD_BITS, (a, b, frames) => {
			const q = this.rangeAt(a);
			for (const f of frames) {
				if (!f.file || !f.line)
					continue;
				if (!this.lines.has(f.file)) {
					this.lines.set(f.file, new Set());
					this.shards.set(f.file, new Set());
				}
				this.lines.get(f.file).add(f.line);
				this.shards.get(f.file).add(s);
				if (q >= 0)
					this.fnLines[q].add(f.file + "\0" + f.line);
			}
			const out = frames[frames.length - 1];
			if (q >= 0 && out.file) {
				const m = this.fnBytes[q];
				m.set(out.file, (m.get(out.file) || 0) + b - a);
			}
		});
	}

	finish(insns) {
		const paths = [...this.lines.keys()].sort();
		const fileIx = new Map(paths.map((p, k) => [p, k]));
		const starts = insns.starts;
		const count = (lo, hi) => {
			const at = (x) => { let a = 0, b = starts.length; while (a < b) { const m = (a + b) >> 1; if (starts[m] < x) a = m + 1; else b = m; } return a; };
			return at(hi) - at(lo);
		};
		// One entry per (name, file): the parts of a function.
		const byKey = new Map();
		const funcs = [];
		let inFunctions = 0;
		this.fr.forEach(([lo, hi, name], q) => {
			let file = null, best = 0;
			for (const [p, n] of this.fnBytes[q])
				if (n > best) { best = n; file = p; }
			const key = name + "\0" + file;
			let fn = byKey.get(key);
			if (!fn) {
				fn = { name, file, lines: new Set(), insns: 0, ranges: [] };
				byKey.set(key, fn);
				funcs.push(fn);
			}
			for (const k of this.fnLines[q])
				if (k.slice(0, k.lastIndexOf("\0")) === file)
					fn.lines.add(k);
			fn.ranges.push(lo, hi - lo);
			if (starts) {
				const n = count(lo, hi);
				fn.insns += n;
				inFunctions += n;
			}
		});
		const out = {
			format: 1, shardBits: SHARD_BITS,
			files: paths.map((p) => [p, this.lines.get(p).size, [...this.shards.get(p)].sort((a, b) => a - b)]),
			funcs: funcs.map((f) => [f.name, f.file === null ? -1 : fileIx.get(f.file), f.lines.size, starts ? f.insns : null, ...f.ranges]),
		};
		if (starts)
			out.insns = { total: starts.length, inFunctions, tool: insns.tool };
		else
			out.insnsWhy = insns.why;
		return out;
	}
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
	generate(args[0], args[1]).catch((err) => { console.error(err); process.exit(1); });
else {
	console.error("usage: gen-dwarf.js ELF OUTDIR | gen-dwarf.js --lookup|--where OUTDIR < addresses");
	process.exit(2);
}
