"use strict";
// The generators of a kernel's data files, shared by CI's command lines
// (Node: gen-dwarf.js, gen-src.js) and the page's Worker (genworker.js),
// so an imported vmlinux gets the files CI makes for the kernels shipped
// here (plan E1):
//
//   kgenSymbols(read, nmText, name, trim)  symbols.json as gen-symbols.py
//                                          writes it (nmText: `llvm-nm -nS ELF`)
//   kgenDisasm(read, objdumpText)          disasm.json as gen-disasm.py writes
//                                          it (objdumpText: `llvm-objdump -d ELF`)
//   kgenDwarf(read, opts)                  gen-dwarf.js's output: line shards,
//                                          files.json, types.json, locals, funcs,
//                                          CFI shards, index.json
//   kgenSrc(getDwarf, readSrc, opts)       gen-src.js's output: src/index.json
//                                          and src/<path>.gz
//
// read(off, len) gives bytes of the ELF; the outputs go to opts.put(name,
// bytes or string); opts.gzip(string or bytes) compresses (zlib level 9 in
// Node, CompressionStream in the browser: other bytes for the same data, so
// the .gz files and the gzBytes counts they make are the only parts of the
// output that differ between the two, see test_gen_worker.py).
//
// dwarf.js and unwind.js are globals here: importScripts in the Worker; in
// Node this file requires them onto globalThis.

if (typeof module !== "undefined" && typeof dwarfRow === "undefined")
	Object.assign(globalThis, require("./dwarf.js"), require("./unwind.js"));

const SHARD_BITS = 16;

// ---- JSON as Python's json.dump(obj, separators=(",", ":")) writes it ----------

// JSON.stringify leaves non-ASCII characters and DEL as they are, Python
// (ensure_ascii) writes them as \uXXXX; nothing else differs for the
// strings, integers, lists and objects of these files.
function kgPyJson(obj) {
	return JSON.stringify(obj).replace(/[\u007f-\uffff]/g, (c) => "\\u" + c.charCodeAt(0).toString(16).padStart(4, "0"));
}

// Python's str.splitlines() and str.split() boundaries.
const KG_LINES = /\r\n|[\n\r\v\f\x1c-\x1e\x85\u2028\u2029]/;
const KG_SPACE = /[\s\x1c-\x1f]+/;

// ---- ELF headers (32-bit little-endian) ---------------------------------------------

function kgElf(read) {
	const h = read(0, 52);
	if (h[0] !== 0x7f || h[1] !== 0x45 || h[2] !== 0x4c || h[3] !== 0x46 || h[4] !== 1 || h[5] !== 1)
		throw new Error("not a 32-bit little-endian ELF");
	const dv = (b) => new DataView(b.buffer, b.byteOffset, b.byteLength);
	const hv = dv(h);
	const phoff = hv.getUint32(0x1c, true), shoff = hv.getUint32(0x20, true);
	const phentsize = hv.getUint16(0x2a, true), phnum = hv.getUint16(0x2c, true);
	const shentsize = hv.getUint16(0x2e, true), shnum = hv.getUint16(0x30, true), shstrndx = hv.getUint16(0x32, true);
	const loads = [];
	const ph = dv(read(phoff, phentsize * phnum));
	for (let i = 0; i < phnum; i++) {
		const o = i * phentsize;
		const vaddr = ph.getUint32(o + 8, true), paddr = ph.getUint32(o + 12, true), memsz = ph.getUint32(o + 20, true);
		if (ph.getUint32(o, true) === 1 && memsz)
			loads.push([vaddr, vaddr + memsz, vaddr - paddr]);
	}
	const sv = dv(read(shoff, shentsize * shnum));
	const shdrs = [];
	for (let i = 0; i < shnum; i++) {
		const u = (k) => sv.getUint32(i * shentsize + 4 * k, true);
		shdrs.push({ name: u(0), type: u(1), flags: u(2), addr: u(3), offset: u(4), size: u(5) });
	}
	const strs = shdrs[shstrndx] ? read(shdrs[shstrndx].offset, shdrs[shstrndx].size) : new Uint8Array(0);
	const dec = new TextDecoder();
	for (const s of shdrs) {
		let end = s.name;
		while (end < strs.length && strs[end])
			end++;
		s.sname = dec.decode(strs.subarray(s.name, end));
	}
	const toPhys = (va) => {
		for (const [lo, hi, delta] of loads)
			if (lo <= va && va < hi)
				return va - delta;
		return null;
	};
	return { loads, shdrs, toPhys };
}

