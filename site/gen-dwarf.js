#!/usr/bin/env node
"use strict";
// Line tables and inline chains of a -g ELF as address shards for the page,
// and its variables with their types.
//
//   site/gen-dwarf.js ELF OUTDIR
//       OUTDIR/index.json and OUTDIR/<hex>.bin, one shard per 64 KiB of
//       physical address space that has code (dwarf.js: dwarfEncode);
//       OUTDIR/types.json, the variables at fixed addresses (each with where
//       it is defined: DW_AT_decl_file, decl_line) and the types
//       they reach (dwarf.js: DwarfTypes); OUTDIR/files.json, every source
//       file with its lines that have code and every function with its
//       lines and instructions, for the page's Coverage tab (see coverage());
//       OUTDIR/cfi-<hex>.bin, the call frame information (.debug_frame or
//       .eh_frame) per 64 KiB, for backtraces (unwind.js);
//       OUTDIR/locals-<hex>.json.gz, the parameters and local variables of
//       the functions with code in each 64 KiB (dwarf.js dwarfScopeGroups);
//       OUTDIR/funcs-<k>.json.gz, every function's facts from
//       DW_TAG_subprogram (prototype, where declared, linkage, inlining,
//       its out-of-line code and inlined copies; dwarf.js DwarfFuncs),
//       sharded by name (dwFuncShard)
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
//   site/gen-dwarf.js --funcs OUTDIR < names
//       one JSON line per function name on stdin: every function of that
//       name with its facts as the page shows them (prototype, ptype-like
//       type, file:line, flags, out-of-line code with the call frame
//       information's frame of each (unwind.js cfiFrameInfo), inlined
//       copies; test_dwarf.py --funcs checks them with gdb, llvm-dwarfdump
//       and llvm-symbolizer)
//   site/gen-dwarf.js --vars OUTDIR < watch expressions
//       one JSON line per "[file.c::|Function::]name" on stdin: every
//       variable of that name with where it is defined (dwarf.js dwVarDecl)
//       and the one the page's Variables panel picks for it (dwVarsPick)
//       or why none (test_dwarf.py --vars checks them with gdb)
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
const { dwFuncShard, dwFuncProto, dwarfElf, dwarfFrames, dwarfFileCode, dwarfDecode,
	dwWhere, dwTypeName, dwTypeSize, dwVarDecls, dwVarsPick, dwarfScopesAt, dwarfLocalFrames, dwLocAt } = require("./dwarf.js");

const { cfiDecode, cfiRow, cfiRowText, cfiFrameInfo } = require("./unwind.js");

const { kgenDwarf, KgInsnStarts } = require("./kgen.js");

// The generation itself is kgen.js kgenDwarf (the page's Worker runs it
// on an imported vmlinux): here the ELF is read from disk, the files are
// written to outDir and the instruction starts come from $OBJDUMP.
async function generate(elf, outDir) {
	const fd = fs.openSync(elf, "r");
	const read = (off, len) => {
		const b = Buffer.alloc(len);
		fs.readSync(fd, b, 0, len, off);
		return new Uint8Array(b.buffer, b.byteOffset, len);
	};
	fs.mkdirSync(outDir, { recursive: true });
	for (const f of fs.readdirSync(outDir))
		if (/^(cfi-)?[0-9a-f]+\.bin$|^(locals|funcs)-[0-9a-f]+\.json\.gz$|^index\.json$/.test(f))
			fs.unlinkSync(path.join(outDir, f));
	await kgenDwarf(read, {
		name: path.basename(elf),
		insns: insnStarts(elf, dwarfElf(read).toPhys),
		put: (name, data) => fs.writeFileSync(path.join(outDir, name), data),
		gzip: (data) => zlib.gzipSync(data, { level: 9 }),
		log: (text) => console.log(outDir + (/^[a-z]+\.json:/.test(text) ? "/" : ": ") + text),
	});
}

// The physical address of every instruction start of the ELF's code, from
// a linear sweep of llvm-objdump -d ($OBJDUMP), sorted; or { why } when
// it cannot run.
function insnStarts(elf, toPhys) {
	const tool = process.env.OBJDUMP || "llvm-objdump";
	return new Promise((resolve) => {
		const p = spawn(tool, ["-d", "--no-show-raw-insn", elf], { stdio: ["ignore", "pipe", "ignore"] });
		const starts = new KgInsnStarts(toPhys);
		p.stdout.setEncoding("latin1");
		p.stdout.on("data", (chunk) => starts.push(chunk));
		p.on("error", (err) => resolve({ why: tool + ": " + err.message }));
		p.on("close", (code) => resolve(code !== 0 ? { why: tool + " exited with " + code } : starts.finish(tool + " -d")));
	});
}


