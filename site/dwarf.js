"use strict";

// DWARF line tables and inline chains: EIP -> file:line and the functions
// it was inlined into (innermost first, like `llvm-symbolizer --inlines`).
//
// One file for three users: site/gen-dwarf.js (node, CI) decodes a whole
// -g vmlinux into address shards for the shipped kernels; dwarfworker.js
// (the page's Worker) decodes one compile unit at a time of an imported
// vmlinux; the page (dbginfo.js) looks up both through dwarfTable() and
// dwarfFrames(), so there is one lookup and one render path.
//
// Supports DWARF 2-5, 32-bit DWARF, little-endian, the forms of GCC
// (strp, line_strp, sec_offset, ref4, implicit_const, ...) and of clang
// (strx1-4, addrx, rnglistx, loclistx, ref_addr with str_offsets, addr
// and rnglists bases). Relocation sections (.rel.debug_*) are ignored: a
// vmlinux is ET_EXEC, its DWARF values are final.

const DW_TAG_SUBPROGRAM = 0x2e, DW_TAG_INLINED = 0x1d;
// Tags whose children can hold code (subprograms, inlined calls). The
// children of every other tag are skipped through DW_AT_sibling.
const DW_CODE_PARENTS = new Set([0x11, 0x3c, 0x2e, 0x0b, 0x1d, 0x39, 0x41]);
const AT = { sibling: 0x01, name: 0x03, stmt_list: 0x10, low_pc: 0x11, high_pc: 0x12, comp_dir: 0x1b,
	abstract_origin: 0x31, specification: 0x47, ranges: 0x55, call_file: 0x58, call_line: 0x59,
	linkage_name: 0x6e, str_offsets_base: 0x72, addr_base: 0x73, rnglists_base: 0x74, mips_linkage_name: 0x2007,
	location: 0x02, byte_size: 0x0b, bit_offset: 0x0c, bit_size: 0x0d, const_value: 0x1c, lower_bound: 0x22,
	upper_bound: 0x2f, count: 0x37, data_member_location: 0x38, decl_file: 0x3a, decl_line: 0x3b, declaration: 0x3c,
	encoding: 0x3e, external: 0x3f, type: 0x49, data_bit_offset: 0x6b };
// Type DIEs DwarfReader.decodeData keeps, by the kind DwarfTypes uses.
const DW_TYPE_TAGS = { 0x24: "base", 0x0f: "ptr", 0x10: "ptr", 0x42: "ptr", 0x26: "const", 0x35: "volatile", 0x37: "restrict",
	0x47: "atomic", 0x16: "typedef", 0x13: "struct", 0x02: "struct", 0x17: "union", 0x04: "enum", 0x01: "array",
	0x15: "func", 0x3b: "void" };
const DW_TAG_MEMBER = 0x0d, DW_TAG_ENUMERATOR = 0x28, DW_TAG_SUBRANGE = 0x21, DW_TAG_PARAM = 0x05,
	DW_TAG_UNSPEC_PARAMS = 0x18, DW_TAG_VARIABLE = 0x34;

// Bytes a form takes when its size does not depend on the data; -1 if it does.
function dwFormSize(form, addrSize, ver) {
	switch (form) {
	case 0x01: return addrSize;
	case 0x05: case 0x12: case 0x26: case 0x2a: return 2;
	case 0x06: case 0x0e: case 0x13: case 0x17: case 0x1c: case 0x1d: case 0x1f: case 0x28: case 0x2c:
	case 0x1f20: case 0x1f21: return 4;
	case 0x07: case 0x14: case 0x20: case 0x24: return 8;
	case 0x0b: case 0x0c: case 0x11: case 0x25: case 0x29: return 1;
	case 0x1e: return 16;
	case 0x27: case 0x2b: return 3;
	case 0x10: return ver <= 2 ? addrSize : 4;
	case 0x19: case 0x21: return 0;
	default: return -1;
	}
}

// DW_AT_high_pc is an address (addr, addrx*) or an offset from low_pc (constant forms).
const dwAddrForm = (f) => f === 0x01 || f === 0x1b || (f >= 0x29 && f <= 0x2c) || f === 0x1f01;

const u32le = (b, p) => (b[p] | b[p + 1] << 8 | b[p + 2] << 16 | b[p + 3] << 24) >>> 0;
const u16le = (b, p) => b[p] | b[p + 1] << 8;
const textDecoder = new TextDecoder();

function cstrAt(b, p) {
	if (!b || p >= b.length)
		return null;
	let e = p;
	while (e < b.length && b[e])
		e++;
	return textDecoder.decode(b.subarray(p, e));
}

function dwNormalize(p) {
	const abs = p.startsWith("/");
	const out = [];
	for (const s of p.split("/")) {
		if (!s || s === ".")
			continue;
		if (s === ".." && out.length && out[out.length - 1] !== "..")
			out.pop();
		else
			out.push(s);
	}
	return (abs ? "/" : "") + out.join("/");
}

// A source path as the page shows it: relative to the kernel tree. An
// absolute path loses the build directory (DW_AT_comp_dir) or the longest
// prefix it shares with it (a source tree above an O= build directory); a
// relative one loses leading "../" (O= builds without prefix maps).
function dwDisplayPath(path, comp) {
	let p = dwNormalize(path);
	if (p.startsWith("/") && comp && comp.startsWith("/")) {
		const c = dwNormalize(comp);
		if (p.startsWith(c + "/"))
			p = p.slice(c.length + 1);
		else {
			const a = p.split("/"), b = c.split("/");
			let k = 0;
			while (k < a.length - 1 && k < b.length && a[k] === b[k])
				k++;
			if (k >= 2)
				p = a.slice(k).join("/");
		}
	}
	return p.replace(/^(\.\.\/)+/, "");
}

class DwarfReader {
	// sec: { readInfo(off, len) -> Uint8Array, infoSize, readLine(off, len), lineSize,
	//        abbrev, str, line_str, str_offsets, addr, rnglists, ranges, aranges: Uint8Array or null }
	constructor(sec) {
		this.sec = sec;
		this.abbrevCache = new Map();
		this.nameCache = new Map();
		this.rootCache = new Map();
		this.unitCache = new Map(); // unit index -> bytes (lazy readers)
		this.list = null;
	}

	// Unit headers, in .debug_info order.
	units() {
		if (this.list)
			return this.list;
		const out = [];
		for (let off = 0; off + 11 <= this.sec.infoSize;) {
			const h = this.sec.readInfo(off, Math.min(24, this.sec.infoSize - off));
			const len = u32le(h, 0);
			if (len >= 0xfffffff0)
				throw new Error("64-bit DWARF is not supported");
			const ver = u16le(h, 4);
			let unitType = 1, addrSize, abbrevOff, dieOff;
			if (ver >= 5) {
				unitType = h[6];
				addrSize = h[7];
				abbrevOff = u32le(h, 8);
				dieOff = 12 + (unitType === 2 || unitType === 6 ? 12 : unitType === 4 || unitType === 5 ? 8 : 0);
			} else {
				abbrevOff = u32le(h, 6);
				addrSize = h[10];
				dieOff = 11;
			}
			if (ver < 2 || ver > 5)
				throw new Error("DWARF version " + ver + " at .debug_info+" + off.toString(16));
			out.push({ off, end: off + 4 + len, ver, unitType, addrSize, abbrevOff, dieOff: off + dieOff });
			off += 4 + len;
		}
		this.list = out;
		return out;
	}

	unitBytes(i) {
		let b = this.unitCache.get(i);
		if (!b) {
			const u = this.units()[i];
			b = this.sec.readInfo(u.off, u.end - u.off);
			if (this.unitCache.size > 64)
				this.unitCache.clear();
			this.unitCache.set(i, b);
		}
		return b;
	}

	unitOf(absOff) {
		const us = this.units();
		let a = 0, b = us.length;
		while (a < b) { const m = (a + b) >> 1; if (us[m].off <= absOff) a = m + 1; else b = m; }
		return a - 1;
	}