// ---- symbols.json (gen-symbols.py) -------------------------------------------------

function kgSecClass(name, flags, type) {
	if (name === ".brk")
		return "brk";
	if (type === 8) // SHT_NOBITS
		return "bss";
	if (flags & 0x4) // SHF_EXECINSTR
		return "text";
	if (name.includes(".rodata") || !(flags & 0x1)) // not SHF_WRITE
		return "rodata";
	if (name.includes(".data"))
		return "data";
	return "other";
}

const KG_DROP_PREFIXES = ["__ksymtab_", "__kstrtab_", "__kstrtabns_", "__crc_", "__UNIQUE_ID_", "__initcall",
	"__setup_", "__param_", "__func__", "__already_done", "__print_once", "__warned",
	"__key.", "__compound_literal", "__tpstrtab_", "__bpf_trace_", "__event_", "event_class_",
	"trace_event_fields_", "print_fmt_", "__SCT__", "__SCK__"];

// --trim (big distro kernels): symbols with a size, all code, and global
// data without the export/tracing bookkeeping.
function kgKeepTrimmed(s) {
	if (!s.size)
		return false;
	if (s.type === "t" || s.type === "T")
		return true;
	return "DBR".includes(s.type) && !KG_DROP_PREFIXES.some((p) => s.name.startsWith(p));
}

// { json, sections, symbols }: json is the file gen-symbols.py writes.
function kgenSymbols(read, nmText, name, trim) {
	const e = kgElf(read);
	const sections = [];
	for (const s of e.shdrs) {
		if (!(s.flags & 0x2) || !s.size) // SHF_ALLOC, non-empty
			continue;
		const pa = e.toPhys(s.addr);
		if (pa === null)
			continue;
		sections.push({ name: s.sname, class: kgSecClass(s.sname, s.flags, s.type), vaddr: s.addr, paddr: pa, size: s.size });
	}
	sections.sort((a, b) => a.vaddr - b.vaddr);
	let symbols = [];
	for (const line of nmText.split(KG_LINES)) {
		const parts = line.split(KG_SPACE).filter(Boolean);
		let va, size, typ, sym;
		if (parts.length === 4)
			[va, size, typ, sym] = [parseInt(parts[0], 16), parseInt(parts[1], 16), parts[2], parts[3]];
		else if (parts.length === 3)
			[va, size, typ, sym] = [parseInt(parts[0], 16), 0, parts[1], parts[2]];
		else
			continue; // undefined symbols have no address
		if ("aAuUvw".includes(typ))
			continue; // absolute, undefined, weak undefined
		const pa = e.toPhys(va);
		if (pa === null)
			continue;
		const sec = sections.find((s) => s.vaddr <= va && va < s.vaddr + s.size) || null;
		// A weak definition: its section's global letter, as an ELF import writes it.
		if (typ === "V" || typ === "W")
			typ = { text: "T", rodata: "R", bss: "B", brk: "B" }[sec ? sec.class : ""] || "D";
		symbols.push({ name: sym, vaddr: va, paddr: pa, size, type: typ, section: sec ? sec.name : "" });
	}
	if (trim)
		symbols = symbols.filter(kgKeepTrimmed);
	return { json: kgPyJson({ name, sections, symbols }) + "\n", sections, symbols };
}

// ---- disasm.json (gen-disasm.py) ---------------------------------------------------

const KG_LABEL = /^([0-9a-f]+) <(.+)>:$/;
const KG_INSN = /^\s*([0-9a-f]+):\s((?:[0-9a-f]{2} ?)+)\s*(?:\t(.*))?$/;
const KG_NO_FALL = /^(?:jmp|ljmp|ret|lret|iret|sysexit|sysret)/;
const KG_PADDING = /^(?:nop|int3)/;

