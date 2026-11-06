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
//       lines and instructions, for the page's Coverage tab (see coverage());
//       OUTDIR/cfi-<hex>.bin, the call frame information (.debug_frame or
//       .eh_frame) per 64 KiB, for backtraces (unwind.js);
//       OUTDIR/locals-<hex>.json.gz, the parameters and local variables of
//       the functions with code in each 64 KiB (dwarf.js dwarfScopeGroups)
//   site/gen-dwarf.js --lookup OUTDIR < addresses
//       one JSON line per physical address (hex or decimal) on stdin:
//       the frames the page shows there (test_dwarf.py compares them with
//       llvm-symbolizer --inlines)
//   site/gen-dwarf.js --lines OUTDIR < file:line lines
//       one JSON line per "path:line" on stdin: its code as the page's
//       Source panel lists it and the addresses its "Run to" stops at
//       (dwarf.js dwarfFileCode over the shards files.json names for the
//       file; test_dwarf.py --lines checks them with llvm-dwarfdump and
//       llvm-symbolizer)
//   site/gen-dwarf.js --cfi OUTDIR < addresses
//       one JSON line per physical address: the call frame information
//       row there (unwind.js cfiRow) as llvm-dwarfdump --debug-frame
//       prints it (test_dwarf.py --cfi compares them)
//   site/gen-dwarf.js --locals OUTDIR < addresses
//       one JSON line per physical address: the frames there with their
//       parameters and locals and the kind of location each has there
//       (test_dwarf.py --locals compares them with gdb's info scope)
//   site/gen-dwarf.js --operands OUTDIR < instructions
//       one JSON line per input line "PADDR VADDR INSTRUCTION" (AT&T, as
//       llvm-objdump prints it): the names the page's listings give its
//       memory operands (asmnames.js asmOperandNames without a pause:
//       globals and CFA-relative stack slots; test_dwarf.py --operands
//       checks them with gdb and llvm-dwarfdump)
//   site/gen-dwarf.js --where OUTDIR < addresses
//       one JSON line per physical address: the variable and member path
//       there (dwarf.js dwWhere) with the leaf's type (test_dwarf.py --types
//       checks them with gdb)
//
// Reads only the .debug_* sections it needs, a unit at a time (a distro
// vmlinux is ~300 MB).

const fs = require("fs");
const zlib = require("zlib");
const path = require("path");
const { spawn } = require("child_process");
const { DwarfReader, DwarfTypes, dwarfElf, dwarfSectionOf, dwarfSections, dwarfTable, dwarfSlice, dwarfFrames, dwarfEachLine, dwarfFileCode, dwarfEncode, dwarfDecode,
	dwWhere, dwTypeName, dwTypeSize, dwarfScopesAt, dwarfLocalFrames, dwLocAt } = require("./dwarf.js");

const { cfiFromElf, cfiSlice, cfiEncode, cfiDecode, cfiRow, cfiRowText } = require("./unwind.js");

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
		if (/^(cfi-)?[0-9a-f]+\.bin$|^locals-[0-9a-f]+\.json\.gz$|^index\.json$/.test(f))
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
	const ty = types(dw, e, outDir);
	const locals = ty.locals;
	delete ty.locals;
	console.log(`${outDir}: parameters and locals of ${locals.functions} functions (${locals.vars} variables), ` +
		`${locals.shards.length} shards, ${locals.bytes} bytes (${locals.gzBytes} gzipped)`);
	const tj = JSON.stringify(ty);
	fs.writeFileSync(path.join(outDir, "types.json"), tj);
	console.log(`${outDir}/types.json: ${ty.vars.length} variables, ${ty.types.length} types, ${tj.length} bytes; ${Date.now() - t3} ms`);
	const t4 = Date.now();
	const cfi = cfiShards(e, read, outDir);
	console.log(`${outDir}: ${cfi.why || `call frame information from ${cfi.source}: ${cfi.fdes} FDEs, ${cfi.cies} CIEs, ` +
		`${cfi.shards.length} shards, ${cfi.bytes} bytes`}; ${Date.now() - t4} ms`);
	const index = { format: 2, cfi, locals, breakpoints: table.lk.reduce((n, x) => n + x, 0) + table.sx.length, shardBits: SHARD_BITS, shards: list, units: us.length,
		version: [...new Set(us.map((u) => u.ver))], lineRows: table.la.length, ranges: table.ra.length,
		source: path.basename(elf), files: "files.json", types: "types.json", variables: ty.vars.length, typeCount: ty.types.length, typeBytes: tj.length };
	fs.writeFileSync(path.join(outDir, "index.json"), JSON.stringify(index));
	console.log(`${outDir}: ${us.length} units, ${nrows} line rows -> ${table.la.length}, ${nranges} function/inline ranges, ` +
		`${list.length} shards, ${bytes} bytes; decode ${t1 - t0} ms, table ${t2 - t1} ms, write ${Date.now() - t2} ms`);
}