	abbrevs(off) {
		let t = this.abbrevCache.get(off);
		if (t)
			return t;
		t = [];
		const b = this.sec.abbrev;
		let p = off;
		const uleb = () => { let r = 0, s = 0, c; do { c = b[p++]; r += (c & 0x7f) * 2 ** s; s += 7; } while (c & 0x80); return r; };
		const sleb = () => { let r = 0, s = 0, c; do { c = b[p++]; r += (c & 0x7f) * 2 ** s; s += 7; } while (c & 0x80); return c & 0x40 ? r - 2 ** s : r; };
		for (;;) {
			const code = uleb();
			if (!code)
				break;
			const tag = uleb(), children = b[p++];
			const specs = [];
			for (;;) {
				const at = uleb(), form = uleb();
				if (!at && !form)
					break;
				specs.push(at, form, form === 0x21 ? sleb() : 0);
			}
			let sib = -1;
			for (let k = 0; k < specs.length; k += 3)
				if (specs[k] === AT.sibling)
					sib = k;
			t[code] = { tag, children, specs, sib, fixed: null };
		}
		this.abbrevCache.set(off, t);
		return t;
	}

	// A walker over one unit's bytes.
	// The root DIE needs only the start of a unit: read a prefix unless the
	// whole unit is at hand already.
	cursor(i) {
		const u = this.units()[i];
		const b = this.unitCache.get(i) || this.sec.readInfo(u.off, Math.min(u.end - u.off, 16384));
		return { u, b, base: u.off, p: 0, strOff: 8, addrBase: 8, rngBase: 12, lowPc: 0, comp: "" };
	}