const kgGcd = (a, b) => { while (b) [a, b] = [b, a % b]; return a; };

// {paddr: "array"} of the stub arrays after the taken labels (gen-disasm.py stub_arrays).
function kgStubArrays(insns, labels, phys) {
	const out = new Map();
	const bounds = [...new Set(labels.map((l) => l[0]))].sort((a, b) => a - b);
	bounds.push(Infinity);
	let k = 0;
	for (let b = 0; b + 1 < bounds.length; b++) {
		const lo = bounds[b], hi = bounds[b + 1];
		while (k < insns.length && insns[k][0] < lo)
			k++;
		if (!phys.has(lo))
			continue;
		const offs = [];
		for (let j = k; j < insns.length - 1; j++) {
			const [pa, raw, text] = insns[j], nxt = insns[j + 1];
			if (nxt[0] >= hi)
				break;
			if (KG_NO_FALL.test(text) && pa + Math.floor(raw.length / 2) === nxt[0] && !KG_PADDING.test(nxt[2]) && !nxt[1].startsWith("00"))
				offs.push(nxt[0] - lo);
		}
		if (offs.length >= 3 && offs.reduce(kgGcd) >= 4)
			for (const o of offs)
				out.set(lo + o, "array");
	}
	return out;
}

// { json, insns, labels, taken }: json is the file gen-disasm.py writes.
function kgenDisasm(read, objdumpText) {
	const e = kgElf(read);
	const insns = [], labels = [];
	for (const line of objdumpText.split(KG_LINES)) {
		let m = KG_LABEL.exec(line);
		if (m) {
			const pa = e.toPhys(parseInt(m[1], 16));
			if (pa !== null)
				labels.push([pa, m[2]]);
			continue;
		}
		m = KG_INSN.exec(line);
		if (!m)
			continue;
		const raw = m[2].replace(/ /g, "");
		const asm = (m[3] || "").replace(/\t/g, " ").trim();
		if (!asm && insns.length) {
			// objdump continues long encodings on a line with bytes only
			insns[insns.length - 1][1] += raw;
			continue;
		}
		const pa = e.toPhys(parseInt(m[1], 16));
		if (pa !== null)
			insns.push([pa, raw, asm]);
	}
	// The instruction starts whose virtual address the image holds.
	const starts = new Set(insns.map((x) => x[0]));
	const phys = new Map();
	const note = (va, how) => {
		const pa = e.toPhys(va);
		if (pa !== null && starts.has(pa) && !phys.has(pa))
			phys.set(pa, how);
	};
	for (const s of e.shdrs) {
		if (s.type !== 1 || !(s.flags & 2) || !s.size) // allocated PROGBITS
			continue;
		const data = read(s.offset, s.size), dv = new DataView(data.buffer, data.byteOffset, data.byteLength);
		if (s.sname === "__ex_table") {
			for (let k = 0; k < data.length - 11; k += 12)
				note((s.addr + k + 4 + dv.getInt32(k + 4, true)) >>> 0, "extable");
		} else if (!(s.flags & 4)) {
			for (let k = (4 - s.addr % 4) % 4; k < data.length - 3; k += 4)
				note(dv.getUint32(k, true), "data");
		}
	}
	for (const [, raw] of insns) {
		const n = raw.length / 2;
		for (let k = 0; k < n - 3; k++)
			note(parseInt(raw.slice(2 * k + 6, 2 * k + 8) + raw.slice(2 * k + 4, 2 * k + 6) + raw.slice(2 * k + 2, 2 * k + 4) + raw.slice(2 * k, 2 * k + 2), 16), "code");
	}
	for (const [pa, how] of kgStubArrays(insns, labels, phys))
		if (!phys.has(pa))
			phys.set(pa, how);
	const taken = [...phys].sort((a, b) => a[0] - b[0] || (a[1] < b[1] ? -1 : a[1] > b[1] ? 1 : 0));
	return { json: kgPyJson({ insns, labels, taken }), insns, labels, taken };
}