// cfi-<hex>.bin: the FDEs (with their CIEs) of each 64 KiB of physical
// address space that has code with CFI (unwind.js cfiEncode); the page
// reads the shard of an address when a backtrace passes there.
function cfiShards(e, read, outDir) {
	const c = cfiFromElf(e, read, e.toPhys);
	if (c.why)
		return { why: c.why };
	const t = c.t, set = new Set();
	for (let i = 0; i < t.lo.length; i++)
		for (let s = Math.floor(t.lo[i] / 2 ** SHARD_BITS); s <= Math.floor((t.lo[i] + t.len[i] - 1) / 2 ** SHARD_BITS); s++)
			set.add(s);
	const shards = [...set].sort((a, b) => a - b);
	let bytes = 0;
	for (const s of shards) {
		const b = cfiEncode(cfiSlice(t, s * 2 ** SHARD_BITS, (s + 1) * 2 ** SHARD_BITS));
		fs.writeFileSync(path.join(outDir, "cfi-" + s.toString(16) + ".bin"), b);
		bytes += b.length;
	}
	return { source: c.source, fdes: t.lo.length, cies: t.cies.length, shards, bytes };
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

// Variables and their types, every unit (dwarf.js DwarfTypes), and the
// parameters and locals of every function (dwarf.js dwarfScopeGroups):
// locals-<hex>.json.gz per 64 KiB of code, { format, fns: [group, ...] }
// gzipped (88 MB of JSON for a distro kernel, 16 MB gzipped), with each
// function in every shard its code touches.
function types(dw, e, outDir) {
	const acc = new DwarfTypes(dw);
	const sections = [];
	const secOf = dwarfSectionOf(e, sections);
	const us = dw.units();
	const shards = new Map();
	let fns = 0, vars = 0, gzBytes = 0;
	for (let i = 0; i < us.length; i++) {
		if (us[i].unitType !== 1 && us[i].unitType !== 3)
			continue;
		for (const g of acc.addUnit(dw.decodeData(i, true), e.toPhys, secOf)) {
			const js = JSON.stringify(g);
			fns++;
			for (const s of g)
				vars += s[5].length;
			const set = new Set();
			const r = g[0][2];
			for (let k = 0; k < r.length; k += 2)
				for (let s = Math.floor(r[k] / 2 ** SHARD_BITS); s <= Math.floor((r[k] + r[k + 1] - 1) / 2 ** SHARD_BITS); s++)
					set.add(s);
			for (const s of set) {
				if (!shards.has(s))
					shards.set(s, []);
				shards.get(s).push(js);
			}
		}
	}
	const list = [...shards.keys()].sort((a, b) => a - b);
	let bytes = 0;
	for (const s of list) {
		const js = '{"format":1,"fns":[' + shards.get(s).join(",") + "]}";
		const gz = zlib.gzipSync(js, { level: 9 });
		fs.writeFileSync(path.join(outDir, "locals-" + s.toString(16) + ".json.gz"), gz);
		bytes += js.length;
		gzBytes += gz.length;
	}
	const T = acc.finish(sections);
	T.locals = { shards: list, functions: fns, vars, bytes, gzBytes };
	return T;
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

// One JSON line per "path:line": dwarfFileCode over the shards that
// files.json lists for the file, like the page's dbgLineCode.
function linesLookup(dir) {
	const index = JSON.parse(fs.readFileSync(path.join(dir, "index.json"), "utf8"));
	const F = JSON.parse(fs.readFileSync(path.join(dir, "files.json"), "utf8"));
	const fileIx = new Map(F.files.map((f, k) => [f[0], k]));
	const size = 2 ** index.shardBits;
	const cache = new Map();
	const shard = (s) => {
		if (!cache.has(s))
			cache.set(s, dwarfDecode(new Uint8Array(fs.readFileSync(path.join(dir, s.toString(16) + ".bin")))));
		return cache.get(s);
	};
	const out = [], files = new Map();
	for (const w of fs.readFileSync(0, "utf8").split("\n").filter(Boolean)) {
		const m = /^(.*):(\d+)$/.exec(w.trim());
		const fi = m ? fileIx.get(m[1]) : undefined;
		if (fi === undefined) {
			out.push(JSON.stringify({ q: w, none: true }));
			continue;
		}
		if (!files.has(m[1]))
			files.set(m[1], dwarfFileCode(F.files[fi][2].map((s) => [s * size, (s + 1) * size, shard(s)]), m[1]));
		const r = files.get(m[1]).get(Number(m[2])) || { rows: [], calls: [], bps: [], noStmt: false };
		out.push(JSON.stringify({ q: w, ...r }));
	}
	process.stdout.write(out.join("\n") + "\n");
}

// One JSON line per physical address: the CFI row there (unwind.js
// cfiRow) in llvm-dwarfdump's notation, read back from the shards.
function cfiLookup(dir) {
	const index = JSON.parse(fs.readFileSync(path.join(dir, "index.json"), "utf8"));
	const cache = new Map();
	const shard = (s) => {
		if (!cache.has(s))
			cache.set(s, index.cfi && index.cfi.shards && index.cfi.shards.includes(s) ?
				cfiDecode(new Uint8Array(fs.readFileSync(path.join(dir, "cfi-" + s.toString(16) + ".bin")))) : null);
		return cache.get(s);
	};
	const out = [];
	for (const w of fs.readFileSync(0, "utf8").split(/\s+/).filter(Boolean)) {
		const pa = Number(w);
		const t = shard(Math.floor(pa / 2 ** index.shardBits));
		const row = t ? cfiRow(t, pa) : null;
		out.push(JSON.stringify(row ? (row.why ? { pa, why: row.why } : { pa, lo: row.lo, row: cfiRowText(row) }) : { pa }));
	}
	process.stdout.write(out.join("\n") + "\n");
}

// One JSON line per physical address: the frames there (innermost first)
// with their parameters and locals as the page lists them (dwarf.js
// dwarfLocalFrames) and each one's location class there: "out" (none),
// "reg:EAX" (a register), "const", "pieces" (DW_OP_piece) or "expr"
// (anything else); for test_dwarf.py --locals (gdb's `info scope`).
function localsLookup(dir) {
	const index = JSON.parse(fs.readFileSync(path.join(dir, "index.json"), "utf8"));
	const { CFI_REGS, dwOpEnd } = require("./unwind.js");
	const pieces = (e) => {
		for (let p = 0; p >= 0 && p < e.length; p = dwOpEnd(e, p))
			if (e[p] === 0x93 || e[p] === 0x9d)
				return true;
		return false;
	};
	const cache = new Map();
	const shard = (s) => {
		if (!cache.has(s))
			cache.set(s, index.locals && index.locals.shards.includes(s) ?
				JSON.parse(zlib.gunzipSync(fs.readFileSync(path.join(dir, "locals-" + s.toString(16) + ".json.gz")))).fns : []);
		return cache.get(s);
	};
	const cls = (loc, pa) => {
		const l = dwLocAt(loc, pa);
		if (l.why)
			return "out";
		if (l.c !== undefined || l.cb)
			return "const";
		const e = l.expr;
		if (pieces(e))
			return "pieces";
		if (e.length === 1 && e[0] >= 0x50 && e[0] <= 0x6f)
			return "reg:" + (CFI_REGS[e[0] - 0x50] || e[0] - 0x50);
		return e.length ? "expr" : "out";
	};
	const out = [];
	for (const w of fs.readFileSync(0, "utf8").split(/\s+/).filter(Boolean)) {
		const pa = Number(w);
		const frames = dwarfLocalFrames(dwarfScopesAt(shard(Math.floor(pa / 2 ** index.shardBits)), pa));
		out.push(JSON.stringify({ pa, frames: frames.map((f) => ({ func: f.func, inlined: f.inlined,
			vars: [...f.params, ...f.locals].map((v) => [v[0], cls(v[3], pa), v[2]]) })) }));
	}
	process.stdout.write(out.join("\n") + "\n");
}

// One JSON line per instruction: the names of its memory operands, from
// the shards (types.json, cfi-*, locals-*) and ../symbols.json if there.
function operandsLookup(dir) {
	const { asmOperandNames, asmSymLookup } = require("./asmnames.js");
	const index = JSON.parse(fs.readFileSync(path.join(dir, "index.json"), "utf8"));
	const T = index.types ? JSON.parse(fs.readFileSync(path.join(dir, index.types), "utf8")) : null;
	const symFile = path.join(dir, "..", "symbols.json");
	const sym = fs.existsSync(symFile) ? asmSymLookup(JSON.parse(fs.readFileSync(symFile, "utf8")).symbols) : null;
	const cfi = new Map(), loc = new Map();
	const shardOf = (pa) => Math.floor(pa / 2 ** index.shardBits);
	const row = (pa) => {
		const s = shardOf(pa);
		if (!cfi.has(s))
			cfi.set(s, index.cfi && index.cfi.shards && index.cfi.shards.includes(s) ?
				cfiDecode(new Uint8Array(fs.readFileSync(path.join(dir, "cfi-" + s.toString(16) + ".bin")))) : null);
		return cfi.get(s) ? cfiRow(cfi.get(s), pa) : null;
	};
	const scopes = (pa) => {
		const s = shardOf(pa);
		if (!loc.has(s))
			loc.set(s, index.locals && index.locals.shards.includes(s) ?
				JSON.parse(zlib.gunzipSync(fs.readFileSync(path.join(dir, "locals-" + s.toString(16) + ".json.gz")))).fns : []);
		return dwarfScopesAt(loc.get(s), pa);
	};
	const out = [];
	for (const line of fs.readFileSync(0, "utf8").split("\n")) {
		const m = /^\s*(\S+)\s+(\S+)\s+(.*)$/.exec(line);
		if (!m)
			continue;
		const pa = Number(m[1]), va = Number(m[2]);
		const names = asmOperandNames(m[3], pa, { off: va - pa, T, sym, scopes, row, cur: null });
		out.push(JSON.stringify({ pa, text: m[3], names: names.map((n) => ({ op: n.op, name: n.name, kind: n.kind, var: n.var, varOff: n.varOff, va: n.va, rel: n.rel })) }));
	}
	process.stdout.write(out.join("\n") + "\n");
}

const args = process.argv.slice(2);
if (args[0] === "--lookup" && args.length === 2)
	lookup(args[1]);
else if (args[0] === "--lines" && args.length === 2)
	linesLookup(args[1]);
else if (args[0] === "--cfi" && args.length === 2)
	cfiLookup(args[1]);
else if (args[0] === "--locals" && args.length === 2)
	localsLookup(args[1]);
else if (args[0] === "--operands" && args.length === 2)
	operandsLookup(args[1]);
else if (args[0] === "--where" && args.length === 2)
	where(args[1]);
else if (args.length === 2)
	generate(args[0], args[1]).catch((err) => { console.error(err); process.exit(1); });
else {
	console.error("usage: gen-dwarf.js ELF OUTDIR | gen-dwarf.js --lookup|--lines|--where|--cfi|--locals|--operands OUTDIR < addresses");
	process.exit(2);
}
