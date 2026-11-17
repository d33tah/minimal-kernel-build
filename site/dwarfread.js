"use strict";

// DWARF's reader (dwarf.js): the tags, attributes and forms, and
// DwarfReaderBase, which reads the units, their abbreviations and DIEs and
// the line programs.

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

const DWT = { base: 0, ptr: 1, const: 2, volatile: 3, restrict: 4, atomic: 5, typedef: 6, struct: 7, union: 8, enum: 9,
	array: 10, func: 11, void: 12 };

class DwarfReaderBase {
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
		case 0x16: { const f = uleb(); c.p = p; c.form = f; return this.form(c, f, 0, want); } // indirect: c.form is the real one
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

	// Attributes of the DIE at c.p as { attr: [form, value] } (all of them;
	// DW_FORM_indirect's with the form the DIE gives); a block (exprloc,
	// blockN) is [form, offset in c.b, length].
	readDie(c, ab) {
		const a = {};
		const s = ab.specs;
		for (let k = 0; k < s.length; k += 3) {
			c.blen = -1;
			c.form = s[k + 1];
			const v = this.form(c, s[k + 1], s[k + 2], true);
			a[s[k]] = c.blen >= 0 ? [c.form, v, c.blen] : [c.form, v];
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
}

export { DW_TAG_SUBPROGRAM, DW_TAG_INLINED, DW_CODE_PARENTS, AT, DW_TAG_LEXICAL_BLOCK, DW_TYPE_TAGS, DW_TAG_MEMBER,
	DW_TAG_ENUMERATOR, DW_TAG_SUBRANGE, DW_TAG_PARAM, DW_TAG_UNSPEC_PARAMS, DW_TAG_VARIABLE, dwAddrForm, u32le, u16le,
	textDecoder, cstrAt, DWT, DwarfReaderBase };