// ---- instruction starts (llvm-objdump -d --no-show-raw-insn) ----------------------

// The physical address of every instruction start, from the output of a
// linear sweep fed in chunks (bytes or latin1 text) as the tool writes it:
// a distro kernel's is ~150 MB, never held whole.
class KgInsnStarts {
	constructor(toPhys) {
		this.toPhys = toPhys;
		this.out = [];
		this.rest = "";
	}

	push(chunk) {
		const text = typeof chunk === "string" ? chunk : kgLatin1(chunk);
		const lines = (this.rest + text).split("\n");
		this.rest = lines.pop();
		for (const l of lines) {
			const m = /^ *([0-9a-f]+): *\t/.exec(l);
			if (m) {
				const pa = this.toPhys(parseInt(m[1], 16));
				if (pa !== null)
					this.out.push(pa);
			}
		}
	}

	finish(tool) {
		return { starts: Float64Array.from(this.out).sort(), tool };
	}
}

function kgLatin1(b) {
	let s = "";
	for (let i = 0; i < b.length; i += 8192)
		s += String.fromCharCode.apply(null, b.subarray(i, i + 8192));
	return s;
}

// ---- DWARF (gen-dwarf.js) ------------------------------------------------------------


// Line shards: the page only reads them (dwarf.js dwarfDecode, which
// also describes the format). The part of table t for [lo, hi): the line
// row in force at lo is repeated at lo, ranges overlapping it are kept.
// Strings are re-indexed.
function dwarfSlice(t, lo, hi) {
	const strings = [], map = new Map();
	const sid = (k) => {
		if (k < 0)
			return -1;
		let v = map.get(k);
		if (v === undefined) {
			v = strings.length;
			strings.push(t.strings[k]);
			map.set(k, v);
		}
		return v;
	};
	const la = [], lf = [], ll = [], lk = [];
	let a = dwarfRow(t, lo);
	if (a < 0)
		a = 0;
	for (let k = a; k < t.la.length && t.la[k] < hi; k++) {
		la.push(Math.max(lo, t.la[k]));
		lf.push(sid(t.lf[k]));
		ll.push(t.ll[k]);
		lk.push(t.la[k] >= lo && t.lk && t.lk[k] ? 1 : 0); // the row in force at lo starts before it
	}
	const sx = Array.from(t.sx || []).filter((x) => x >= lo && x < hi);
	const rg = [];
	for (let k = 0; k < t.ra.length; k++)
		if (t.ra[k] < hi && t.rh[k] > lo)
			rg.push([t.ra[k], t.rh[k], t.rd[k], sid(t.rn[k]), sid(t.rf[k]), t.rl[k]]);
	return dwarfPack(strings, la, lf, ll, rg, lk, sx);
}

// A line table as an MKBD 2 shard.
function dwarfEncode(t) {
	const out = [];
	const uv = (x) => {
		for (;;) {
			const c = x % 128;
			x = Math.floor(x / 128);
			if (x) out.push(c | 0x80);
			else { out.push(c); return; }
		}
	};
	const zv = (v) => v < 0 ? -2 * v - 1 : 2 * v;
	const zz = (v) => uv(zv(v));
	out.push(0x4d, 0x4b, 0x42, 0x44, 2);
	uv(t.strings.length);
	const enc = new TextEncoder();
	for (const s of t.strings) {
		for (const c of enc.encode(s))
			out.push(c);
		out.push(0);
	}
	uv(t.la.length);
	let pa = 0, pf = 0, pl = 0;
	for (let k = 0; k < t.la.length; k++) {
		uv(t.la[k] - pa); pa = t.la[k];
		uv(zv(t.lf[k] + 1 - pf) * 2 + (t.lk[k] ? 1 : 0)); pf = t.lf[k] + 1;
		zz(t.ll[k] - pl); pl = t.ll[k];
	}
	uv(t.ra.length);
	pa = 0;
	for (let k = 0; k < t.ra.length; k++) {
		uv(t.ra[k] - pa); pa = t.ra[k];
		uv(t.rh[k] - t.ra[k]); uv(t.rd[k]); uv(t.rn[k] + 1); uv(t.rf[k] + 1); uv(t.rl[k]);
	}
	uv(t.sx.length);
	pa = 0;
	for (const x of t.sx) {
		uv(x - pa); pa = x;
	}
	return Uint8Array.from(out);
}