// One JSON line per name on stdin: its functions' facts as the page
// renders them (the shard dwFuncShard names, types.json for the types,
// cfi-*.bin for the frame of each out-of-line copy).
function funcsLookup(dir) {
	const index = JSON.parse(fs.readFileSync(path.join(dir, "index.json"), "utf8"));
	const T = JSON.parse(fs.readFileSync(path.join(dir, index.types), "utf8"));
	const shards = new Map(), cfi = new Map();
	const shard = (k) => {
		if (!shards.has(k))
			shards.set(k, JSON.parse(zlib.gunzipSync(fs.readFileSync(path.join(dir, "funcs-" + k.toString(16) + ".json.gz")))));
		return shards.get(k);
	};
	const frame = (pa) => {
		const s = Math.floor(pa / 2 ** index.shardBits);
		if (!cfi.has(s))
			cfi.set(s, index.cfi && index.cfi.shards && index.cfi.shards.includes(s) ?
				cfiDecode(new Uint8Array(fs.readFileSync(path.join(dir, "cfi-" + s.toString(16) + ".bin")))) : null);
		return cfi.get(s) ? cfiFrameInfo(cfi.get(s), pa) : null;
	};
	const out = [];
	for (const name of fs.readFileSync(0, "utf8").split(/\s+/).filter(Boolean)) {
		const F = shard(dwFuncShard(name, index.funcs.shards));
		out.push(JSON.stringify({ name, fns: F.fns.filter((e) => e[0] === name).map((e) => ({
			proto: dwFuncProto(T, e), type: dwFuncProto(T, e, false), file: e[1] >= 0 ? F.files[e[1]] : null, line: e[2],
			external: !!(e[3] & 1), varargs: !!(e[3] & 2), noreturn: !!(e[3] & 4), prototyped: !!(e[3] & 8), inline: e[4],
			ool: e[7].map((r) => r.reduce((a, x, k) => (k % 2 ? a[a.length - 1].push(x) : a.push([x]), a), []).map(([lo, len]) => ({ lo, len, frame: frame(lo) }))),
			copies: e[8] })) }));
	}
	process.stdout.write(out.join("\n") + "\n");
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

// One JSON line per watch expression's name: its definitions and the pick.
function varsLookup(dir) {
	const index = JSON.parse(fs.readFileSync(path.join(dir, "index.json"), "utf8"));
	const T = JSON.parse(fs.readFileSync(path.join(dir, index.types), "utf8"));
	const byName = new Map();
	for (const v of T.vars)
		byName.set(v[0], [...byName.get(v[0]) || [], v]);
	const def = (v) => {
		const [d, ...also] = dwVarDecls(T, v);
		return { pa: v[1], file: d ? d.file : null, line: d ? d.line : null, also: also.map((x) => [x.file, x.line]), scope: v[6] || "", external: !!(v[5] & 1) };
	};
	const out = [];
	for (const line of fs.readFileSync(0, "utf8").split("\n")) {
		const m = /^\s*(?:(.+)::)?([A-Za-z_]\w*)\s*$/.exec(line);
		if (!m)
			continue;
		const r = dwVarsPick(T, byName.get(m[2]) || [], m[1] || "", m[2]);
		out.push(JSON.stringify({ expr: line.trim(), defs: r.all.map(def), pick: r.v ? def(r.v) : null, matches: r.list ? r.list.length : 0, why: r.why || null }));
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
		out.push(JSON.stringify({ pa, text: m[3], names: names.map((n) => ({ op: n.op, name: n.name, kind: n.kind, var: n.var, varOff: n.varOff, va: n.va, rel: n.rel,
			aliases: n.aliases, scope: n.scope })) }));
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
else if (args[0] === "--funcs" && args.length === 2)
	funcsLookup(args[1]);
else if (args[0] === "--where" && args.length === 2)
	where(args[1]);
else if (args[0] === "--vars" && args.length === 2)
	varsLookup(args[1]);
else if (args.length === 2)
	generate(args[0], args[1]).catch((err) => { console.error(err); process.exit(1); });
else {
	console.error("usage: gen-dwarf.js ELF OUTDIR | gen-dwarf.js --lookup|--lines|--where|--vars|--cfi|--locals|--operands|--funcs OUTDIR < addresses");
	process.exit(2);
}
