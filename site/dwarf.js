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
	encoding: 0x3e, external: 0x3f, type: 0x49, data_bit_offset: 0x6b, frame_base: 0x40, loclists_base: 0x8c,
	inline: 0x20, prototyped: 0x27, noreturn: 0x87 };
const DW_TAG_LEXICAL_BLOCK = 0x0b;
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
		return { u, b, base: u.off, p: 0, strOff: 8, addrBase: 8, rngBase: 12, locBase: 12, lowPc: 0, comp: "" };
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

	// A location list (DW_AT_location in sec_offset / loclistx form) ->
	// [[lo, hi, expression bytes], ...] (virtual addresses; a default
	// location, DW_LLE_default_location, has lo 0 and hi 2^32), or null
	// when the section is missing.
	locList(c, form, v) {
		const out = [];
		if (c.u.ver >= 5) {
			if (this.sec.loclists && typeof this.sec.loclists === "function")
				this.sec.loclists = this.sec.loclists();
			const b = this.sec.loclists;
			if (!b)
				return null;
			let p = form === 0x22 ? c.locBase + u32le(b, c.locBase + v * 4) : v;
			const uleb = () => { let r = 0, s = 0, x; do { x = b[p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return r; };
			const ad = () => { const x = u32le(b, p); p += c.u.addrSize; return x; };
			const expr = () => { const n = uleb(); const e = b.slice(p, p + n); p += n; return e; };
			let base = c.lowPc;
			for (let guard = 0; guard < 100000; guard++) {
				const k = b[p++];
				if (k === 0 || k === undefined)
					break;
				if (k === 1) base = this.addr(c, 0x1b, uleb());
				else if (k === 2) { const s = this.addr(c, 0x1b, uleb()), e = this.addr(c, 0x1b, uleb()); out.push([s, e, expr()]); }
				else if (k === 3) { const s = this.addr(c, 0x1b, uleb()), n = uleb(); out.push([s, s + n, expr()]); }
				else if (k === 4) { const s = uleb(), e = uleb(); out.push([base + s, base + e, expr()]); }
				else if (k === 5) out.push([0, 2 ** 32, expr()]);
				else if (k === 6) base = ad();
				else if (k === 7) { const s = ad(), e = ad(); out.push([s, e, expr()]); }
				else if (k === 8) { const s = ad(), n = uleb(); out.push([s, s + n, expr()]); }
				else if (k === 9) { uleb(); uleb(); } // DW_LLE_GNU_view_pair: views only
				else throw new Error("unknown DW_LLE 0x" + k.toString(16));
			}
		} else {
			if (this.sec.loc && typeof this.sec.loc === "function")
				this.sec.loc = this.sec.loc();
			const b = this.sec.loc;
			if (!b)
				return null;
			let base = c.lowPc;
			for (let p = v; p + 8 <= b.length;) {
				const s = u32le(b, p), e = u32le(b, p + 4);
				p += 8;
				if (!s && !e)
					break;
				if (s === 0xffffffff) {
					base = e;
					continue;
				}
				const n = u16le(b, p);
				out.push([base + s, base + e, b.slice(p + 2, p + 2 + n)]);
				p += 2 + n;
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
		if (a[AT.loclists_base]) c.locBase = a[AT.loclists_base][1];
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
		this.rootCache.set(i, { info, bases: { p: dieEnd, strOff: c.strOff, addrBase: c.addrBase, rngBase: c.rngBase, locBase: c.locBase,
			lowPc: c.lowPc, comp: c.comp } });
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
	// [addr, file, line, kind] flat } with kind 0 a row, 2 a row with
	// is_stmt (a recommended breakpoint address), 1 the end of a sequence.
	// File numbers index `files` (0-based).
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
		const defStmt = b[p++] ? 2 : 0;
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
			let addr = 0, file = 1, line = 1, stmt = defStmt;
			for (;;) {
				if (p >= end)
					break;
				const op = b[p++];
				if (op >= opBase) {
					const a = op - opBase;
					addr += Math.floor(a / lineRange) * minInst;
					line += lineBase + (a % lineRange);
					rows.push(addr, file, line, stmt);
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
				case 1: rows.push(addr, file, line, stmt); break;
				case 2: addr += uleb() * minInst; break;
				case 3: line += sleb(); break;
				case 4: file = uleb(); break;
				case 5: uleb(); break;
				case 8: addr += Math.floor((255 - opBase) / lineRange) * minInst; break;
				case 9: addr += u16le(b, p); p += 2; break;
				case 6: stmt ^= 2; break;
				case 7: case 10: case 11: break;
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

	// ---- function facts (DwarfFuncs) ----------------------------------------
	// What a DW_TAG_subprogram DIE says about its function, as it says it
	// (a concrete DIE with an abstract origin says almost nothing itself):
	// { name, file (display path), line, ext, inl (DW_AT_inline or null),
	// decl, noret, proto, ret (type DIE offset, null: none given), origin,
	// spec (DIE offsets, -1: none), params: [[name, type offset or null]],
	// varargs, rs: [[lo, hi], ...] virtual (set by the caller) }.
	subRecord(c, a, ref, files) {
		const n = a[AT.name] || a[AT.linkage_name] || a[AT.mips_linkage_name];
		const fi = a[AT.decl_file] ? a[AT.decl_file][1] : -1;
		return { name: n ? this.str(c, ...n) : null, file: fi >= 0 && files[fi] ? files[fi] : null,
			line: a[AT.decl_line] ? a[AT.decl_line][1] : 0, ext: !!a[AT.external], inl: a[AT.inline] ? a[AT.inline][1] : null,
			decl: !!a[AT.declaration], noret: !!a[AT.noreturn], proto: !!a[AT.prototyped], ret: a[AT.type] ? ref(a[AT.type]) : null,
			origin: a[AT.abstract_origin] ? ref(a[AT.abstract_origin]) : -1, spec: a[AT.specification] ? ref(a[AT.specification]) : -1,
			params: [], varargs: false, rs: [] };
	}

	// The display paths of unit i's line program (what DW_AT_decl_file indexes).
	unitFiles(i) {
		if (!this.filesCache)
			this.filesCache = new Map();
		let f = this.filesCache.get(i);
		if (!f) {
			const r = this.root(i);
			f = r.stmt >= 0 && this.sec.readLine ? this.lineProgram(r.stmt, r.c.comp, true).files : [];
			this.filesCache.set(i, f);
		}
		return f;
	}

	// subRecord of the DW_TAG_subprogram DIE at absolute offset off in any
	// unit, with its parameters (an abstract origin or declaration that
	// clang puts in another unit, DW_FORM_ref_addr); null if it is none.
	subprogramAt(off) {
		if (!this.subCache)
			this.subCache = new Map();
		if (this.subCache.has(off))
			return this.subCache.get(off);
		let rec = null;
		const i = this.unitOf(off);
		if (i >= 0) {
			const c = this.root(i).c;
			c.b = this.unitBytes(i);
			c.p = off - c.base;
			const abbrevs = this.abbrevs(c.u.abbrevOff);
			const ab = abbrevs[this.uleb(c)];
			if (ab && ab.tag === DW_TAG_SUBPROGRAM) {
				const ref = (x) => x[0] === 0x10 || x[0] === 0x1f20 ? x[1] : c.base + x[1];
				rec = this.subRecord(c, this.readDie(c, ab), ref, this.unitFiles(i));
				for (let depth = 0; ab.children && c.p < c.b.length;) {
					const code = this.uleb(c);
					if (!code) {
						if (--depth < 0)
							break;
						continue;
					}
					const k = abbrevs[code];
					if (!k)
						break;
					if (depth === 0 && k.tag === DW_TAG_PARAM) {
						const a = this.readDie(c, k);
						rec.params.push([a[AT.name] ? this.str(c, ...a[AT.name]) : null, a[AT.type] ? ref(a[AT.type]) : null]);
					} else {
						this.skipDie(c, k);
						if (depth === 0 && k.tag === DW_TAG_UNSPEC_PARAMS)
							rec.varargs = true;
					}
					if (k.children)
						depth++;
				}
			}
		}
		if (this.subCache.size > 50000)
			this.subCache.clear();
		this.subCache.set(off, rec);
		return rec;
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
	// void), ... } keyed by absolute DIE offset. With locals, also the
	// scopes of the code (dwarfLocals reads them): see collectScopes().
	decodeData(i, locals = false) {
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
		const L = locals ? { scopes: [], info: new Map(), kids: new Map(), lastKid: null, subs: new Map(), copies: [], files } : null;
		const stack = [{ tag: r.tag, raw: null, fn: -1, scope: -1, die: -1 }];
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
			let raw = null, scope = parent.scope;
			if (L) {
				L.lastKid = null;
				if (parent.die >= 0) {
					L.lastKid = [dieOff, tag, -1];
					const k = L.kids.get(parent.die);
					if (k)
						k.push(L.lastKid);
					else
						L.kids.set(parent.die, [L.lastKid]);
				}
			}
			if (L && (tag === DW_TAG_SUBPROGRAM || tag === DW_TAG_INLINED || tag === DW_TAG_LEXICAL_BLOCK)) {
				scope = this.scopeDie(c, ab, tag, dieOff, parent, L, ref);
			} else if (kind) {
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
			} else if (L && tag === DW_TAG_PARAM && (parent.tag === DW_TAG_SUBPROGRAM || parent.tag === DW_TAG_INLINED)) {
				const a = this.readDie(c, ab);
				this.localDie(c, a, true, dieOff, parent, L, ref);
				const sub = parent.tag === DW_TAG_SUBPROGRAM ? L.subs.get(parent.die) : null;
				if (sub)
					sub.params.push([name(a), a[AT.type] ? ref(a[AT.type]) : null]);
			} else if (L && tag === DW_TAG_UNSPEC_PARAMS && parent.tag === DW_TAG_SUBPROGRAM) {
				this.skipDie(c, ab);
				const sub = L.subs.get(parent.die);
				if (sub)
					sub.varargs = true;
			} else if (tag === DW_TAG_VARIABLE) {
				const a = this.readDie(c, ab);
				if (L && parent.die >= 0)
					this.localDie(c, a, false, dieOff, parent, L, ref);
				const file = a[AT.decl_file] ? files[a[AT.decl_file][1]] || null : null;
				const line = a[AT.decl_line] ? a[AT.decl_line][1] : 0;
				if (a[AT.declaration])
					declared.set(dieOff, { name: name(a), type: tref(a), external: !!a[AT.external], file, line });
				const loc = a[AT.location];
				const va = loc && loc.length === 3 ? this.fixedAddr(c, loc[1], loc[2]) : null;
				if (va !== null)
					out.vars.push({ name: name(a), type: tref(a), va, spec: a[AT.specification] ? ref(a[AT.specification]) : -1,
						origin: a[AT.abstract_origin] ? ref(a[AT.abstract_origin]) : -1, external: !!a[AT.external], fn: parent.fn, file, line });
			} else
				this.skipDie(c, ab);
			if (ab.children)
				stack.push({ tag, raw, fn: tag === DW_TAG_SUBPROGRAM ? dieOff : parent.fn, scope,
					die: tag === DW_TAG_SUBPROGRAM || tag === DW_TAG_INLINED || tag === DW_TAG_LEXICAL_BLOCK ? dieOff : -1 });
		}
		if (L) {
			L.origins = new Map();
			for (const list of L.kids.values())
				for (const [d, , o] of list)
					L.origins.set(d, o);
			out.scopes = this.finishScopes(L);
			out.subs = L.subs;
			out.copies = L.copies;
		}
		// A definition through DW_AT_specification / abstract_origin takes the
		// declaration's name, type and (when it has none) place, as gdb
		// reads the attributes; static locals name their function.
		for (const v of out.vars) {
			const d = declared.get(v.spec) || declared.get(v.origin);
			if (d) {
				v.name = v.name || d.name;
				if (v.type < 0)
					v.type = d.type;
				v.external = v.external || d.external;
				v.file = v.file || d.file;
				v.line = v.line || d.line;
			}
			if ((!v.file || !v.line) && (v.spec >= 0 || v.origin >= 0)) {
				const [f, l] = this.declAt(v.origin >= 0 ? v.origin : v.spec);
				v.file = v.file || f;
				v.line = v.line || l;
			}
			if (!v.name && v.origin >= 0)
				v.name = this.nameAt(v.origin);
			if (v.type < 0 && v.origin >= 0)
				v.type = this.typeAt(v.origin);
			v.scope = v.fn >= 0 ? this.nameAt(v.fn) : null;
		}
		return out;
	}

	// ---- scopes, parameters and local variables (decodeData with locals) ----
	// A scope is a concrete function (kind 0: a DW_TAG_subprogram with code),
	// a lexical block with code (1) or an inlined call (2), with its address
	// ranges, its parent scope and its variables. The rules are gdb's
	// (dwarf2/read.c read_lexical_block_scope, inherit_abstract_dies): a
	// lexical block without code holds its variables for the scope around
	// it; a function, inlined call or block with an abstract origin also
	// gets the origin's children that none of its own children refers to
	// (their parameters and variables are optimized out here), where a
	// clang block without DW_AT_abstract_origin stands for the origin's
	// child at the same place when both have children of the same tags.

	// The scope a DW_TAG_subprogram / inlined_subroutine / lexical_block
	// opens for its children: a new one, the parent's, or -1 (no code).
	scopeDie(c, ab, tag, dieOff, parent, L, ref) {
		const a = this.readDie(c, ab);
		const origin = a[AT.abstract_origin] ? ref(a[AT.abstract_origin]) : -1;
		if (L.lastKid)
			L.lastKid[2] = origin;
		const sub = tag === DW_TAG_SUBPROGRAM ? this.subRecord(c, a, ref, L.files) : null;
		if (sub)
			L.subs.set(dieOff, sub);
		if (tag !== DW_TAG_SUBPROGRAM && parent.scope < 0)
			return -1;
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
		if (sub)
			sub.rs = rs;
		else if (tag === DW_TAG_INLINED && rs.length && origin >= 0)
			L.copies.push([origin, rs.reduce((m, r) => Math.min(m, r[0]), Infinity)]);
		if (!rs.length)
			return tag === DW_TAG_LEXICAL_BLOCK && !a[AT.low_pc] && !a[AT.ranges] ? parent.scope : -1;
		let name = null;
		if (tag !== DW_TAG_LEXICAL_BLOCK) {
			const n = a[AT.name] || a[AT.linkage_name] || a[AT.mips_linkage_name];
			if (n)
				name = this.str(c, ...n);
			else if (origin >= 0)
				name = this.nameAt(origin);
			else if (a[AT.specification])
				name = this.nameAt(ref(a[AT.specification]));
		}
		L.scopes.push({ kind: tag === DW_TAG_SUBPROGRAM ? 0 : tag === DW_TAG_LEXICAL_BLOCK ? 1 : 2,
			parent: tag === DW_TAG_SUBPROGRAM ? -1 : parent.scope, ranges: rs, name, origin, die: dieOff, vars: [],
			fb: tag === DW_TAG_SUBPROGRAM ? this.locAttr(c, a[AT.frame_base]) : null });
		return L.scopes.length - 1;
	}

	// A DW_TAG_formal_parameter or DW_TAG_variable inside code: remembered
	// (abstract origins and inheritance need it) and added to its scope.
	localDie(c, a, param, dieOff, parent, L, ref) {
		const origin = a[AT.abstract_origin] ? ref(a[AT.abstract_origin]) : -1;
		if (L.lastKid)
			L.lastKid[2] = origin;
		if (a[AT.declaration])
			return;
		const v = { die: dieOff, name: a[AT.name] ? this.str(c, ...a[AT.name]) : null, type: a[AT.type] ? ref(a[AT.type]) : -1, origin, param,
			loc: this.locAttr(c, a[AT.location]), cv: a[AT.const_value] ? (a[AT.const_value].length === 3 ?
				c.b.slice(a[AT.const_value][1], a[AT.const_value][1] + a[AT.const_value][2]) : a[AT.const_value][1]) : null };
		L.info.set(dieOff, v);
		if (parent.scope >= 0)
			L.scopes[parent.scope].vars.push(v);
	}

	// A location attribute: { expr: bytes }, { list: [[lo, hi, bytes], ...] }
	// (virtual addresses), { why } or null (none: optimized out).
	locAttr(c, x) {
		if (!x)
			return null;
		if (x.length === 3)
			return { expr: this.ownExpr(c, c.b.slice(x[1], x[1] + x[2])) };
		const l = this.locList(c, x[0], x[1]);
		return l ? { list: l.map(([lo, hi, e]) => [lo, hi, this.ownExpr(c, e)]) } : { why: "its location list is in a section this file does not have" };
	}

	// An expression that needs nothing but itself: DW_OP_addrx and
	// DW_OP_GNU_addr_index (an index into .debug_addr, clang) become
	// DW_OP_addr with the address.
	ownExpr(c, e) {
		const opEnd = typeof dwOpEnd === "function" ? dwOpEnd : require("./unwind.js").dwOpEnd;
		if (!e.includes(0xa1) && !e.includes(0xfb))
			return e;
		const out = [];
		for (let p = 0; p < e.length;) {
			const q = opEnd(e, p);
			if (q < 0)
				return e;
			if (e[p] === 0xa1 || e[p] === 0xfb) {
				const cur = { b: e, p: p + 1 };
				const va = this.addr(c, 0x1b, this.uleb(cur));
				if (va === null)
					return e;
				out.push(0x03, va & 255, va >>> 8 & 255, va >>> 16 & 255, va >>> 24 & 255);
			} else
				out.push(...e.subarray(p, q));
			p = q;
		}
		return Uint8Array.from(out);
	}

	// Names and types through abstract origins, then the inherited ones.
	finishScopes(L) {
		const resolve = (v, depth = 0) => {
			if ((v.name !== null && v.type >= 0 && (v.cv !== null || v.loc)) || v.origin < 0 || depth > 8)
				return v;
			const o = L.info.get(v.origin);
			if (o) {
				resolve(o, depth + 1);
				if (v.name === null) v.name = o.name;
				if (v.type < 0) v.type = o.type;
				// gdb reads an attribute a DIE lacks from its abstract origin
				// (a static local has its DW_OP_addr there).
				if (v.cv === null && !v.loc) {
					v.cv = o.cv;
					v.loc = o.loc;
				}
			} else {
				if (v.name === null) v.name = this.nameAt(v.origin);
				if (v.type < 0) v.type = this.typeAt(v.origin);
			}
			return v;
		};
		const kids = (die) => L.kids.get(die) || this.foreignKids(die, L);
		// An abstract DIE gdb processes: a parameter or variable becomes
		// one of s's (optimized out), a block (no code) passes its own on.
		const take = (s, k, tag, depth) => {
			if (tag === DW_TAG_LEXICAL_BLOCK) {
				if (depth < 16)
					for (const [kk, kt] of kids(k))
						take(s, kk, kt, depth + 1);
			} else if (tag === DW_TAG_PARAM || tag === DW_TAG_VARIABLE) {
				const o = L.info.get(k);
				if (o)
					s.vars.push(resolve({ die: k, name: o.name, type: o.type, origin: o.origin, param: o.param, loc: o.loc, cv: o.cv,
						inherited: !o.loc && o.cv === null }));
			}
		};
		for (const s of L.scopes) {
			s.vars.forEach((v) => resolve(v));
			if (s.origin < 0)
				continue;
			const mine = L.kids.get(s.die) || [], theirs = kids(s.origin);
			const iso = mine.length === theirs.length && mine.every((x, k) => x[1] === theirs[k][1]);
			const parentOf = new Set(theirs.map((x) => x[0]));
			const used = new Set();
			mine.forEach(([, tag, origin], k) => {
				if (tag === 0x48 || tag === 0x4109) // call sites
					return;
				let o = origin;
				for (let n = 0; o >= 0 && n < 8; n++) {
					const up = L.origins.get(o);
					if (up === undefined || up < 0 || up === o)
						break;
					o = up;
				}
				if (o < 0 && iso && tag === DW_TAG_LEXICAL_BLOCK)
					o = theirs[k][0];
				if (o >= 0 && parentOf.has(o))
					used.add(o);
			});
			for (const [k, tag] of theirs)
				if (!used.has(k))
					take(s, k, tag, 0);
		}
		return L.scopes;
	}

	// The children of a DIE in another unit (an abstract origin clang
	// refers to with DW_FORM_ref_addr) as [[offset, tag, origin], ...],
	// like L.kids for this unit; their parameters and variables go into
	// L.info, the blocks' children are read when asked for.
	foreignKids(off, L) {
		const out = [];
		L.kids.set(off, out);
		const i = this.unitOf(off);
		if (i < 0)
			return out;
		const c = this.root(i).c;
		c.b = this.unitBytes(i);
		c.p = off - c.base;
		const abbrevs = this.abbrevs(c.u.abbrevOff);
		const ab = abbrevs[this.uleb(c)];
		if (!ab || !ab.children)
			return out;
		this.skipDie(c, ab);
		const ref = (x) => x[0] === 0x10 || x[0] === 0x1f20 ? x[1] : c.base + x[1];
		for (let depth = 0; c.p < c.b.length;) {
			const die = c.base + c.p;
			const code = this.uleb(c);
			if (!code) {
				if (--depth < 0)
					break;
				continue;
			}
			const k = abbrevs[code];
			if (!k)
				break;
			if (depth === 0) {
				const a = this.readDie(c, k);
				const origin = a[AT.abstract_origin] ? ref(a[AT.abstract_origin]) : -1;
				out.push([die, k.tag, origin]);
				if ((k.tag === DW_TAG_PARAM || k.tag === DW_TAG_VARIABLE) && !a[AT.declaration])
					L.info.set(die, { die, name: a[AT.name] ? this.str(c, ...a[AT.name]) : null, type: a[AT.type] ? ref(a[AT.type]) : -1,
						origin, param: k.tag === DW_TAG_PARAM, loc: this.locAttr(c, a[AT.location]),
						cv: a[AT.const_value] ? (a[AT.const_value].length === 3 ?
							c.b.slice(a[AT.const_value][1], a[AT.const_value][1] + a[AT.const_value][2]) : a[AT.const_value][1]) : null });
			} else
				this.skipDie(c, k);
			if (k.children)
				depth++;
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

	// [file, line] of DW_AT_decl_file / decl_line of the DIE at absolute
	// offset off in any unit, each through its abstract origin or
	// specification when the DIE has none (as gdb reads them; GCC leaves
	// out of a definition what its declaration already says); null / 0 if
	// unknown.
	declAt(off) {
		let file = null, line = 0;
		for (let depth = 0; depth < 8 && (!file || !line); depth++) {
			const i = this.unitOf(off);
			if (i < 0)
				break;
			const c = this.root(i).c;
			c.b = this.unitBytes(i);
			c.p = off - c.base;
			const ab = this.abbrevs(c.u.abbrevOff)[this.uleb(c)];
			if (!ab)
				break;
			const a = this.readDie(c, ab);
			if (!file && a[AT.decl_file])
				file = this.unitFiles(i)[a[AT.decl_file][1]] || null;
			if (!line && a[AT.decl_line])
				line = a[AT.decl_line][1];
			const next = a[AT.abstract_origin] || a[AT.specification];
			if (!next)
				break;
			off = next[0] === 0x10 ? next[1] : c.base + next[1];
		}
		return [file, line];
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
		this.files = []; // where variables are defined (vars[k][7])
		this.fileIx = new Map();
		this.seen = new Map(); // name@address -> variable
		this.other = new Map(); // unit index -> its decodeData types
		this.funcs = new DwarfFuncs(dw);
	}

	// The type id of a DIE offset of unit d (DwarfReader.decodeData): an
	// existing entry of this table, or a new one appended.
	idFor(d) {
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
		return id;
	}

	// The scopes of unit d (decodeData with locals) with this table's type
	// ids (dwarfScopeGroups), for an import's Worker after the table is
	// done: types that are new to the table (none when the table was made
	// with locals) are appended, added says how many.
	scopesOf(d, toPhys) {
		const n = this.types.length;
		const groups = dwarfScopeGroups(d.scopes || [], this.idFor(d), toPhys);
		return { groups, added: this.types.slice(n) };
	}

	// d: DwarfReader.decodeData of one unit. toPhys: virtual -> physical or
	// null (outside the loaded image); secOf: virtual -> section index.
	// With d.scopes (decodeData with locals): returns its functions with
	// their scopes and variables, types as ids of this table
	// (dwarfScopeGroups), else [].
	addUnit(d, toPhys, secOf) {
		const id = this.idFor(d);
		for (const x of d.vars) {
			const pa = toPhys(x.va);
			if (pa === null || !x.name)
				continue;
			const k = x.name + "@" + pa;
			const had = this.seen.get(k);
			if (had && x.file) {
				// Another definition at the same address (a __weak one and the
				// one the linker kept: DWARF does not say which is which).
				const f = this.fileId(x.file), line = x.line || 0;
				if (had[7] < 0)
					[had[7], had[8]] = [f, line];
				else if ((had[7] !== f || had[8] !== line) && !(had[9] || []).some((d) => d[0] === f && d[1] === line))
					(had[9] = had[9] || []).push([f, line]);
			}
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
			const v = [x.name, pa, id(x.type), 0, sec[0], (x.external ? 1 : 0) | (sec[1] ? 2 : 0) | (x.fn >= 0 ? 4 : 0), x.scope || "",
				x.file ? this.fileId(x.file) : -1, x.line || 0];
			this.seen.set(k, v);
			this.vars.push(v);
		}
		if (d.subs)
			this.funcs.add(d, id, toPhys);
		return d.scopes ? dwarfScopeGroups(d.scopes, id, toPhys) : [];
	}

	// Index of path in files (vars[k][7]), added when new.
	fileId(path) {
		let f = this.fileIx.get(path);
		if (f === undefined) {
			f = this.files.length;
			this.files.push(path);
			this.fileIx.set(path, f);
		}
		return f;
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

	// { format, types, vars, sections, files }: declarations point to the
	// definition of the same name, vars get their sizes and are sorted.
	// A variable: [name, physical address, type id, size, section index,
	// flags (1 external, 2 per-CPU, 4 static local), function of a static
	// local, index in files of DW_AT_decl_file, DW_AT_decl_line, and only
	// when other units define a variable of that name at that address
	// too: [[file index, line], ...] of those]; the trailing fields are
	// left out when unknown ("", -1).
	finish(sections) {
		const defs = new Map();
		this.types.forEach((t, k) => {
			if ((t[0] === DWT.struct || t[0] === DWT.union || t[0] === DWT.enum) && t[1] && t[3] && !defs.has(t[0] + t[1]))
				defs.set(t[0] + t[1], k);
		});
		for (const t of this.types)
			if ((t[0] === DWT.struct || t[0] === DWT.union || t[0] === DWT.enum) && !t[3] && defs.has(t[0] + t[1]))
				t[t[0] === DWT.enum ? 6 : 5] = defs.get(t[0] + t[1]);
		const out = { format: 2, types: this.types, vars: this.vars, sections, files: this.files, funcs: this.funcs.finish() };
		for (const v of this.vars) {
			v[3] = dwTypeSize(out, v[2]);
			if (v[7] < 0)
				v.length = v[6] ? 7 : 6;
		}
		this.vars.sort((x, y) => x[1] - y[1] || y[3] - x[3]);
		return out;
	}
}

// ---- function facts -------------------------------------------------------------
//
// One entry per source function (name and where it is declared): what
// DW_TAG_subprogram says about it and where its code is, out of line and
// inlined. Built unit by unit next to the types (DwarfTypes.addUnit), so
// its type ids are the table's. gen-dwarf.js ships it in name-hashed
// shards (funcs-<k>.json.gz, dwFuncShard); for an import it comes with
// the types from the Worker.
//   { format, files: [path], fns: [[name, file index (-1: unknown), line,
//     flags, inline, return type id (-1: void), [[param name, type id],
//     ...], [[lo, length, lo2, length2, ...], ...] its out-of-line code,
//     one list of ranges per DW_TAG_subprogram with code (a copy per unit
//     for a static inline function, GCC's .isra/.constprop clones; a
//     second range is GCC's .cold part), [entry, ...] its inlined copies
//     (DW_TAG_inlined_subroutine with code: the lowest address of each,
//     sorted)], ...] }
//   addresses physical; flags 1 external (DW_AT_external), 2 varargs,
//   4 noreturn, 8 prototyped; inline: DW_AT_inline (0 not inlined, 1
//   inlined, 2 declared inline but not inlined, 3 declared inline and
//   inlined) or -1 when the DIE has none.
class DwarfFuncs {
	constructor(dw) {
		this.dw = dw;
		this.byKey = new Map();
		this.list = [];
	}

	// d: decodeData with locals; id: the type table's id function for d.
	add(d, id, toPhys) {
		const local = (off) => d.subs.get(off) || (this.dw ? this.dw.subprogramAt(off) : null);
		// A function's facts through DW_AT_abstract_origin / specification:
		// each field from the first DIE of the chain that has it; the
		// parameters (names, types) of the DIE that declares them.
		const resolve = (rec) => {
			const chain = [rec];
			for (let r = rec, n = 0; n < 8; n++) {
				const next = r.origin >= 0 ? r.origin : r.spec;
				const q = next >= 0 ? local(next) : null;
				if (!q)
					break;
				chain.push(q);
				r = q;
			}
			const first = (f) => { for (const r of chain) { const v = f(r); if (v !== null && v !== undefined) return v; } return null; };
			const name = first((r) => r.name);
			if (!name)
				return null;
			const at = first((r) => r.file ? r : null) || rec;
			const pr = chain.slice().reverse().find((r) => r.params.length) || chain[chain.length - 1];
			const params = pr.params.map((p, k) => [p[0] || first((r) => r.params[k] && r.params[k][0]) || "", p[1] !== null ? p[1] :
				first((r) => r.params[k] && r.params[k][1])]);
			const ret = first((r) => r.ret);
			return { name, file: at.file, line: at.line, ext: chain.some((r) => r.ext), varargs: chain.some((r) => r.varargs),
				noret: chain.some((r) => r.noret), proto: chain.some((r) => r.proto), inl: first((r) => r.inl), ret, params };
		};
		const entry = (f) => {
			const key = f.name + "\0" + (f.file || "") + "\0" + f.line;
			let e = this.byKey.get(key);
			if (!e) {
				e = [f.name, f.file, f.line, (f.ext ? 1 : 0) | (f.varargs ? 2 : 0) | (f.noret ? 4 : 0) | (f.proto ? 8 : 0),
					f.inl === null ? -1 : f.inl, f.ret === null ? -1 : id(f.ret), f.params.map(([n, t]) => [n, t === null ? -1 : id(t)]), [], []];
				this.byKey.set(key, e);
				this.list.push(e);
			} else {
				// Another unit's DIE of the same function adds what this one lacked.
				e[3] |= (f.ext ? 1 : 0) | (f.varargs ? 2 : 0) | (f.noret ? 4 : 0) | (f.proto ? 8 : 0);
				if (e[4] < 0 && f.inl !== null)
					e[4] = f.inl;
			}
			return e;
		};
		for (const rec of d.subs.values()) {
			const rs = rec.rs.map(([lo, hi]) => [toPhys(lo), hi - lo]).filter(([pa]) => pa !== null);
			if (!rs.length || rec.decl)
				continue;
			const f = resolve(rec);
			if (!f)
				continue;
			const e = entry(f);
			if (!e[7].some((r) => r[0] === rs[0][0]))
				e[7].push(rs.flat());
		}
		for (const [origin, lo] of d.copies) {
			const pa = toPhys(lo);
			const rec = pa === null ? null : local(origin);
			const f = rec ? resolve(rec) : null;
			if (f)
				entry(f)[8].push(pa);
		}
	}

	finish() {
		const files = [], fileIx = new Map();
		for (const e of this.list) {
			const p = e[1];
			if (p === null) {
				e[1] = -1;
				continue;
			}
			if (!fileIx.has(p)) {
				fileIx.set(p, files.length);
				files.push(p);
			}
			e[1] = fileIx.get(p);
			e[8].sort((x, y) => x - y);
		}
		return { format: 1, files, fns: this.list };
	}
}

// The shard of a function's facts by name (FNV-1a), for gen-dwarf.js and
// the page alike.
function dwFuncShard(name, n) {
	let h = 0x811c9dc5;
	for (let k = 0; k < name.length; k++)
		h = Math.imul(h ^ name.charCodeAt(k), 0x01000193) >>> 0;
	return h % n;
}

// The name DWARF gives the function of an ELF symbol: GCC's clones and
// split parts (start_kernel.cold, foo.isra.0, foo.constprop.0, foo.part.0)
// are the function before the first dot.
const dwFuncBase = (sym) => sym.replace(/\..*$/, "");

// ---- parameters and local variables -------------------------------------------
//
// The scopes of a unit's code as the page reads them (gen-dwarf.js ships
// them in locals-<hex>.json shards per 64 KiB, the Worker sends a unit's
// for an import): one group per concrete function, its scopes in DIE
// order, the function first:
//   [kind, parent, ranges, name, frame base, vars]
//     kind    0 function, 1 lexical block, 2 inlined call
//     parent  index of the enclosing scope in the group (-1: the function)
//     ranges  [lo, length, lo2, length2, ...] physical
//     name    the function's (kinds 0 and 2), null for a block
//     frame base  location (below) of DW_AT_frame_base, functions only
//     vars    [[name, type id (DwarfTypes), flags, location], ...] in DIE
//             order; flags 1: a parameter, 2: only in the abstract origin
//             (the compiler said nothing about it here: optimized out)
// A location is null (none: optimized out), "hex" (one DWARF expression),
// [[lo, length, "hex"], ...] (a location list, physical; length 0: the
// default location), { c: number } or { cb: "hex" } (DW_AT_const_value),
// or { why } (unreadable).
function dwarfScopeGroups(scopes, id, toPhys) {
	const hx = (b) => Array.from(b, (x) => (x < 16 ? "0" : "") + x.toString(16)).join("");
	const loc = (l, cv) => {
		if (cv !== null && cv !== undefined)
			return typeof cv === "number" ? { c: cv } : { cb: hx(cv) };
		if (!l)
			return null;
		if (l.why)
			return { why: l.why };
		if (l.expr)
			return hx(l.expr);
		const out = [];
		for (const [lo, hi, e] of l.list) {
			if (lo === 0 && hi === 2 ** 32) {
				out.push([0, 0, hx(e)]);
				continue;
			}
			const pa = toPhys(lo);
			if (pa !== null)
				out.push([pa, hi - lo, hx(e)]);
		}
		return out;
	};
	const groups = [];
	let g = null, map = null;
	scopes.forEach((s, k) => {
		const rs = [];
		for (const [lo, hi] of s.ranges) {
			const pa = toPhys(lo);
			if (pa !== null)
				rs.push(pa, hi - lo);
		}
		if (s.kind === 0) {
			g = rs.length ? [] : null;
			map = new Map();
			if (g)
				groups.push(g);
		}
		if (!g || !rs.length || (s.kind !== 0 && !map.has(s.parent)))
			return;
		map.set(k, g.length);
		g.push([s.kind, s.kind === 0 ? -1 : map.get(s.parent), rs, s.name, s.kind === 0 ? loc(s.fb, null) : null,
			s.vars.filter((v) => v.name).map((v) => [v.name, id(v.type), (v.param ? 1 : 0) | (v.inherited ? 2 : 0), loc(v.loc, v.cv)])]);
	});
	return groups;
}

// The scopes of group list G containing physical address pa, innermost
// first: [[group, scope index], ...]; [] when no function there.
function dwarfScopesAt(G, pa) {
	const inside = (r) => {
		for (let k = 0; k < r.length; k += 2)
			if (pa >= r[k] && pa < r[k] + r[k + 1])
				return true;
		return false;
	};
	for (const g of G) {
		if (!inside(g[0][2]))
			continue;
		const out = [];
		for (let k = g.length - 1; k >= 0; k--)
			if (inside(g[k][2]) && (!out.length || dwScopeIn(g, out[out.length - 1][1], k)))
				out.push([g, k]);
		return out;
	}
	return [];
}

// A variable's location (dwarfScopeGroups) in force at physical address
// pa: { expr: Uint8Array }, { c } / { cb } (a constant), or { why } (none
// here: optimized out, or unreadable).
function dwLocAt(loc, pa) {
	const bytes = (h) => Uint8Array.from(h.match(/../g) || [], (x) => parseInt(x, 16));
	if (loc === null)
		return { why: "optimized out (the compiler kept no location for it)" };
	if (typeof loc === "string")
		return { expr: bytes(loc) };
	if (!Array.isArray(loc))
		return loc.cb !== undefined ? { cb: bytes(loc.cb) } : loc;
	let dflt = null;
	for (const [lo, len, e] of loc) {
		if (!len)
			dflt = e;
		else if (pa >= lo && pa < lo + len)
			return { expr: bytes(e) };
	}
	return dflt !== null ? { expr: bytes(dflt) } : { why: "optimized out here (its location list has no entry for this address)" };
}

// The frames at pa with their variables, innermost first, from the
// scopes dwarfScopesAt found: [{ func, inlined, scopes: [group, index],
// params: [var], locals: [var] }], one per inlined call and one for the
// function; a frame's locals are those of its lexical blocks around pa,
// innermost block first (gdb's order), its parameters in DIE order.
function dwarfLocalFrames(chain) {
	const out = [];
	let cur = null;
	for (const [g, k] of chain) {
		const s = g[k];
		if (!cur) {
			cur = { func: null, inlined: false, fb: g[0][4], params: [], locals: [] };
			out.push(cur);
		}
		for (const v of s[5])
			(v[2] & 1 ? cur.params : cur.locals).push(v);
		if (s[0] !== 1) {
			cur.func = s[3];
			cur.inlined = s[0] === 2;
			cur = null;
		}
	}
	return out.filter((f) => f.func !== null || f.params.length || f.locals.length);
}

// Whether scope a of group g lies inside scope b.
function dwScopeIn(g, a, b) {
	for (let s = a, n = 0; s >= 0 && n < 64; s = g[s][1], n++)
		if (s === b)
			return true;
	return false;
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

// A function (DwarfFuncs entry e) in C: "int printk(const char *fmt,
// ...)", or without names its type as gdb's ptype prints it, "int (const
// char *, ...)". A prototyped function without parameters takes (void).
function dwFuncProto(T, e, named = true) {
	const ps = e[6].map(([n, t]) => dwTypeName(T, t, named ? n : ""));
	if (e[3] & 2)
		ps.push("...");
	return dwTypeName(T, e[5], (named ? e[0] : "") + "(" + (ps.length ? ps.join(", ") : e[3] & 8 ? "void" : "") + ")");
}

// Index of the last variable starting at or below pa, -1 if none.
function dwVarIndex(T, pa) {
	const v = T.vars;
	let a = 0, b = v.length;
	while (a < b) { const m = (a + b) >> 1; if (v[m][1] <= pa) a = m + 1; else b = m; }
	return a - 1;
}

// Where variable v (types.json) is defined: { file, line } (DW_AT_decl_file
// and decl_line, through the declaration or abstract origin when the
// definition has none) or null.
function dwVarDecl(T, v) {
	return T.files && v.length > 7 && v[7] >= 0 && T.files[v[7]] ? { file: T.files[v[7]], line: v[8] } : null;
}

// Every place a definition of v at its address says it is defined:
// dwVarDecl's first, then those of other units (a __weak definition and
// the one the linker kept); [] when unknown.
function dwVarDecls(T, v) {
	const d = dwVarDecl(T, v);
	return d ? [d, ...(v[9] || []).map(([f, line]) => ({ file: T.files[f], line }))] : [];
}

// "file:line" of dwVarDecls (", also at file:line ..."), or "" when unknown.
function dwVarDeclText(T, v) {
	const ds = dwVarDecls(T, v);
	return ds.map((d, k) => (k === 1 ? ", also at " : k ? ", " : "") + d.file + ":" + d.line).join("") +
		(ds.length > 1 ? " (definitions at one address: e.g. a __weak one and the one the linker kept)" : "");
}

// The variable a watch expression's name picks out of all those with that
// name (list): qualifier q ("" none; a function: Function::name, its static
// locals; a source file, any q with a "." or "/": file.c::name, the file
// the variable is defined in, by path or unique suffix as dwarfResolvePath
// takes them); among several, the external one, else the first.
// { v, list (the variables q leaves), all: list } or { why, all }.
function dwVarsPick(T, all, q, name) {
	let list = all;
	if (q && /[./]/.test(q)) {
		if (!T.files)
			return { why: "this kernel's variables were read without their DW_AT_decl_file: they cannot be picked by file", all };
		const paths = [...new Set(all.flatMap((v) => dwVarDecls(T, v)).map((d) => d.file))];
		const r = dwarfResolvePath(paths, q, name + " is not defined in " + q.replace(/^\.?\//, "") + (paths.length ? "; it is defined in " +
			paths.slice(0, 5).join(", ") + (paths.length > 5 ? ", … (" + paths.length + " files)" : "") : "; no definition of it says where it is"));
		if (r.why)
			return { why: r.why, all };
		list = all.filter((v) => dwVarDecls(T, v).some((d) => d.file === r.path));
	} else if (q)
		list = all.filter((v) => v[6] === q);
	if (!list.length)
		return { why: "no variable " + (q ? q + "::" : "") + name + " in this kernel's DWARF", all };
	return { v: list.find((x) => x[5] & 1) || list[0], list, all };
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
	const r = dwPathIn(T, best[2], pa - best[1], best[0]);
	return { var: best, ...r, leafAddr: pa - r.offset };
}

// The member or element path to byte off of an object of type id named
// name: { path, note, type (of the leaf), offset (of the byte in the leaf),
// unions }, as dwWhere describes them.
function dwPathIn(T, id, off, name) {
	let path = name, note = "";
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
	return { path, note, type: id, offset: off, unions };
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
		// Location lists (18.6 MB for a distro kernel): read when a
		// variable's location is first asked for.
		loclists: () => whole(".debug_loclists"), loc: () => whole(".debug_loc"),
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
	// addresses, then the rows in program order: the last one wins),
	// is_stmt.
	let ra = new Float64Array(n), rf = new Int32Array(n), rl = new Uint32Array(n), rk = new Float64Array(n), rs = new Uint8Array(n);
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
			const end = rows[k + 3] === 1;
			if (start)
				ok = toPhys(rows[k]) !== null;
			start = end;
			if (!end && !inCover(rows[k]))
				continue;
			const pa = !ok ? null : end ? (toPhys(rows[k] - 1) ?? -1) + 1 : toPhys(rows[k]);
			if (!pa)
				continue;
			ra[m] = pa;
			rf[m] = end ? -2 : fid[rows[k + 1]] ?? -1;
			rl[m] = end ? 0 : rows[k + 2];
			rk[m] = end ? 0 : 1 + order++;
			rs[m] = rows[k + 3] === 2 ? 1 : 0;
			m++;
		}
	}
	const idx = new Uint32Array(m);
	for (let k = 0; k < m; k++)
		idx[k] = k;
	idx.sort((x, y) => ra[x] - ra[y] || rk[x] - rk[y]);
	// One row per address (the last one there wins), then one per run of
	// the same file:line. Breakpoint addresses of a line: each is_stmt row
	// that does not continue an is_stmt row of the same line in the same
	// sequence (one more statement on that line is not a new place to
	// stop); lk marks a run whose first address is one, sx holds the others.
	const ua = [], uf = [], ul = [], us = [], ue = [];
	for (let q = 0; q < m; q++) {
		const k = idx[q];
		const a = ra[k], f = rf[k] === -2 ? -1 : rf[k], l = rf[k] === -2 ? 0 : rl[k];
		const last = ua.length - 1;
		if (last >= 0 && ua[last] === a) {
			uf[last] = f; ul[last] = l; us[last] = rs[k];
			continue;
		}
		ua.push(a); uf.push(f); ul.push(l); us.push(rs[k]); ue.push(rf[k] === -2 ? 1 : 0); // ue: a sequence ends here
	}
	ra = rf = rl = rk = rs = null;
	const la = [], lf = [], ll = [], lk = [], sx = [];
	for (let q = 0; q < ua.length; q++) {
		const f = uf[q], l = ul[q];
		const stmt = us[q] && f >= 0 && l > 0 && !(q > 0 && !ue[q] && us[q - 1] && uf[q - 1] === f && ul[q - 1] === l);
		const last = la.length - 1;
		if (last >= 0 && lf[last] === f && ll[last] === l) {
			if (stmt)
				sx.push(ua[q]);
			continue;
		}
		la.push(ua[q]); lf.push(f); ll.push(l); lk.push(stmt ? 1 : 0);
	}
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
	return dwarfPack(strings, la, lf, ll, rg, lk, sx);
}

// lk: 1 where a line row's address is a breakpoint address (is_stmt), sx:
// the breakpoint addresses inside rows (sorted). Tables decoded from a
// version 1 shard or remembered from before have neither: every row start
// counts then.
function dwarfPack(strings, la, lf, ll, rg, lk, sx) {
	const t = { strings, la: Float64Array.from(la), lf: Int32Array.from(lf), ll: Uint32Array.from(ll),
		lk: Uint8Array.from(lk), sx: Float64Array.from(sx),
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

// fn(a, b, frames) for each line row of table t in [lo, hi) that has a
// file:line, clipped to [lo, hi); frames as dwarfFrames at a. What has
// code and what ran (the page's coverage, gen-dwarf.js's totals) is
// counted per row like this: every frame with a line, so the call line of
// inlined code counts as well as the inlined line.
function dwarfEachLine(t, lo, hi, fn) {
	for (let k = Math.max(0, dwarfRow(t, lo)); k < t.la.length && t.la[k] < hi; k++) {
		if (t.lf[k] < 0)
			continue;
		const a = Math.max(lo, t.la[k]), b = Math.min(hi, k + 1 < t.la.length ? t.la[k + 1] : hi);
		const frames = b > a ? dwarfFrames(t, a) : null;
		if (frames)
			fn(a, b, frames);
	}
}

// The code of file `path` in table t within [lo, hi), per line: Map line
// -> { rows: [[a, b, breakpoint addresses (is_stmt) in [a, b)], ...], the
// line rows naming it, calls: [[a, b, name, depth], ...], the ranges of
// functions inlined by a call on it }. A table without is_stmt data (an
// older shard, a unit kept from before) gives each row's start as its
// breakpoint address and says so (noStmt).
function dwarfFileAddrs(t, path, lo = 0, hi = Infinity, out = new Map()) {
	const fi = new Set();
	t.strings.forEach((s, k) => { if (s === path) fi.add(k); });
	if (!fi.size)
		return out;
	const noStmt = !t.lk || !!t.noStmt;
	const at = (l) => {
		if (!out.has(l))
			out.set(l, { rows: [], calls: [], noStmt: false });
		return out.get(l);
	};
	for (let k = Math.max(0, dwarfRow(t, lo)); k < t.la.length && t.la[k] < hi; k++) {
		if (!fi.has(t.lf[k]) || !t.ll[k])
			continue;
		const a = Math.max(lo, t.la[k]), b = Math.min(hi, k + 1 < t.la.length ? t.la[k + 1] : hi);
		if (!(b > a))
			continue;
		const bp = [];
		if (noStmt) {
			if (t.la[k] >= lo)
				bp.push(a);
		} else {
			if (t.lk[k] && t.la[k] >= lo)
				bp.push(a);
			let x = 0, y = t.sx.length;
			while (x < y) { const q = (x + y) >> 1; if (t.sx[q] < a) x = q + 1; else y = q; }
			for (; x < t.sx.length && t.sx[x] < b; x++)
				if (t.sx[x] > a || !bp.length)
					bp.push(t.sx[x]);
		}
		const e = at(t.ll[k]);
		e.rows.push([a, b, bp]);
		e.noStmt = e.noStmt || noStmt;
	}
	for (let q = 0; q < t.ra.length; q++)
		if (fi.has(t.rf[q]) && t.rl[q] && t.ra[q] < hi && t.rh[q] > lo)
			at(t.rl[q]).calls.push([t.ra[q], t.rh[q], t.strings[t.rn[q]] || null, t.rd[q]]);
	return out;
}

// The code of file `path` over several tables (address shards, or compile
// units with lo 0 and hi Infinity), any iterable of [lo, hi, t], per line:
// Map line -> { rows, calls, bps, noStmt }. Rows are merged where one goes
// on in the next shard, each inlined call is listed once, and bps are the
// addresses a "Run to path:line" stops at: every breakpoint address of
// its rows plus the start of every inlined call's range (code inlined by
// a call on the line runs that line, and its first row usually names the
// inlined function's line, not this one).
function dwarfFileCode(tables, path) {
	const acc = new Map();
	for (const [lo, hi, t] of tables)
		dwarfFileAddrs(t, path, lo, hi, acc);
	const out = new Map();
	for (const [line, e] of acc) {
		e.rows.sort((x, y) => x[0] - y[0]);
		const rows = [];
		for (const r of e.rows) {
			const last = rows[rows.length - 1];
			if (last && last[1] === r[0])
				last[1] = r[1], last[2] = last[2].concat(r[2]);
			else
				rows.push([r[0], r[1], r[2].slice()]);
		}
		const seen = new Map();
		for (const c of e.calls)
			seen.set(c.join("\0"), c);
		const calls = [...seen.values()].sort((x, y) => x[0] - y[0] || x[3] - y[3]);
		const bps = [...new Set([...rows.flatMap((r) => r[2]), ...calls.map((c) => c[0])])].sort((x, y) => x - y);
		out.set(line, { rows, calls, bps, noStmt: e.noStmt });
	}
	return out;
}

// The file a user typed ("init/main.c", "main.c") among `paths`: { path }
// for an exact name or a single one ending in "/" + it, else { why }.
function dwarfResolvePath(paths, q, none) {
	q = q.replace(/^\.?\//, "");
	if (paths.includes(q))
		return { path: q };
	const hits = paths.filter((p) => p.endsWith("/" + q));
	if (hits.length === 1)
		return { path: hits[0] };
	return { why: hits.length ? q + " is ambiguous: " + hits.slice(0, 5).join(", ") + (hits.length > 5 ? ", …" : "") :
		none || "no source file " + q + " with code in this kernel's line table" };
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
// "MKBD" 2, strings (count, then NUL-terminated UTF-8), line rows (count,
// then address delta, file+1 zigzag delta * 2 + lk, line zigzag delta),
// ranges (count, then start delta, length, depth, name+1, call file+1,
// call line), breakpoint addresses inside rows (count, then address
// deltas), all unsigned LEB128. Version 1 had no lk bit and no sx.

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

function dwarfDecode(b) {
	const ver = b[4];
	if (b[0] !== 0x4d || b[1] !== 0x4b || b[2] !== 0x42 || b[3] !== 0x44 || (ver !== 1 && ver !== 2))
		throw new Error("not a DWARF shard (MKBD 1 or 2)");
	let p = 5;
	const uv = () => { let r = 0, s = 0, x; do { x = b[p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return r; };
	const unzig = (v) => v % 2 ? -(v + 1) / 2 : v / 2;
	const zz = () => unzig(uv());
	const strings = [];
	for (let n = uv(); n > 0; n--) {
		let e = p;
		while (b[e]) e++;
		strings.push(textDecoder.decode(b.subarray(p, e)));
		p = e + 1;
	}
	const nl = uv();
	const la = new Float64Array(nl), lf = new Int32Array(nl), ll = new Uint32Array(nl), lk = new Uint8Array(nl);
	let pa = 0, pf = 0, pl = 0;
	for (let k = 0; k < nl; k++) {
		pa += uv(); la[k] = pa;
		if (ver === 1)
			pf += zz();
		else {
			const v = uv();
			lk[k] = v % 2;
			pf += unzig(Math.floor(v / 2));
		}
		lf[k] = pf - 1;
		pl += zz(); ll[k] = pl;
	}
	const nr = uv();
	const t = { strings, la, lf, ll, lk, sx: null, ra: new Float64Array(nr), rh: new Float64Array(nr), rd: new Uint8Array(nr),
		rn: new Int32Array(nr), rf: new Int32Array(nr), rl: new Uint32Array(nr), maxLen: 0 };
	pa = 0;
	for (let k = 0; k < nr; k++) {
		pa += uv(); t.ra[k] = pa;
		t.rh[k] = pa + uv(); t.rd[k] = uv(); t.rn[k] = uv() - 1; t.rf[k] = uv() - 1; t.rl[k] = uv();
		t.maxLen = Math.max(t.maxLen, t.rh[k] - pa);
	}
	const ns = ver === 1 ? 0 : uv();
	t.sx = new Float64Array(ns);
	pa = 0;
	for (let k = 0; k < ns; k++) {
		pa += uv(); t.sx[k] = pa;
	}
	if (ver === 1)
		t.noStmt = true;
	return t;
}

if (typeof module !== "undefined")
	module.exports = { DwarfFuncs, dwFuncShard, dwFuncBase, dwFuncProto, dwarfScopeGroups, dwarfScopesAt, dwLocAt, dwarfLocalFrames, DwarfTypes, DWT, dwStrip, dwTypeSize, dwTypeName, dwWhere, dwPathIn, dwVarIndex, dwVarDecl, dwVarDecls, dwVarDeclText, dwVarsPick, DwarfReader, dwarfElf, dwarfSectionOf, dwarfSections, dwarfTable, dwarfSlice, dwarfFrames, dwarfFunction, dwarfRow, dwarfEachLine, dwarfFileAddrs, dwarfFileCode, dwarfResolvePath, dwarfEncode, dwarfDecode, dwDisplayPath };