// CFI shards: the page only reads them (unwind.js cfiDecode, which also
// describes the format). The FDEs of t that overlap [lo, hi), with the
// CIEs they use.
function cfiSlice(t, lo, hi) {
	const cieIx = new Map(), cies = [], fdes = [];
	for (let i = 0; i < t.lo.length; i++) {
		if (t.lo[i] >= hi || t.lo[i] + t.len[i] <= lo)
			continue;
		if (!cieIx.has(t.cie[i])) {
			cieIx.set(t.cie[i], cies.length);
			cies.push(t.cies[t.cie[i]]);
		}
		fdes.push([t.lo[i], t.len[i], cieIx.get(t.cie[i]), t.off[i], t.plen[i]]);
	}
	return cfiPack(cies, fdes, t.prog, t.bias);
}

// A CFI table as an MKBC 1 shard.
function cfiEncode(t) {
	const out = [];
	const uv = (x) => {
		for (;;) {
			const c = x % 128;
			x = Math.floor(x / 128);
			if (x) out.push(c | 0x80);
			else { out.push(c); return; }
		}
	};
	const bytes = (a) => { uv(a.length); for (const x of a) out.push(x); };
	out.push(0x4d, 0x4b, 0x42, 0x43, 1);
	uv(t.bias === null ? 0 : t.bias + 1);
	uv(t.cies.length);
	for (const c of t.cies) {
		uv(c.caf); uv(c.daf < 0 ? -2 * c.daf - 1 : 2 * c.daf); uv(c.ra); bytes(c.init);
	}
	uv(t.lo.length);
	let pa = 0;
	for (let i = 0; i < t.lo.length; i++) {
		uv(t.lo[i] - pa); pa = t.lo[i];
		uv(t.len[i]); uv(t.cie[i]); bytes(t.prog.subarray(t.off[i], t.off[i] + t.plen[i]));
	}
	return Uint8Array.from(out);
}

// cfi-<hex>.bin: the FDEs (with their CIEs) of each 64 KiB of physical
// address space that has code with CFI (unwind.js cfiEncode); the page
// reads the shard of an address when a backtrace passes there.
function kgCfiShards(e, read, put) {
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
		put("cfi-" + s.toString(16) + ".bin", b);
		bytes += b.length;
	}
	return { source: c.source, fdes: t.lo.length, cies: t.cies.length, shards, bytes };
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
class KgCoverage {
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
async function kgTypes(dw, e, put, gzip) {
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
		shards.delete(s);
		const gz = await gzip(js);
		put("locals-" + s.toString(16) + ".json.gz", gz);
		bytes += js.length;
		gzBytes += gz.length;
	}
	const T = acc.finish(sections);
	T.locals = { shards: list, functions: fns, vars, bytes, gzBytes };
	return T;
}

// funcs-<k>.json.gz: the function facts (dwarf.js DwarfFuncs) split by
// name into shards of ~100 KB of JSON (gzipped ~35 KB), each with its own
// file list: the page fetches the one shard a name hashes to.
const FUNC_SHARD_BYTES = 100000;
async function kgFuncShards(F, put, gzip) {
	const total = JSON.stringify(F).length;
	const n = Math.max(1, Math.round(total / FUNC_SHARD_BYTES));
	const parts = Array.from({ length: n }, () => ({ format: 1, files: [], fns: [], ix: new Map() }));
	for (const e of F.fns) {
		const p = parts[dwFuncShard(e[0], n)];
		const f = e[1] >= 0 ? F.files[e[1]] : null;
		if (f !== null && !p.ix.has(f)) {
			p.ix.set(f, p.files.length);
			p.files.push(f);
		}
		p.fns.push([e[0], f === null ? -1 : p.ix.get(f), ...e.slice(2)]);
	}
	let bytes = 0, gzBytes = 0;
	for (let k = 0; k < n; k++) {
		const p = parts[k];
		delete p.ix;
		const js = JSON.stringify(p);
		const gz = await gzip(js);
		put("funcs-" + k.toString(16) + ".json.gz", gz);
		bytes += js.length;
		gzBytes += gz.length;
	}
	return { shards: n, functions: F.fns.length, outOfLine: F.fns.filter((e) => e[7].length).length,
		copies: F.fns.reduce((x, e) => x + e[8].length, 0), bytes, gzBytes };
}