	// Read (or skip, when want is false) one attribute value at c.p.
	form(c, form, implicit, want) {
		const b = c.b, u = c.u;
		let p = c.p;
		const uleb = () => { let r = 0, s = 0, x; do { x = b[p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return r; };
		let v = 0;
		switch (form) {
		case 0x01: v = u.addrSize === 4 ? u32le(b, p) : u32le(b, p) + u32le(b, p + 4) * 2 ** 32; p += u.addrSize; break;
		case 0x0b: case 0x0c: case 0x11: v = b[p]; p += 1; break;
		case 0x05: case 0x12: v = u16le(b, p); p += 2; break;
		case 0x06: case 0x0e: case 0x13: case 0x17: case 0x1f: case 0x1c: case 0x1d: case 0x1f20: case 0x1f21:
			v = u32le(b, p); p += 4; break;
		case 0x07: case 0x14: case 0x20: case 0x24: v = u32le(b, p) + u32le(b, p + 4) * 2 ** 32; p += 8; break;
		case 0x10: v = u.ver <= 2 ? u32le(b, p) : u32le(b, p); p += u.ver <= 2 ? u.addrSize : 4; break;
		case 0x0d: { let r = 0, s = 0, x; do { x = b[p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); v = x & 0x40 ? r - 2 ** s : r; break; }
		case 0x0f: case 0x15: case 0x1a: case 0x1b: case 0x22: case 0x23: case 0x1f01: case 0x1f02: v = uleb(); break;
		case 0x08: v = p; while (b[p]) p++; p++; break;
		case 0x09: case 0x18: { const n = uleb(); v = p; p += n; c.blen = n; break; }
		case 0x0a: { const n = b[p]; v = p + 1; p += 1 + n; c.blen = n; break; }
		case 0x03: { const n = u16le(b, p); v = p + 2; p += 2 + n; c.blen = n; break; }
		case 0x04: { const n = u32le(b, p); v = p + 4; p += 4 + n; c.blen = n; break; }
		case 0x19: v = 1; break;
		case 0x21: v = implicit; break;
		case 0x1e: p += 16; break;
		case 0x25: case 0x29: v = b[p]; p += 1; break;
		case 0x26: case 0x2a: v = u16le(b, p); p += 2; break;
		case 0x27: case 0x2b: v = b[p] | b[p + 1] << 8 | b[p + 2] << 16; p += 3; break;
		case 0x28: case 0x2c: v = u32le(b, p); p += 4; break;
		case 0x16: { const f = uleb(); c.p = p; return this.form(c, f, 0, want); }
		default: throw new Error("unknown DWARF form 0x" + form.toString(16));
		}
		c.p = p;
		return v;
	}

	str(c, form, v) {
		switch (form) {
		case 0x08: return cstrAt(c.b, v);
		case 0x0e: return cstrAt(this.sec.str, v);
		case 0x1f: return cstrAt(this.sec.line_str, v);
		case 0x1a: case 0x25: case 0x26: case 0x27: case 0x28: case 0x1f02: {
			const so = this.sec.str_offsets;
			return so ? cstrAt(this.sec.str, u32le(so, c.strOff + v * 4)) : null;
		}
		default: return null;
		}
	}

	addr(c, form, v) {
		if (form === 0x01)
			return v;
		const a = this.sec.addr;
		if (!a)
			return null;
		const o = c.addrBase + v * c.u.addrSize;
		return c.u.addrSize === 4 ? u32le(a, o) : u32le(a, o) + u32le(a, o + 4) * 2 ** 32;
	}

	// DW_AT_ranges -> [[lo, hi], ...]
	ranges(c, form, v) {
		const out = [];
		if (c.u.ver >= 5) {
			const b = this.sec.rnglists;
			if (!b)
				return out;
			let p = form === 0x23 ? c.rngBase + u32le(b, c.rngBase + v * 4) : v;
			const uleb = () => { let r = 0, s = 0, x; do { x = b[p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return r; };
			const ad = () => { const x = u32le(b, p); p += c.u.addrSize; return x; };
			let base = c.lowPc;
			for (;;) {
				const k = b[p++];
				if (k === 0 || k === undefined)
					break;
				if (k === 1) base = this.addr(c, 0x1b, uleb());
				else if (k === 2) { const s = this.addr(c, 0x1b, uleb()); out.push([s, this.addr(c, 0x1b, uleb())]); }
				else if (k === 3) { const s = this.addr(c, 0x1b, uleb()); out.push([s, s + uleb()]); }
				else if (k === 4) { const s = uleb(); out.push([base + s, base + uleb()]); }
				else if (k === 5) base = ad();
				else if (k === 6) { const s = ad(); out.push([s, ad()]); }
				else if (k === 7) { const s = ad(); out.push([s, s + uleb()]); }
				else throw new Error("unknown DW_RLE 0x" + k.toString(16));
			}
		} else {
			const b = this.sec.ranges;
			if (!b)
				return out;
			let base = c.lowPc;
			for (let p = v; p + 8 <= b.length; p += 8) {
				const s = u32le(b, p), e = u32le(b, p + 4);
				if (!s && !e)
					break;
				if (s === 0xffffffff) base = e;
				else out.push([base + s, base + e]);
			}
		}
		return out.filter(([s, e]) => e > s);
	}

	// Attributes of the DIE at c.p as { attr: [form, value] } (all of them);
	// a block (exprloc, blockN) is [form, offset in c.b, length].
	readDie(c, ab) {
		const a = {};
		const s = ab.specs;
		for (let k = 0; k < s.length; k += 3) {
			c.blen = -1;
			const v = this.form(c, s[k + 1], s[k + 2], true);
			a[s[k]] = c.blen >= 0 ? [s[k + 1], v, c.blen] : [s[k + 1], v];
		}
		return a;
	}

	skipDie(c, ab) {
		if (ab.fixed === null) {
			let n = 0;
			for (let k = 0; k < ab.specs.length; k += 3) {
				const z = dwFormSize(ab.specs[k + 1], c.u.addrSize, c.u.ver);
				if (z < 0) { n = -1; break; }
				n += z;
			}
			ab.fixed = n;
		}
		if (ab.fixed >= 0) {
			c.p += ab.fixed;
			return;
		}
		const s = ab.specs;
		for (let k = 0; k < s.length; k += 3)
			this.form(c, s[k + 1], s[k + 2], false);
	}

	// Root DIE: the bases the other DIEs' strx/addrx/rnglistx need, then
	// name, comp_dir, line table offset and address ranges.
	root(i) {
		const hit = this.rootCache.get(i);
		if (hit) {
			const c = Object.assign(this.cursor(i), hit.bases);
			return { ...hit.info, c };
		}
		const c = this.cursor(i);
		c.p = c.u.dieOff - c.base;
		const code = this.uleb(c);
		const ab = this.abbrevs(c.u.abbrevOff)[code];
		if (!ab)
			return { c, tag: 0, ranges: [], stmt: -1 };
		const a = this.readDie(c, ab);
		const dieEnd = c.p;
		if (a[AT.str_offsets_base]) c.strOff = a[AT.str_offsets_base][1];
		if (a[AT.addr_base]) c.addrBase = a[AT.addr_base][1];
		if (a[AT.rnglists_base]) c.rngBase = a[AT.rnglists_base][1];
		const lo = a[AT.low_pc] ? this.addr(c, ...a[AT.low_pc]) : 0;
		c.lowPc = lo || 0;
		let ranges = [];
		if (a[AT.ranges])
			ranges = this.ranges(c, ...a[AT.ranges]);
		else if (a[AT.low_pc] && a[AT.high_pc]) {
			const [f, v] = a[AT.high_pc];
			const hi = dwAddrForm(f) ? this.addr(c, f, v) : lo + v;
			if (hi > lo)
				ranges = [[lo, hi]];
		}
		c.comp = a[AT.comp_dir] ? this.str(c, ...a[AT.comp_dir]) || "" : "";
		const name = a[AT.name] ? this.str(c, ...a[AT.name]) : null;
		c.p = dieEnd;
		const info = { tag: ab.tag, children: ab.children, ranges, name, stmt: a[AT.stmt_list] ? a[AT.stmt_list][1] : -1 };
		this.rootCache.set(i, { info, bases: { p: dieEnd, strOff: c.strOff, addrBase: c.addrBase, rngBase: c.rngBase, lowPc: c.lowPc, comp: c.comp } });
		return { ...info, c };
	}

	uleb(c) {
		const b = c.b;
		let r = 0, s = 0, x;
		do { x = b[c.p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80);
		return r;
	}

	// [[lo, hi, unit index], ...] sorted: from .debug_aranges, else from
	// every compile unit's root DIE.
	unitIndex() {
		const out = [];
		const ar = this.sec.aranges;
		const us = this.units();
		const covered = new Set();
		if (ar) {
			for (let p = 0; p + 16 <= ar.length;) {
				const len = u32le(ar, p), end = p + 4 + len;
				const infoOff = u32le(ar, p + 6), as = ar[p + 10];
				const i = this.unitOf(infoOff);
				let q = p + 12;
				q = Math.ceil(q / (2 * as)) * 2 * as;
				for (; q + 2 * as <= end; q += 2 * as) {
					const s = u32le(ar, q), n = u32le(ar, q + as);
					if (!s && !n)
						break;
					if (n && i >= 0) {
						out.push([s, s + n, i]);
						covered.add(i);
					}
				}
				p = end;
			}
		}
		for (let i = 0; i < us.length; i++)
			if (!covered.has(i) && (us[i].unitType === 1 || us[i].unitType === 3))
				for (const [s, e] of this.root(i).ranges)
					out.push([s, e, i]);
		out.sort((x, y) => x[0] - y[0]);
		return out;
	}

	// The line program at .debug_line+off: { files: [display path], rows:
	// [addr, file, line, end] flat }. File numbers index `files` (0-based).
	lineProgram(off, comp, filesOnly) {
		const h = this.sec.readLine(off, 4);
		const len = u32le(h, 0);
		const b = this.sec.readLine(off, 4 + len);
		let p = 4;
		const uleb = () => { let r = 0, s = 0, x; do { x = b[p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return r; };
		const sleb = () => { let r = 0, s = 0, x; do { x = b[p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return x & 0x40 ? r - 2 ** s : r; };
		const ver = u16le(b, p); p += 2;
		let addrSize = 4;
		if (ver >= 5) { addrSize = b[p]; p += 2; }
		const hlen = u32le(b, p); p += 4;
		const prog = p + hlen;
		const minInst = b[p++];
		if (ver >= 4) p++;
		p++; // default_is_stmt
		const lineBase = (b[p++] << 24) >> 24, lineRange = b[p++], opBase = b[p++];
		const stdLen = b.subarray(p, p + opBase - 1); p += opBase - 1;
		const dirs = [], files = [];
		const fakeC = { b, u: { addrSize, ver: 5 }, p: 0, strOff: 0, addrBase: 0 };
		if (ver >= 5) {
			const entries = (out, isFile) => {
				const nf = b[p++], fmt = [];
				for (let i = 0; i < nf; i++) fmt.push([uleb(), uleb()]);
				const n = uleb();
				for (let i = 0; i < n; i++) {
					let path = "", dir = 0;
					for (const [ct, form] of fmt) {
						fakeC.p = p;
						const v = this.form(fakeC, form, 0, true);
						p = fakeC.p;
						if (ct === 1) path = this.str(fakeC, form, v) || "";
						else if (ct === 2) dir = v;
					}
					out.push(isFile ? [path, dir] : path);
				}
			};
			entries(dirs, false);
			entries(files, true);
		} else {
			dirs.push(comp);
			while (b[p]) { const s = cstrAt(b, p); dirs.push(s); p += s.length + 1; }
			p++;
			files.push(["", 0]); // file 0 does not exist before DWARF 5
			while (b[p]) {
				const s = cstrAt(b, p); p += s.length + 1;
				const d = uleb(); uleb(); uleb();
				files.push([s, d]);
			}
		}
		// Index 0 is the compile directory; a relative entry 0 (prefix-mapped
		// build directory) is dropped: names under it are tree-relative.
		const shown = files.map(([name, d]) => {
			if (!name)
				return "";
			let full = name;
			if (!name.startsWith("/")) {
				const dir = dirs[d] || "";
				if (d === 0 && ver >= 5)
					full = dir.startsWith("/") ? dir + "/" + name : name;
				else
					full = dir ? (dir.startsWith("/") ? dir : (dirs[0] && dirs[0].startsWith("/") ? dirs[0] + "/" : "") + dir) + "/" + name : name;
			}
			return dwDisplayPath(full, comp || (dirs[0] && dirs[0].startsWith("/") ? dirs[0] : ""));
		});
		if (filesOnly)
			return { files: shown, rows: [] };
		const rows = [];
		p = prog;
		const end = 4 + len;
		while (p < end) {
			let addr = 0, file = 1, line = 1;
			for (;;) {
				if (p >= end)
					break;
				const op = b[p++];
				if (op >= opBase) {
					const a = op - opBase;
					addr += Math.floor(a / lineRange) * minInst;
					line += lineBase + (a % lineRange);
					rows.push(addr, file, line, 0);
				} else if (op === 0) {
					const l = uleb(), e = b[p];
					if (e === 1) {
						rows.push(addr, file, line, 1);
						p += l;
						break;
					}
					if (e === 2)
						addr = addrSize === 4 ? u32le(b, p + 1) : u32le(b, p + 1) + u32le(b, p + 5) * 2 ** 32;
					p += l;
				} else switch (op) {
				case 1: rows.push(addr, file, line, 0); break;
				case 2: addr += uleb() * minInst; break;
				case 3: line += sleb(); break;
				case 4: file = uleb(); break;
				case 5: uleb(); break;
				case 8: addr += Math.floor((255 - opBase) / lineRange) * minInst; break;
				case 9: addr += u16le(b, p); p += 2; break;
				case 6: case 7: case 10: case 11: break;
				case 12: uleb(); break;
				default: for (let i = 0; i < stdLen[op - 1]; i++) uleb();
				}
			}
		}
		return { files: shown, rows };
	}

	// Name of the subprogram DIE at absolute offset off: DW_AT_name (else
	// the linkage name), through abstract_origin / specification.
	nameAt(off, depth = 0) {
		if (this.nameCache.has(off))
			return this.nameCache.get(off);
		let name = null;
		const i = this.unitOf(off);
		if (i >= 0 && depth < 8) {
			const c = this.root(i).c;
			c.b = this.unitBytes(i);
			c.p = off - c.base;
			const ab = this.abbrevs(c.u.abbrevOff)[this.uleb(c)];
			if (ab) {
				const a = this.readDie(c, ab);
				const ref = (x) => x[0] === 0x10 || x[0] === 0x1f20 ? x[1] : c.base + x[1];
				const n = a[AT.name] || a[AT.linkage_name] || a[AT.mips_linkage_name];
				if (n)
					name = this.str(c, ...n);
				else if (a[AT.abstract_origin])
					name = this.nameAt(ref(a[AT.abstract_origin]), depth + 1);
				else if (a[AT.specification])
					name = this.nameAt(ref(a[AT.specification]), depth + 1);
			}
		}
		this.nameCache.set(off, name);
		return name;
	}

	// One compile unit: its line rows and the address ranges of its
	// functions (depth 0) and inlined calls (depth 1 + inlined ancestors),
	// with the call site of each inlined call.
	decodeUnit(i) {
		const r = this.root(i);
		const c = r.c;
		c.b = this.unitBytes(i);
		const out = { files: [], rows: [], ranges: [], cover: r.ranges, name: r.name, comp: c.comp };
		if (r.stmt >= 0 && this.sec.readLine)
			Object.assign(out, this.lineProgram(r.stmt, c.comp));
		if (!r.children)
			return out;
		const abbrevs = this.abbrevs(c.u.abbrevOff);
		const end = c.u.end - c.base;
		const inl = [0]; // inlined-call depth at each DIE nesting level
		const ref = (x) => x[0] === 0x10 || x[0] === 0x1f20 ? x[1] : c.base + x[1];
		while (c.p < end && inl.length) {
			const dieOff = c.base + c.p;
			const code = this.uleb(c);
			if (!code) {
				inl.pop();
				continue;
			}
			const ab = abbrevs[code];
			if (!ab)
				throw new Error("bad abbreviation code at .debug_info+" + dieOff.toString(16));
			const tag = ab.tag;
			let depth = inl[inl.length - 1];
			if (tag === DW_TAG_SUBPROGRAM || tag === DW_TAG_INLINED) {
				const a = this.readDie(c, ab);
				if (tag === DW_TAG_SUBPROGRAM)
					depth = 0;
				else
					depth++;
				let rs = [];
				if (a[AT.ranges])
					rs = this.ranges(c, ...a[AT.ranges]);
				else if (a[AT.low_pc] && a[AT.high_pc]) {
					const lo = this.addr(c, ...a[AT.low_pc]);
					const [f, v] = a[AT.high_pc];
					const hi = dwAddrForm(f) ? this.addr(c, f, v) : lo + v;
					if (hi > lo)
						rs = [[lo, hi]];
				}
				if (rs.length) {
					let name = null;
					const n = a[AT.name] || a[AT.linkage_name] || a[AT.mips_linkage_name];
					if (n)
						name = this.str(c, ...n);
					else if (a[AT.abstract_origin])
						name = this.nameAt(ref(a[AT.abstract_origin]));
					else if (a[AT.specification])
						name = this.nameAt(ref(a[AT.specification]));
					const cf = tag === DW_TAG_INLINED && a[AT.call_file] ? a[AT.call_file][1] : -1;
					const cl = tag === DW_TAG_INLINED && a[AT.call_line] ? a[AT.call_line][1] : 0;
					for (const [lo, hi] of rs)
						out.ranges.push(lo, hi, depth, name, cf, cl);
				}
			} else if (ab.children && !DW_CODE_PARENTS.has(tag)) {
				if (ab.sib >= 0) {
					const a = this.readDie(c, ab);
					c.p = ref(a[AT.sibling]) - c.base;
					continue;
				}
				this.skipDie(c, ab);
			} else
				this.skipDie(c, ab);
			if (ab.children)
				inl.push(depth);
		}
		return out;
	}

	// One compile unit's data: the variables with a fixed address
	// (DW_OP_addr / DW_OP_addrx, optionally + DW_OP_plus_uconst; static
	// locals included) and the raw type DIEs, for DwarfTypes.addUnit.
	// Types are { kind, name, size, type (absolute DIE offset or -1 for
	// void), ... } keyed by absolute DIE offset.
	decodeData(i) {
		const r = this.root(i);
		const c = r.c;
		c.b = this.unitBytes(i);
		const files = r.stmt >= 0 && this.sec.readLine ? this.lineProgram(r.stmt, c.comp, true).files : [];
		const out = { types: new Map(), vars: [], files };
		if (!r.children)
			return out;
		const abbrevs = this.abbrevs(c.u.abbrevOff);
		const end = c.u.end - c.base;
		const ref = (x) => x[0] === 0x10 || x[0] === 0x1f20 ? x[1] : c.base + x[1];
		const tref = (a) => a[AT.type] ? ref(a[AT.type]) : -1;
		const name = (a) => a[AT.name] ? this.str(c, ...a[AT.name]) : null;
		const cval = (x) => {
			if (!x)
				return null;
			if (x.length === 3) // a block: little-endian bytes
				return x[2] <= 6 ? [...c.b.subarray(x[1], x[1] + x[2])].reduceRight((v, y) => v * 256 + y, 0) : null;
			return x[1];
		};
		const declared = new Map(); // declaration DIEs of variables, for DW_AT_specification
		const stack = [{ tag: r.tag, raw: null, fn: -1 }];
		while (c.p < end && stack.length) {
			const dieOff = c.base + c.p;
			const code = this.uleb(c);
			if (!code) {
				stack.pop();
				continue;
			}
			const ab = abbrevs[code];
			if (!ab)
				throw new Error("bad abbreviation code at .debug_info+" + dieOff.toString(16));
			const tag = ab.tag;
			const parent = stack[stack.length - 1];
			const kind = DW_TYPE_TAGS[tag];
			let raw = null;
			if (kind) {
				const a = this.readDie(c, ab);
				raw = { kind, name: name(a), size: a[AT.byte_size] ? a[AT.byte_size][1] : -1, type: tref(a) };
				if (a[AT.declaration])
					raw.decl = true;
				if (kind === "base")
					raw.enc = a[AT.encoding] ? a[AT.encoding][1] : 0;
				else if (kind === "struct" || kind === "union" || kind === "enum") {
					raw.list = [];
					if (a[AT.decl_file])
						raw.at = (files[a[AT.decl_file][1]] || "?") + ":" + (a[AT.decl_line] ? a[AT.decl_line][1] : 0);
				} else if (kind === "array")
					raw.dims = [];
				else if (kind === "func")
					raw.params = [];
				out.types.set(dieOff, raw);
			} else if (tag === DW_TAG_MEMBER && parent.raw && parent.raw.list && parent.raw.kind !== "enum") {
				const a = this.readDie(c, ab);
				const loc = a[AT.data_member_location];
				let off = 0;
				if (loc && loc.length === 3) {
					// DWARF 2 style: DW_OP_plus_uconst n
					const q = { b: c.b, p: loc[1] };
					if (c.b[q.p++] === 0x23)
						off = this.uleb(q);
				} else if (loc)
					off = loc[1];
				const m = [name(a), off, tref(a)];
				if (a[AT.bit_size]) {
					const bits = a[AT.bit_size][1];
					let pos;
					if (a[AT.data_bit_offset])
						pos = a[AT.data_bit_offset][1];
					else {
						// DWARF 2/3: bit_offset counts from the most significant bit of a
						// byte_size storage unit at data_member_location.
						const unit = a[AT.byte_size] ? a[AT.byte_size][1] : 0;
						pos = off * 8 + (a[AT.bit_offset] ? unit * 8 - a[AT.bit_offset][1] - bits : 0);
					}
					m[1] = Math.floor(pos / 8);
					m.push(bits, pos % 8);
				}
				parent.raw.list.push(m);
			} else if (tag === DW_TAG_ENUMERATOR && parent.raw && parent.raw.kind === "enum") {
				const a = this.readDie(c, ab);
				// data1/2/4 values stay unsigned: the page compares values modulo
				// the enum's size.
				parent.raw.list.push([name(a), cval(a[AT.const_value])]);
			} else if (tag === DW_TAG_SUBRANGE && parent.raw && parent.raw.kind === "array") {
				const a = this.readDie(c, ab);
				let n = -1;
				if (a[AT.count] && a[AT.count].length === 2 && a[AT.count][0] !== 0x13)
					n = a[AT.count][1];
				else if (a[AT.upper_bound] && a[AT.upper_bound].length === 2 && a[AT.upper_bound][0] !== 0x13) {
					const ub = a[AT.upper_bound];
					// upper_bound -1 (all ones in data forms) is a flexible array: no count.
					const v = ub[0] === 0x0d ? ub[1] : ub[0] === 0x0b && ub[1] === 0xff ? -1 : ub[0] === 0x05 && ub[1] === 0xffff ? -1 :
						ub[0] === 0x06 && ub[1] === 0xffffffff ? -1 : ub[1];
					const lb = a[AT.lower_bound] ? a[AT.lower_bound][1] : 0;
					n = v < 0 ? -1 : v - lb + 1;
				}
				parent.raw.dims.push(n);
			} else if ((tag === DW_TAG_PARAM || tag === DW_TAG_UNSPEC_PARAMS) && parent.raw && parent.raw.kind === "func") {
				if (tag === DW_TAG_PARAM) {
					const a = this.readDie(c, ab);
					parent.raw.params.push(tref(a));
				} else {
					this.skipDie(c, ab);
					parent.raw.varargs = true;
				}
			} else if (tag === DW_TAG_VARIABLE) {
				const a = this.readDie(c, ab);
				if (a[AT.declaration])
					declared.set(dieOff, { name: name(a), type: tref(a), external: !!a[AT.external] });
				const loc = a[AT.location];
				const va = loc && loc.length === 3 ? this.fixedAddr(c, loc[1], loc[2]) : null;
				if (va !== null)
					out.vars.push({ name: name(a), type: tref(a), va, spec: a[AT.specification] ? ref(a[AT.specification]) : -1,
						origin: a[AT.abstract_origin] ? ref(a[AT.abstract_origin]) : -1, external: !!a[AT.external], fn: parent.fn });
			} else
				this.skipDie(c, ab);
			if (ab.children)
				stack.push({ tag, raw, fn: tag === DW_TAG_SUBPROGRAM ? dieOff : parent.fn });
		}
		// A definition through DW_AT_specification / abstract_origin takes the
		// declaration's name and type; static locals name their function.
		for (const v of out.vars) {
			const d = declared.get(v.spec) || declared.get(v.origin);
			if (d) {
				v.name = v.name || d.name;
				if (v.type < 0)
					v.type = d.type;
				v.external = v.external || d.external;
			}
			if (!v.name && v.origin >= 0)
				v.name = this.nameAt(v.origin);
			if (v.type < 0 && v.origin >= 0)
				v.type = this.typeAt(v.origin);
			v.scope = v.fn >= 0 ? this.nameAt(v.fn) : null;
		}
		return out;
	}

	// DW_AT_type of the DIE at absolute offset off (a variable's abstract
	// origin), -1 if none.
	typeAt(off) {
		const i = this.unitOf(off);
		if (i < 0)
			return -1;
		const c = this.root(i).c;
		c.b = this.unitBytes(i);
		c.p = off - c.base;
		const ab = this.abbrevs(c.u.abbrevOff)[this.uleb(c)];
		if (!ab)
			return -1;
		const a = this.readDie(c, ab);
		return a[AT.type] ? (a[AT.type][0] === 0x10 ? a[AT.type][1] : c.base + a[AT.type][1]) : -1;
	}

	// The address a location expression names when it is a fixed one:
	// DW_OP_addr / DW_OP_addrx / DW_OP_GNU_addr_index, optionally followed by
	// DW_OP_plus_uconst; null for anything else (registers, stack values, ...).
	fixedAddr(c, p, len) {
		const b = c.b, end = p + len;
		const q = { b, p: p + 1 };
		let va;
		if (b[p] === 0x03) {
			va = c.u.addrSize === 4 ? u32le(b, q.p) : u32le(b, q.p) + u32le(b, q.p + 4) * 2 ** 32;
			q.p += c.u.addrSize;
		} else if (b[p] === 0xa1 || b[p] === 0xfb)
			va = this.addr(c, 0x1b, this.uleb(q));
		else
			return null;
		if (q.p < end && b[q.p] === 0x23) {
			q.p++;
			va += this.uleb(q);
		}
		return q.p === end && va !== null ? va : null;
	}
}

// ---- types and variables ----------------------------------------------------
//
// GCC repeats every type in every compile unit; DwarfTypes keeps one copy
// of each (named struct/union/enum: by kind, name, size and where it is
// declared; everything else by its structure) and only the types the
// variables reach. The result, { types, vars, sections } as JSON
// (gen-dwarf.js writes it for the shipped kernels, dwarfworker.js builds
// it for an import), is what the page's Variables panel and memory hover
// read:
//   types[id]  [DWT.base, name, size, DW_ATE encoding]
//              [DWT.ptr, target, size]            target -1: void
//              [DWT.const|volatile|restrict|atomic, target]
//              [DWT.typedef, name, target]
//              [DWT.struct|union, name, size, members, decl file:line, definition]
//                 members [name, byte offset, type] (+ bit size, bit position
//                 for a bit field); name "" for an anonymous member; members
//                 null for a declaration, then "definition" is the id of the
//                 full type of the same name when the kernel has one
//              [DWT.enum, name, size, [[name, value], ...], underlying type, decl file:line, definition]
//              [DWT.array, element, [count, ...]]  count -1: unknown (flexible)
//              [DWT.func, return type, [parameter types], varargs]
//              [DWT.void, name]
//   vars       [name, physical address, type, size, section index, flags, function]
//              sorted by address; flags 1: external, 2: per-CPU (the
//              template copy in .data..percpu), 4: a static local; function:
//              the one a static local is in (none when DWARF does not name it:
//              clang puts the statics of an only-inlined function in an
//              empty DW_TAG_subprogram)

// GCC's names of C's integer types, as C programmers (and gdb) write them.
const DW_BASE_NAMES = { "long int": "long", "long unsigned int": "unsigned long", "short int": "short",
	"short unsigned int": "unsigned short", "long long int": "long long", "long long unsigned int": "unsigned long long" };

const DWT = { base: 0, ptr: 1, const: 2, volatile: 3, restrict: 4, atomic: 5, typedef: 6, struct: 7, union: 8, enum: 9,
	array: 10, func: 11, void: 12 };

class DwarfTypes {
	// dw: the DwarfReader, for types another unit holds (DW_FORM_ref_addr).
	constructor(dw) {
		this.dw = dw;
		this.types = [];
		this.keys = new Map();
		this.vars = [];
		this.seen = new Map(); // name@address -> variable
		this.other = new Map(); // unit index -> its decodeData types
	}

	// d: DwarfReader.decodeData of one unit. toPhys: virtual -> physical or
	// null (outside the loaded image); secOf: virtual -> section index.
	addUnit(d, toPhys, secOf) {
		const memoKey = new Map(), memoId = new Map();
		const T = { get: (off) => d.types.get(off) || this.foreign(off) };
		const key = (off) => {
			if (off < 0)
				return "v";
			let k = memoKey.get(off);
			if (k !== undefined)
				return k;
			const t = T.get(off);
			if (!t)
				return "?";
			// A cycle can only pass through a named struct/union/enum (keyed by
			// name, no recursion); the placeholder keeps anything else finite.
			memoKey.set(off, "@" + off);
			switch (t.kind) {
			case "base": k = "b|" + t.name + "|" + t.size + "|" + t.enc; break;
			case "void": k = "0|" + t.name; break;
			case "ptr": case "const": case "volatile": case "restrict": case "atomic": k = t.kind + "|" + key(t.type); break;
			case "typedef": k = "t|" + t.name + "|" + key(t.type); break;
			case "array": k = "a|" + key(t.type) + "|" + t.dims.join(","); break;
			case "func": k = "f|" + key(t.type) + "|" + t.params.map(key).join(",") + "|" + (t.varargs ? 1 : 0); break;
			default:
				if (t.name)
					k = t.kind + "|" + t.name + "|" + (t.decl ? "decl" : t.size + "|" + (t.at || ""));
				else
					k = t.kind + "|" + t.size + "|{" + (t.kind === "enum" ? t.list.map((e) => e.join("=")).join(",") :
						t.list.map((m) => m[0] + "@" + m.slice(1).map((x, j) => j === 1 ? key(x) : x).join(":")).join(",")) + "}";
			}
			memoKey.set(off, k);
			return k;
		};
		const id = (off) => {
			if (off < 0)
				return -1;
			let v = memoId.get(off);
			if (v !== undefined)
				return v;
			const t = T.get(off);
			const k = t ? key(off) : "?";
			v = this.keys.get(k);
			if (v === undefined) {
				v = this.types.length;
				this.keys.set(k, v);
				memoId.set(off, v);
				this.types.push(null);
				this.types[v] = !t ? [DWT.void, "?"] : this.entry(t, id);
			} else
				memoId.set(off, v);
			return v;
		};
		for (const x of d.vars) {
			const pa = toPhys(x.va);
			if (pa === null || !x.name)
				continue;
			const k = x.name + "@" + pa;
			const had = this.seen.get(k);
			if (had) {
				// Another unit's DIE for it: keep a complete type over an
				// incomplete one (an array without a size: extern const char x[]).
				const T = { types: this.types };
				if (!dwTypeSize(T, had[2]) && x.type >= 0) {
					const t = id(x.type);
					if (dwTypeSize(T, t))
						had[2] = t;
				}
				continue;
			}
			const sec = secOf(x.va);
			const v = [x.name, pa, id(x.type), 0, sec[0], (x.external ? 1 : 0) | (sec[1] ? 2 : 0) | (x.fn >= 0 ? 4 : 0), x.scope || ""];
			this.seen.set(k, v);
			this.vars.push(v);
		}
	}

	foreign(off) {
		const i = this.dw ? this.dw.unitOf(off) : -1;
		if (i < 0)
			return undefined;
		let m = this.other.get(i);
		if (!m) {
			if (this.other.size > 8)
				this.other.clear();
			m = this.dw.decodeData(i).types;
			this.other.set(i, m);
		}
		return m.get(off);
	}

	entry(t, id) {
		switch (t.kind) {
		case "base": return [DWT.base, DW_BASE_NAMES[t.name] || t.name, t.size, t.enc];
		case "void": return [DWT.void, t.name || "void"];
		case "ptr": return [DWT.ptr, id(t.type), t.size > 0 ? t.size : 4];
		case "typedef": return [DWT.typedef, t.name, id(t.type)];
		case "array": return [DWT.array, id(t.type), t.dims.length ? t.dims : [-1]];
		case "func": return [DWT.func, id(t.type), t.params.map(id), t.varargs ? 1 : 0];
		case "struct": case "union":
			return [DWT[t.kind], t.name || "", t.decl ? -1 : t.size, t.decl ? null : t.list.map((m) => {
				const e = [m[0] || "", m[1], id(m[2])];
				if (m.length > 3)
					e.push(m[3], m[4]);
				return e;
			}), t.at || ""];
		case "enum": return [DWT.enum, t.name || "", t.decl ? -1 : t.size, t.decl ? null : t.list, id(t.type), t.at || ""];
		default: return [DWT[t.kind], id(t.type)];
		}
	}

	// { format, types, vars, sections }: declarations point to the
	// definition of the same name, vars get their sizes and are sorted.
	finish(sections) {
		const defs = new Map();
		this.types.forEach((t, k) => {
			if ((t[0] === DWT.struct || t[0] === DWT.union || t[0] === DWT.enum) && t[1] && t[3] && !defs.has(t[0] + t[1]))
				defs.set(t[0] + t[1], k);
		});
		for (const t of this.types)
			if ((t[0] === DWT.struct || t[0] === DWT.union || t[0] === DWT.enum) && !t[3] && defs.has(t[0] + t[1]))
				t[t[0] === DWT.enum ? 6 : 5] = defs.get(t[0] + t[1]);
		const out = { format: 1, types: this.types, vars: this.vars, sections };
		for (const v of this.vars) {
			v[3] = dwTypeSize(out, v[2]);
			if (!v[6])
				v.length = 6;
		}
		this.vars.sort((x, y) => x[1] - y[1] || y[3] - x[3]);
		return out;
	}
}

// The type behind typedefs and qualifiers (and a declaration's definition).
function dwStrip(T, id) {
	for (let n = 0; id >= 0 && n < 64; n++) {
		const t = T.types[id];
		if (t[0] === DWT.typedef || (t[0] >= DWT.const && t[0] <= DWT.atomic))
			id = t[t[0] === DWT.typedef ? 2 : 1];
		else if ((t[0] === DWT.struct || t[0] === DWT.union) && !t[3] && t[5] !== undefined)
			id = t[5];
		else if (t[0] === DWT.enum && !t[3] && t[6] !== undefined)
			id = t[6];
		else
			return id;
	}
	return id;
}

// Bytes of a type; 0 when unknown (void, a declaration only, a flexible array).
function dwTypeSize(T, id) {
	id = dwStrip(T, id);
	if (id < 0)
		return 0;
	const t = T.types[id];
	switch (t[0]) {
	case DWT.base: case DWT.ptr: return t[2];
	case DWT.struct: case DWT.union: case DWT.enum: return t[2] > 0 ? t[2] : 0;
	case DWT.array: return t[2].some((n) => n < 0) ? 0 : t[2].reduce((x, n) => x * n, dwTypeSize(T, t[1]));
	default: return 0;
	}
}

// C spelling of a type, as gdb's whatis prints it: "struct task_struct",
// "char [16]", "void (*)(struct work_struct *)", "const char *".
function dwTypeName(T, id, inner = "") {
	const sp = (s) => s ? " " + s : "";
	if (id < 0)
		return "void" + sp(inner);
	const t = T.types[id];
	switch (t[0]) {
	case DWT.base: case DWT.typedef: case DWT.void: return t[1] + sp(inner);
	case DWT.struct: case DWT.union: case DWT.enum:
		return (t[0] === DWT.struct ? "struct " : t[0] === DWT.union ? "union " : "enum ") + (t[1] || "{...}") + sp(inner);
	case DWT.ptr: {
		const to = t[1] >= 0 ? T.types[t[1]][0] : -1;
		return dwTypeName(T, t[1], to === DWT.array || to === DWT.func ? "(*" + inner + ")" : "*" + inner);
	}
	case DWT.const: case DWT.volatile: case DWT.restrict: case DWT.atomic: {
		const q = ["const", "volatile", "restrict", "_Atomic"][t[0] - DWT.const];
		const to = t[1] >= 0 ? T.types[t[1]] : null;
		if (to && to[0] === DWT.ptr)
			return dwTypeName(T, t[1], q + sp(inner));
		// A qualified array is an array of qualified elements (GCC says both).
		if (to && to[0] === DWT.array && to[1] >= 0 && T.types[to[1]][0] === t[0])
			return dwTypeName(T, t[1], inner);
		return q + " " + dwTypeName(T, t[1], inner);
	}
	case DWT.array: return dwTypeName(T, t[1], inner + t[2].map((n) => "[" + (n >= 0 ? n : "") + "]").join(""));
	case DWT.func: {
		const ps = t[2].map((p) => dwTypeName(T, p));
		if (t[3])
			ps.push("...");
		return dwTypeName(T, t[1], inner + "(" + (ps.length ? ps.join(", ") : "void") + ")");
	}
	default: return "?" + sp(inner);
	}
}

// Index of the last variable starting at or below pa, -1 if none.
function dwVarIndex(T, pa) {
	const v = T.vars;
	let a = 0, b = v.length;
	while (a < b) { const m = (a + b) >> 1; if (v[m][1] <= pa) a = m + 1; else b = m; }
	return a - 1;
}

// The variable holding byte pa and the path to it, innermost member or
// element last: { var, path: "init_task.comm[3]" (a C expression), note
// ("padding", "bits 3-5"), type, offset (of pa in the leaf), leafAddr,
// unions: the other members of a union there } or null.
function dwWhere(T, pa) {
	if (T.maxSize === undefined)
		T.maxSize = T.vars.reduce((x, v) => Math.max(x, v[3]), 1);
	let best = null;
	for (let k = dwVarIndex(T, pa); k >= 0 && T.vars[k][1] > pa - T.maxSize - 1; k--) {
		// The smallest variable holding pa; one of size 0 (an empty struct,
		// unknown size) only names its first byte when no other holds it.
		const v = T.vars[k];
		if (v[3] ? pa < v[1] + v[3] && (!best || !best[3] || v[3] < best[3]) : pa === v[1] && !best)
			best = v;
	}
	if (!best)
		return null;
	let path = best[0], id = best[2], off = pa - best[1], note = "";
	const unions = [];
	for (let n = 0; n < 32; n++) {
		const s = dwStrip(T, id);
		if (s < 0)
			break;
		const t = T.types[s];
		if ((t[0] === DWT.struct || t[0] === DWT.union) && t[3]) {
			const hits = t[3].filter((m) => {
				const sz = m.length > 3 ? Math.ceil((m[4] + m[3]) / 8) : dwTypeSize(T, m[2]);
				return off >= m[1] && off < m[1] + Math.max(sz, 1);
			});
			if (!hits.length) {
				note = "padding";
				break;
			}
			const m = hits[0];
			if (t[0] === DWT.union && hits.length > 1)
				unions.push(...hits.slice(1).map((x) => x[0] || "{...}"));
			if (m[0])
				path += "." + m[0];
			off -= m[1];
			id = m[2];
			if (m.length > 3) {
				note = m[3] === 1 ? "bit " + m[4] : "bits " + m[4] + "\u2013" + (m[4] + m[3] - 1);
				break;
			}
		} else if (t[0] === DWT.array) {
			const es = dwTypeSize(T, t[1]);
			if (!es)
				break;
			const dims = t[2];
			let i = Math.floor(off / es);
			off -= i * es;
			const idx = [];
			for (let d = dims.length - 1; d >= 0; d--) {
				const n = dims[d] > 0 ? dims[d] : Infinity;
				idx.unshift(d ? i % n : i);
				i = Math.floor(i / n);
			}
			path += idx.map((x) => "[" + x + "]").join("");
			id = t[1];
		} else
			break;
	}
	return { var: best, path, note, type: id, offset: off, leafAddr: pa - off, unions };
}

// ---- ELF -----------------------------------------------------------------

// PT_LOAD segments and sections of an ELF32 little-endian file, through
// read(off, len) -> Uint8Array (a File slice in the Worker, fs in node).
function dwarfElf(read) {
	const h = read(0, 52);
	if (u32le(h, 0) !== 0x464c457f)
		throw new Error("not an ELF file");
	if (h[4] !== 1 || h[5] !== 1)
		throw new Error("not a 32-bit little-endian ELF (only i386 kernels boot here)");
	const phoff = u32le(h, 0x1c), shoff = u32le(h, 0x20);
	const phentsize = u16le(h, 0x2a), phnum = u16le(h, 0x2c), shentsize = u16le(h, 0x2e), shnum = u16le(h, 0x30);
	const shstrndx = u16le(h, 0x32);
	const ph = read(phoff, phentsize * phnum);
	const loads = [];
	for (let i = 0; i < phnum; i++) {
		const o = i * phentsize;
		if (u32le(ph, o) === 1 && u32le(ph, o + 20))
			loads.push([u32le(ph, o + 8), u32le(ph, o + 8) + u32le(ph, o + 20), u32le(ph, o + 12)]);
	}
	const sh = read(shoff, shentsize * shnum);
	const secs = [];
	for (let i = 0; i < shnum; i++) {
		const o = i * shentsize;
		secs.push({ name: u32le(sh, o), type: u32le(sh, o + 4), flags: u32le(sh, o + 8), addr: u32le(sh, o + 12),
			offset: u32le(sh, o + 16), size: u32le(sh, o + 20) });
	}
	const strs = secs[shstrndx] ? read(secs[shstrndx].offset, secs[shstrndx].size) : new Uint8Array(0);
	const sections = {};
	const alloc = []; // [name, addr, end] of the sections in memory (SHF_ALLOC), .bss included
	for (const s of secs) {
		const name = cstrAt(strs, s.name);
		if (s.type !== 8) // SHT_NOBITS
			sections[name] = s;
		if (s.flags & 2 && s.size)
			alloc.push([name, s.addr, s.addr + s.size]);
	}
	// Virtual -> physical through the PT_LOAD segments; null outside them.
	const toPhys = (va) => {
		for (const [lo, hi, pa] of loads)
			if (va >= lo && va < hi)
				return va - lo + pa;
		return null;
	};
	return { loads, sections, alloc, toPhys };
}

// For DwarfTypes.addUnit: virtual address -> [index in names, per-CPU],
// names collecting the section names met.
function dwarfSectionOf(elf, names) {
	const ix = new Map();
	return (va) => {
		const s = elf.alloc.find(([, lo, hi]) => va >= lo && va < hi);
		const n = s ? s[0] : "?";
		if (!ix.has(n)) {
			ix.set(n, names.length);
			names.push(n);
		}
		return [ix.get(n), n === ".data..percpu"];
	};
}

// The DwarfReader input for an ELF: small sections read whole, .debug_info
// and .debug_line read a unit or a line program at a time. null when the
// ELF has no .debug_info.
function dwarfSections(elf, read) {
	const s = elf.sections;
	const whole = (n) => s[n] && s[n].size ? read(s[n].offset, s[n].size) : null;
	const part = (n) => s[n] ? (off, len) => read(s[n].offset + off, Math.max(0, Math.min(len, s[n].size - off))) : null;
	if (!s[".debug_info"] || !s[".debug_info"].size)
		return null;
	return {
		readInfo: part(".debug_info"), infoSize: s[".debug_info"].size,
		readLine: part(".debug_line"), lineSize: s[".debug_line"] ? s[".debug_line"].size : 0,
		abbrev: whole(".debug_abbrev"), str: whole(".debug_str"), line_str: whole(".debug_line_str"),
		str_offsets: whole(".debug_str_offsets"), addr: whole(".debug_addr"), rnglists: whole(".debug_rnglists"),
		ranges: whole(".debug_ranges"), aranges: whole(".debug_aranges"),
	};
}

// ---- tables: what lookups read ------------------------------------------
//
// A table covers some address range: sorted line rows (addr, file string
// index or -1 for "no line information", line) and function/inlined-call
// ranges (lo, hi, depth, name, call file, call line). Addresses are
// physical (what the page works in). Built from decoded units by
// dwarfTable(), stored as shards by gen-dwarf.js (dwarfEncode), read back
// by dwarfDecode.

// units: [{ files, rows, ranges }] with addresses mapped by toPhys (null
// drops the row or range: code outside the loaded image, e.g. discarded
// sections at 0).
function dwarfTable(units, toPhys, strings = [], strIdx = new Map()) {
	const sid = (s) => {
		if (s === null || s === undefined || s === "")
			return -1;
		let k = strIdx.get(s);
		if (k === undefined) {
			k = strings.length;
			strings.push(s);
			strIdx.set(s, k);
		}
		return k;
	};
	let n = 0;
	for (const u of units)
		n += u.rows.length / 4 + (u.cover ? u.cover.length : 0);
	// Rows: addr, file, line, kind (0 end of sequence first at equal
	// addresses, then the rows in program order: the last one wins).
	let ra = new Float64Array(n), rf = new Int32Array(n), rl = new Uint32Array(n), rk = new Float64Array(n);
	let m = 0, order = 0;
	for (const u of units) {
		const fid = u.files.map(sid);
		// Line rows count only inside the unit's own address ranges (what
		// llvm-symbolizer looks up first): an end marker closes each range,
		// rows outside them are dropped. A sequence that starts outside the
		// image (discarded code at 0) stays out.
		const cover = [];
		for (const [lo, hi] of (u.cover || []).slice().sort((x, y) => x[0] - y[0]))
			if (cover.length && lo <= cover[cover.length - 1][1])
				cover[cover.length - 1][1] = Math.max(hi, cover[cover.length - 1][1]);
			else
				cover.push([lo, hi]);
		const inCover = (va) => {
			if (!u.cover)
				return true;
			let a = 0, b = cover.length;
			while (a < b) { const q = (a + b) >> 1; if (cover[q][0] <= va) a = q + 1; else b = q; }
			return a > 0 && va < cover[a - 1][1];
		};
		const rows = u.rows.slice();
		for (const [, e] of cover)
			rows.push(e, 0, 0, 1);
		let start = true, ok = true;
		for (let k = 0; k < rows.length; k += 4) {
			if (start)
				ok = toPhys(rows[k]) !== null;
			start = !!rows[k + 3];
			if (!rows[k + 3] && !inCover(rows[k]))
				continue;
			const pa = !ok ? null : rows[k + 3] ? (toPhys(rows[k] - 1) ?? -1) + 1 : toPhys(rows[k]);
			if (!pa)
				continue;
			const end = rows[k + 3];
			ra[m] = pa;
			rf[m] = end ? -2 : fid[rows[k + 1]] ?? -1;
			rl[m] = end ? 0 : rows[k + 2];
			rk[m] = end ? 0 : 1 + order++;
			m++;
		}
	}
	const idx = new Uint32Array(m);
	for (let k = 0; k < m; k++)
		idx[k] = k;
	idx.sort((x, y) => ra[x] - ra[y] || rk[x] - rk[y]);
	const la = [], lf = [], ll = [];
	for (let q = 0; q < m; q++) {
		const k = idx[q];
		const a = ra[k], f = rf[k] === -2 ? -1 : rf[k], l = rf[k] === -2 ? 0 : rl[k];
		const last = la.length - 1;
		if (last >= 0 && la[last] === a) {
			lf[last] = f;
			ll[last] = l;
			if (last > 0 && lf[last - 1] === f && ll[last - 1] === l) {
				la.pop(); lf.pop(); ll.pop();
			}
			continue;
		}
		if (last >= 0 && lf[last] === f && ll[last] === l)
			continue;
		la.push(a); lf.push(f); ll.push(l);
	}
	ra = rf = rl = rk = null;
	const rg = [];
	for (const u of units) {
		const fid = u.files;
		const r = u.ranges;
		for (let k = 0; k < r.length; k += 6) {
			const lo = toPhys(r[k]);
			if (lo === null)
				continue;
			rg.push([lo, lo + (r[k + 1] - r[k]), r[k + 2], sid(r[k + 3] || "?"), r[k + 4] >= 0 ? sid(fid[r[k + 4]]) : -1, r[k + 5]]);
		}
	}
	rg.sort((x, y) => x[0] - y[0] || x[2] - y[2]);
	return dwarfPack(strings, la, lf, ll, rg);
}

function dwarfPack(strings, la, lf, ll, rg) {
	const t = { strings, la: Float64Array.from(la), lf: Int32Array.from(lf), ll: Uint32Array.from(ll),
		ra: new Float64Array(rg.length), rh: new Float64Array(rg.length), rd: new Uint8Array(rg.length),
		rn: new Int32Array(rg.length), rf: new Int32Array(rg.length), rl: new Uint32Array(rg.length), maxLen: 0 };
	rg.forEach((x, k) => {
		t.ra[k] = x[0]; t.rh[k] = x[1]; t.rd[k] = Math.min(255, x[2]); t.rn[k] = x[3]; t.rf[k] = x[4]; t.rl[k] = x[5];
		t.maxLen = Math.max(t.maxLen, x[1] - x[0]);
	});
	return t;
}

// The part of table t for [lo, hi): the line row in force at lo is
// repeated at lo, ranges overlapping it are kept. Strings are re-indexed.
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
	const la = [], lf = [], ll = [];
	let a = dwarfRow(t, lo);
	if (a < 0)
		a = 0;
	for (let k = a; k < t.la.length && t.la[k] < hi; k++) {
		la.push(Math.max(lo, t.la[k]));
		lf.push(sid(t.lf[k]));
		ll.push(t.ll[k]);
	}
	const rg = [];
	for (let k = 0; k < t.ra.length; k++)
		if (t.ra[k] < hi && t.rh[k] > lo)
			rg.push([t.ra[k], t.rh[k], t.rd[k], sid(t.rn[k]), sid(t.rf[k]), t.rl[k]]);
	return dwarfPack(strings, la, lf, ll, rg);
}

// Index of the line row in force at pa, or -1.
function dwarfRow(t, pa) {
	let a = 0, b = t.la.length;
	while (a < b) { const m = (a + b) >> 1; if (t.la[m] <= pa) a = m + 1; else b = m; }
	return a - 1;
}

// Frames at physical address pa, innermost first: [{ func, file, line }]
// (func null: no DWARF function here, e.g. assembly; file null: no line
// information), or null when the table knows nothing about pa.
function dwarfFrames(t, pa) {
	const k = dwarfRow(t, pa);
	const file = k >= 0 && t.lf[k] >= 0 ? t.strings[t.lf[k]] : null;
	const line = k >= 0 && t.lf[k] >= 0 ? t.ll[k] : 0;
	// Ranges covering pa, the innermost (latest start) per depth.
	let a = 0, b = t.ra.length;
	while (a < b) { const m = (a + b) >> 1; if (t.ra[m] <= pa) a = m + 1; else b = m; }
	const byDepth = [];
	for (let q = a - 1; q >= 0 && t.ra[q] >= pa - t.maxLen; q--)
		if (t.rh[q] > pa && byDepth[t.rd[q]] === undefined)
			byDepth[t.rd[q]] = q;
	let deepest = -1;
	while (byDepth[deepest + 1] !== undefined)
		deepest++;
	if (deepest < 0)
		return k >= 0 && file !== null ? [{ func: null, file, line }] : null;
	const frames = [];
	for (let d = deepest; d >= 0; d--) {
		const q = byDepth[d];
		const func = t.strings[t.rn[q]] || null;
		if (d === deepest)
			frames.push({ func, file, line });
		else {
			const inner = byDepth[d + 1];
			frames.push({ func, file: t.rf[inner] >= 0 ? t.strings[t.rf[inner]] : null, line: t.rf[inner] >= 0 ? t.rl[inner] : 0 });
		}
	}
	return frames;
}

// [lo, hi) of the outermost function (depth 0) covering pa, or null.
function dwarfFunction(t, pa) {
	let a = 0, b = t.ra.length;
	while (a < b) { const m = (a + b) >> 1; if (t.ra[m] <= pa) a = m + 1; else b = m; }
	for (let q = a - 1; q >= 0 && t.ra[q] >= pa - t.maxLen; q--)
		if (t.rd[q] === 0 && t.rh[q] > pa)
			return [t.ra[q], t.rh[q]];
	return null;
}

// ---- shard files ----------------------------------------------------------
// "MKBD" 1, strings (count, then NUL-terminated UTF-8), line rows (count,
// then address delta, file+1 zigzag delta, line zigzag delta), ranges
// (count, then start delta, length, depth, name+1, call file+1, call line),
// all unsigned LEB128.

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
	const zz = (v) => uv(v < 0 ? -2 * v - 1 : 2 * v);
	out.push(0x4d, 0x4b, 0x42, 0x44, 1);
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
		zz(t.lf[k] + 1 - pf); pf = t.lf[k] + 1;
		zz(t.ll[k] - pl); pl = t.ll[k];
	}
	uv(t.ra.length);
	pa = 0;
	for (let k = 0; k < t.ra.length; k++) {
		uv(t.ra[k] - pa); pa = t.ra[k];
		uv(t.rh[k] - t.ra[k]); uv(t.rd[k]); uv(t.rn[k] + 1); uv(t.rf[k] + 1); uv(t.rl[k]);
	}
	return Uint8Array.from(out);
}

function dwarfDecode(b) {
	if (b[0] !== 0x4d || b[1] !== 0x4b || b[2] !== 0x42 || b[3] !== 0x44 || b[4] !== 1)
		throw new Error("not a DWARF shard (MKBD 1)");
	let p = 5;
	const uv = () => { let r = 0, s = 0, x; do { x = b[p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return r; };
	const zz = () => { const v = uv(); return v % 2 ? -(v + 1) / 2 : v / 2; };
	const strings = [];
	for (let n = uv(); n > 0; n--) {
		let e = p;
		while (b[e]) e++;
		strings.push(textDecoder.decode(b.subarray(p, e)));
		p = e + 1;
	}
	const nl = uv();
	const la = new Float64Array(nl), lf = new Int32Array(nl), ll = new Uint32Array(nl);
	let pa = 0, pf = 0, pl = 0;
	for (let k = 0; k < nl; k++) {
		pa += uv(); la[k] = pa;
		pf += zz(); lf[k] = pf - 1;
		pl += zz(); ll[k] = pl;
	}
	const nr = uv();
	const t = { strings, la, lf, ll, ra: new Float64Array(nr), rh: new Float64Array(nr), rd: new Uint8Array(nr),
		rn: new Int32Array(nr), rf: new Int32Array(nr), rl: new Uint32Array(nr), maxLen: 0 };
	pa = 0;
	for (let k = 0; k < nr; k++) {
		pa += uv(); t.ra[k] = pa;
		t.rh[k] = pa + uv(); t.rd[k] = uv(); t.rn[k] = uv() - 1; t.rf[k] = uv() - 1; t.rl[k] = uv();
		t.maxLen = Math.max(t.maxLen, t.rh[k] - pa);
	}
	return t;
}

if (typeof module !== "undefined")
	module.exports = { DwarfTypes, DWT, dwStrip, dwTypeSize, dwTypeName, dwWhere, dwVarIndex, DwarfReader, dwarfElf, dwarfSectionOf, dwarfSections, dwarfTable, dwarfSlice, dwarfFrames, dwarfFunction, dwarfRow, dwarfEncode, dwarfDecode, dwDisplayPath };