// gen-dwarf.js's files for the -g ELF read gives. opts: name (index.json
// "source"), insns (a promise of KgInsnStarts.finish(), or of { why }
// when the instructions cannot be counted), put(name, bytes | string),
// gzip(string) -> bytes (or a promise), log(text), progress(what) (optional).
// Returns index.json's object (also put).
async function kgenDwarf(read, opts) {
	const { put, gzip, log = () => {}, progress = () => {} } = opts;
	const t0 = Date.now();
	const e = dwarfElf(read);
	const sec = dwarfSections(e, read);
	if (!sec)
		throw new Error(opts.name + " has no .debug_info");
	// Every unit is decoded, so .debug_aranges (an address index) is not needed.
	const dw = new DwarfReader({ ...sec, aranges: null });
	const units = [];
	const us = dw.units();
	let nrows = 0, nranges = 0;
	progress("line tables");
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
	const shards = new Set();
	for (let k = 0; k < table.la.length; k++)
		if (table.lf[k] >= 0)
			shards.add(Math.floor(table.la[k] / 2 ** SHARD_BITS));
	for (let k = 0; k < table.ra.length; k++)
		for (let s = Math.floor(table.ra[k] / 2 ** SHARD_BITS); s <= Math.floor((table.rh[k] - 1) / 2 ** SHARD_BITS); s++)
			shards.add(s);
	const list = [...shards].sort((a, b) => a - b);
	let bytes = 0;
	const cov = new KgCoverage(table);
	for (const s of list) {
		const slice = dwarfSlice(table, s * 2 ** SHARD_BITS, (s + 1) * 2 ** SHARD_BITS);
		const b = dwarfEncode(slice);
		put(s.toString(16) + ".bin", b);
		bytes += b.length;
		cov.addShard(s, slice);
	}
	progress("instructions");
	const fj = cov.finish(await opts.insns);
	put("files.json", JSON.stringify(fj));
	log(`files.json: ${fj.files.length} files, ${fj.funcs.length} functions, ` +
		`${fj.files.reduce((n, f) => n + f[1], 0)} lines with code, ${fj.insns ? fj.insns.total + " instructions (" + fj.insns.inFunctions +
		" in functions)" : "no instruction counts (" + fj.insnsWhy + ")"}; ${Date.now() - t2} ms with the shards`);
	const t3 = Date.now();
	progress("variables and types");
	const ty = await kgTypes(dw, e, put, gzip);
	const locals = ty.locals;
	delete ty.locals;
	log(`parameters and locals of ${locals.functions} functions (${locals.vars} variables), ` +
		`${locals.shards.length} shards, ${locals.bytes} bytes (${locals.gzBytes} gzipped)`);
	progress("function facts");
	const funcs = await kgFuncShards(ty.funcs, put, gzip);
	delete ty.funcs;
	log(`facts of ${funcs.functions} functions (${funcs.outOfLine} with code of their own, ${funcs.copies} inlined copies), ` +
		`${funcs.shards} shards, ${funcs.bytes} bytes (${funcs.gzBytes} gzipped)`);
	const tj = JSON.stringify(ty);
	put("types.json", tj);
	log(`types.json: ${ty.vars.length} variables, ${ty.types.length} types, ${tj.length} bytes; ${Date.now() - t3} ms`);
	const t4 = Date.now();
	progress("call frame information");
	const cfi = kgCfiShards(e, read, put);
	log(`${cfi.why || `call frame information from ${cfi.source}: ${cfi.fdes} FDEs, ${cfi.cies} CIEs, ` +
		`${cfi.shards.length} shards, ${cfi.bytes} bytes`}; ${Date.now() - t4} ms`);
	const index = { format: 2, cfi, locals, funcs, breakpoints: table.lk.reduce((n, x) => n + x, 0) + table.sx.length, shardBits: SHARD_BITS, shards: list, units: us.length,
		version: [...new Set(us.map((u) => u.ver))], lineRows: table.la.length, ranges: table.ra.length,
		source: opts.name, files: "files.json", types: "types.json", variables: ty.vars.length, typeCount: ty.types.length, typeBytes: tj.length };
	put("index.json", JSON.stringify(index));
	log(`${us.length} units, ${nrows} line rows -> ${table.la.length}, ${nranges} function/inline ranges, ` +
		`${list.length} shards, ${bytes} bytes; decode ${t1 - t0} ms, table ${t2 - t1} ms, write ${Date.now() - t2} ms`);
	return index;
}

// ---- C source (gen-src.js) ----------------------------------------------------------

// The source files a kernel's DWARF names (gen-dwarf.js's output, read
// through getDwarf(name) -> bytes), looked up with readSrc(path) -> bytes
// or null (not a file of the tree; may return a promise), written gzipped
// as <path>.gz with index.json. opts: origin, notes ({ path: how it was
// made }), put, gzip, log. Returns { files, missing, pastEnd, named, checked }:
// pastEnd lists the files shorter than a line the DWARF names (not the
// tree the kernel was built from).
async function kgenSrc(getDwarf, readSrc, opts) {
	const { put, gzip, log = () => {} } = opts;
	const dec = new TextDecoder();
	const json = async (name) => JSON.parse(dec.decode(await getDwarf(name)));
	// path -> highest line named; inlined calls: [call file, call line, callee].
	const maxLine = new Map(), calls = [];
	const index = await json("index.json");
	for (const s of index.shards) {
		const t = dwarfDecode(await getDwarf(s.toString(16) + ".bin"));
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
		const T = await json(index.types);
		const note = (f, l) => maxLine.set(T.files[f], Math.max(maxLine.get(T.files[f]) || 0, l));
		for (const v of T.vars)
			if (T.files && v.length > 7 && v[7] >= 0) {
				note(v[7], v[8]);
				for (const [f, l] of v[9] || [])
					note(f, l);
			}
	}
	const files = [], missing = {}, text = new Map(), pastEnd = [];
	let raw = 0, packed = 0;
	for (const p of [...maxLine.keys()].sort()) {
		if (p.startsWith("/") || p.split("/").includes("..")) {
			missing[p] = "outside the kernel source tree (a compiler or system header)";
			continue;
		}
		const b = await readSrc(p);
		if (!b) {
			missing[p] = "not in the source tree: made during the build";
			continue;
		}
		const lines = dec.decode(b).split("\n");
		if (maxLine.get(p) > lines.length)
			pastEnd.push(`${p}:${maxLine.get(p)} (${lines.length} lines)`);
		text.set(p, lines);
		const z = await gzip(b);
		put(p + ".gz", z);
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
	put("index.json", JSON.stringify({ format: 1, origin: opts.origin, files, missing, notes: opts.notes || {}, bytes: raw, gzipBytes: packed }));
	log(`${files.length} files (${raw} bytes, ${packed} gzipped), ${Object.keys(missing).length} missing` +
		(Object.keys(missing).length ? ": " + Object.keys(missing).join(", ") : "") +
		`; inlined calls whose call line names the function: ${named}/${checked}`);
	return { files, missing, pastEnd, named, checked };
}

if (typeof module !== "undefined")
	module.exports = { SHARD_BITS, kgPyJson, kgElf, kgenSymbols, kgenDisasm, KgInsnStarts, kgenDwarf, kgenSrc };
