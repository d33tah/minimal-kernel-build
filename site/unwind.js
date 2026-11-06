"use strict";

// Call frame information (DWARF CFI) and the backtrace at a pause.
//
// One file for three users, like dwarf.js: site/gen-dwarf.js (node, CI)
// parses a -g vmlinux's .debug_frame (or .eh_frame) and writes it as
// address shards for the shipped kernels; dwarfworker.js parses an
// imported vmlinux's; the page (dbginfo.js dbgUnwind) runs cfiRow() and
// unwind() on either, so there is one evaluator and one unwinder.
//
// A CFI table (cfiParse, cfiDecode) is
//   { bias, cies: [{ caf, daf, ra, init }], lo, len, cie, off, plen, prog, maxLen }
// with FDE i covering physical [lo[i], lo[i] + len[i]) and its CFA program
// at prog[off[i] .. off[i] + plen[i]); bias = virtual - physical (for
// DW_CFA_set_loc, which names a virtual address). The programs are kept as
// bytes and run when a row is asked for (cfiRow).
//
// i386 DWARF register numbers: 0 EAX, 1 ECX, 2 EDX, 3 EBX, 4 ESP, 5 EBP,
// 6 ESI, 7 EDI, 8 EIP (the return address column of GCC's and clang's CIEs).

const CFI_REGS = ["EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI", "EIP"];
const CFI_ESP = 4, CFI_EBP = 5, CFI_EIP = 8;

// ---- parsing -----------------------------------------------------------------

// .debug_frame (eh false) or .eh_frame (eh true) bytes -> CFI table.
// secAddr: the section's virtual address (pc-relative pointers in
// .eh_frame); toPhys: virtual -> physical or null (FDEs of discarded code
// are dropped).
function cfiParse(b, eh, secAddr, toPhys) {
	const u32 = (p) => (b[p] | b[p + 1] << 8 | b[p + 2] << 16 | b[p + 3] << 24) >>> 0;
	let p = 0;
	const uleb = () => { let r = 0, s = 0, x; do { x = b[p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return r; };
	const sleb = () => { let r = 0, s = 0, x; do { x = b[p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return x & 0x40 ? r - 2 ** s : r; };
	// .eh_frame pointer encodings: absptr, udata2/4, sdata2/4, uleb/sleb,
	// pc-relative; indirect and data-relative pointers are not used by a kernel.
	const ptr = (enc) => {
		const at = p;
		let v;
		switch (enc & 0x0f) {
		case 0x00: case 0x03: v = u32(p); p += 4; break;
		case 0x0b: v = u32(p) | 0; p += 4; break;
		case 0x02: v = b[p] | b[p + 1] << 8; p += 2; break;
		case 0x0a: v = (b[p] | b[p + 1] << 8) << 16 >> 16; p += 2; break;
		case 0x01: v = uleb(); break;
		case 0x09: v = sleb(); break;
		default: throw new Error("unsupported .eh_frame pointer encoding 0x" + enc.toString(16));
		}
		if ((enc & 0x70) === 0x10)
			v += secAddr + at;
		else if (enc & 0x70)
			throw new Error("unsupported .eh_frame pointer application 0x" + enc.toString(16));
		return v >>> 0;
	};
	const cies = [], cieAt = new Map();
	const fdes = []; // [lo (physical), len, cie, off, plen]
	let bias = null, biasMixed = false;
	const cie = (start, end) => {
		const c = { caf: 1, daf: 1, ra: CFI_EIP, init: null, enc: 0 };
		const ver = b[p++];
		let aug = "";
		while (b[p]) aug += String.fromCharCode(b[p++]);
		p++;
		if (ver >= 4)
			p += 2; // address size, segment selector size
		c.caf = uleb();
		c.daf = sleb();
		c.ra = ver === 1 ? b[p++] : uleb();
		if (aug[0] === "z") {
			const n = uleb(), stop = p + n;
			for (const a of aug.slice(1)) {
				if (a === "R")
					c.enc = b[p++];
				else if (a === "L")
					p++;
				else if (a === "P") {
					const e = b[p++];
					ptr(e & 0x7f);
				}
			}
			p = stop;
		} else if (aug && aug !== "")
			throw new Error("unsupported CIE augmentation " + JSON.stringify(aug));
		c.init = b.slice(p, end);
		c.aug = aug;
		cieAt.set(start, cies.length);
		cies.push(c);
	};
	const pending = []; // FDEs before their CIE (allowed in .debug_frame)
	while (p + 4 <= b.length) {
		const start = p;
		const len = u32(p);
		p += 4;
		if (len === 0) { // .eh_frame terminator
			if (eh) break;
			continue;
		}
		if (len === 0xffffffff)
			throw new Error("64-bit DWARF CFI is not supported");
		const end = p + len;
		const idAt = p;
		const id = u32(p);
		p += 4;
		if (eh ? id === 0 : id === 0xffffffff)
			cie(start, end);
		else
			pending.push([start, end, eh ? idAt - id : id]);
		p = end;
	}
	// FDEs after every CIE is known (.debug_frame may put a CIE after its FDEs).
	for (const [start, end, cieOff] of pending) {
		const ci = cieAt.get(cieOff);
		if (ci === undefined)
			continue;
		p = start + 8;
		const c = cies[ci];
		let lo, range;
		if (eh) {
			lo = ptr(c.enc);
			range = ptr(c.enc & 0x0f);
			if (c.aug[0] === "z")
				p += uleb();
		} else {
			lo = u32(p);
			range = u32(p + 4);
			p += 8;
		}
		const pa = lo && range ? toPhys(lo) : null;
		if (pa === null)
			continue;
		if (bias === null)
			bias = lo - pa;
		else if (bias !== lo - pa)
			biasMixed = true;
		fdes.push([pa, range, ci, p, end - p]);
	}
	fdes.sort((x, y) => x[0] - y[0]);
	return cfiPack(cies.map(({ caf, daf, ra, init }) => ({ caf, daf, ra, init })), fdes, b, biasMixed ? null : bias || 0);
}

// The CFI of an ELF (dwarf.js dwarfElf): .debug_frame, else .eh_frame.
// { t, source } or { why }. toPhys: virtual -> physical (the page's
// offset for an import, the PT_LOAD mapping in CI).
function cfiFromElf(elf, read, toPhys) {
	for (const [name, eh] of [[".debug_frame", false], [".eh_frame", true]]) {
		const s = elf.sections[name];
		if (s && s.size)
			return { t: cfiParse(read(s.offset, s.size), eh, s.addr, toPhys), source: name };
	}
	return { why: "this vmlinux has no call frame information (.debug_frame or .eh_frame)" };
}

// cies + [[lo, len, cie, offset in src, length], ...] -> CFI table with
// its own prog buffer (only the programs it holds).
function cfiPack(cies, fdes, src, bias) {
	const n = fdes.length;
	const t = { bias, cies, lo: new Float64Array(n), len: new Uint32Array(n), cie: new Uint32Array(n), off: new Uint32Array(n),
		plen: new Uint32Array(n), prog: null, maxLen: 0 };
	let size = 0;
	for (const f of fdes)
		size += f[4];
	t.prog = new Uint8Array(size);
	let o = 0;
	fdes.forEach(([lo, len, ci, off, plen], i) => {
		t.lo[i] = lo; t.len[i] = len; t.cie[i] = ci; t.off[i] = o; t.plen[i] = plen;
		t.prog.set(src.subarray(off, off + plen), o);
		o += plen;
		t.maxLen = Math.max(t.maxLen, len);
	});
	return t;
}

// The FDEs of t that overlap [lo, hi), with the CIEs they use.
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

// Index of the FDE covering pa, or -1.
function cfiFind(t, pa) {
	let a = 0, b = t.lo.length;
	while (a < b) { const m = (a + b) >> 1; if (t.lo[m] <= pa) a = m + 1; else b = m; }
	for (let k = a - 1; k >= 0 && t.lo[k] >= pa - t.maxLen; k--)
		if (pa < t.lo[k] + t.len[k])
			return k;
	return -1;
}

// ---- shard files ------------------------------------------------------------
// "MKBC" 1, bias + 1 (0: mixed), CIEs (count, then code alignment,
// data alignment zigzag, return address column, program length, program),
// FDEs (count, then start delta, length, CIE, program length, program),
// numbers unsigned LEB128.

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

function cfiDecode(b) {
	if (b[0] !== 0x4d || b[1] !== 0x4b || b[2] !== 0x42 || b[3] !== 0x43 || b[4] !== 1)
		throw new Error("not a CFI shard (MKBC 1)");
	let p = 5;
	const uv = () => { let r = 0, s = 0, x; do { x = b[p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return r; };
	const bias = uv();
	const cies = [];
	for (let n = uv(); n > 0; n--) {
		const caf = uv(), z = uv(), ra = uv(), len = uv();
		cies.push({ caf, daf: z % 2 ? -(z + 1) / 2 : z / 2, ra, init: b.slice(p, p + len) });
		p += len;
	}
	const fdes = [];
	let pa = 0;
	for (let n = uv(); n > 0; n--) {
		pa += uv();
		const len = uv(), ci = uv(), plen = uv();
		fdes.push([pa, len, ci, p, plen]);
		p += plen;
	}
	return cfiPack(cies, fdes, b, bias ? bias - 1 : null);
}

// ---- rows ------------------------------------------------------------------------

// The unwind row of the FDE covering pa: { lo, cfa, regs } with
//   cfa   { reg, off } or { expr } (DW_CFA_def_cfa_expression bytes)
//   regs  { n: { k, v } }, k one of "off" ([CFA+v]), "valoff" (CFA+v),
//         "reg" (register v), "expr" ([expr], v the bytes), "valexpr",
//         "undef", "same"
// lo: where the row starts. null when no FDE covers pa; { why } when the
// program cannot be run.
function cfiRow(t, pa) {
	const i = cfiFind(t, pa);
	if (i < 0)
		return null;
	const c = t.cies[t.cie[i]];
	let row = { cfa: { reg: CFI_ESP, off: 4 }, regs: {} };
	const copy = (r) => ({ cfa: { ...r.cfa }, regs: { ...r.regs } });
	let loc = t.lo[i], initial = null;
	const stack = [];
	const run = (b, p, end, isInit) => {
		const uleb = () => { let r = 0, s = 0, x; do { x = b[p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return r; };
		const sleb = () => { let r = 0, s = 0, x; do { x = b[p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return x & 0x40 ? r - 2 ** s : r; };
		const block = () => { const n = uleb(); const v = b.slice(p, p + n); p += n; return v; };
		const restore = (r) => {
			if (initial && initial.regs[r])
				row.regs[r] = initial.regs[r];
			else
				delete row.regs[r];
		};
		const advance = (d) => {
			if (!isInit && loc + d > pa)
				return false;
			loc += d;
			return true;
		};
		while (p < end) {
			const op = b[p++];
			const hi = op & 0xc0, low = op & 0x3f;
			if (hi === 0x40) {
				if (!advance(low * c.caf)) return false;
				continue;
			}
			if (hi === 0x80) {
				row.regs[low] = { k: "off", v: uleb() * c.daf };
				continue;
			}
			if (hi === 0xc0) {
				restore(low);
				continue;
			}
			switch (op) {
			case 0x00: break;
			case 0x01: { // set_loc: a virtual address
				const va = (b[p] | b[p + 1] << 8 | b[p + 2] << 16 | b[p + 3] << 24) >>> 0;
				p += 4;
				if (t.bias === null)
					throw new Error("DW_CFA_set_loc in a table with mixed address offsets");
				const to = va - t.bias;
				if (!isInit && to > pa) return false;
				loc = to;
				break;
			}
			case 0x02: if (!advance(b[p++] * c.caf)) return false; break;
			case 0x03: { const d = b[p] | b[p + 1] << 8; p += 2; if (!advance(d * c.caf)) return false; break; }
			case 0x04: { const d = (b[p] | b[p + 1] << 8 | b[p + 2] << 16 | b[p + 3] << 24) >>> 0; p += 4; if (!advance(d * c.caf)) return false; break; }
			case 0x05: { const r = uleb(); row.regs[r] = { k: "off", v: uleb() * c.daf }; break; }
			case 0x06: restore(uleb()); break;
			case 0x07: row.regs[uleb()] = { k: "undef" }; break;
			case 0x08: row.regs[uleb()] = { k: "same" }; break;
			case 0x09: { const r = uleb(); row.regs[r] = { k: "reg", v: uleb() }; break; }
			case 0x0a: stack.push(copy(row)); break;
			case 0x0b: {
				// GCC's remember/restore keep the CFA of the restored state too.
				const s = stack.pop();
				if (s) row = s;
				break;
			}
			case 0x0c: row.cfa = { reg: uleb(), off: uleb() }; break;
			case 0x0d: row.cfa = { reg: uleb(), off: row.cfa.expr ? 0 : row.cfa.off }; break;
			case 0x0e: row.cfa = { reg: row.cfa.reg, off: uleb() }; break;
			case 0x0f: row.cfa = { expr: block() }; break;
			case 0x10: { const r = uleb(); row.regs[r] = { k: "expr", v: block() }; break; }
			case 0x11: { const r = uleb(); row.regs[r] = { k: "off", v: sleb() * c.daf }; break; }
			case 0x12: row.cfa = { reg: uleb(), off: sleb() * c.daf }; break;
			case 0x13: row.cfa = { reg: row.cfa.reg, off: sleb() * c.daf }; break;
			case 0x14: { const r = uleb(); row.regs[r] = { k: "valoff", v: uleb() * c.daf }; break; }
			case 0x15: { const r = uleb(); row.regs[r] = { k: "valoff", v: sleb() * c.daf }; break; }
			case 0x16: { const r = uleb(); row.regs[r] = { k: "valexpr", v: block() }; break; }
			case 0x2e: uleb(); break; // GNU_args_size
			case 0x2f: { const r = uleb(); row.regs[r] = { k: "off", v: -uleb() * c.daf }; break; }
			default: throw new Error("unknown DW_CFA opcode 0x" + op.toString(16));
			}
		}
		return true;
	};
	try {
		run(c.init, 0, c.init.length, true);
		initial = copy(row);
		loc = t.lo[i];
		run(t.prog, t.off[i], t.off[i] + t.plen[i], false);
	} catch (e) {
		return { why: e.message };
	}
	return { lo: loc, fde: [t.lo[i], t.lo[i] + t.len[i]], ra: c.ra, cfa: row.cfa, regs: row.regs };
}

// ---- DWARF expressions (shared with locations of variables) ------------------

const DW_OP_NAMES = { 0x06: "DW_OP_deref", 0x23: "DW_OP_plus_uconst", 0x22: "DW_OP_plus", 0x1c: "DW_OP_minus",
	0x9c: "DW_OP_call_frame_cfa", 0x9f: "DW_OP_stack_value", 0x12: "DW_OP_dup", 0x13: "DW_OP_drop", 0x1a: "DW_OP_and" };
// Names of the other operations, for location tooltips (operands as "…").
const DW_OP_MORE = { 0x08: "DW_OP_const1u", 0x09: "DW_OP_const1s", 0x0a: "DW_OP_const2u", 0x0b: "DW_OP_const2s", 0x0c: "DW_OP_const4u",
	0x0d: "DW_OP_const4s", 0x14: "DW_OP_over", 0x15: "DW_OP_pick", 0x16: "DW_OP_swap", 0x17: "DW_OP_rot", 0x19: "DW_OP_abs",
	0x1b: "DW_OP_div", 0x1d: "DW_OP_mod", 0x1e: "DW_OP_mul", 0x1f: "DW_OP_neg", 0x20: "DW_OP_not", 0x21: "DW_OP_or",
	0x24: "DW_OP_shl", 0x25: "DW_OP_shr", 0x26: "DW_OP_shra", 0x27: "DW_OP_xor", 0x28: "DW_OP_bra", 0x29: "DW_OP_eq",
	0x2a: "DW_OP_ge", 0x2b: "DW_OP_gt", 0x2c: "DW_OP_le", 0x2d: "DW_OP_lt", 0x2e: "DW_OP_ne", 0x2f: "DW_OP_skip",
	0x92: "DW_OP_bregx", 0x94: "DW_OP_deref_size", 0x96: "DW_OP_nop", 0x9d: "DW_OP_bit_piece", 0x9e: "DW_OP_implicit_value",
	0xa0: "DW_OP_implicit_pointer", 0xa1: "DW_OP_addrx", 0xa4: "DW_OP_const_type", 0xa5: "DW_OP_regval_type",
	0xa6: "DW_OP_deref_type", 0xa8: "DW_OP_convert", 0xa9: "DW_OP_reinterpret", 0xf2: "DW_OP_GNU_implicit_pointer",
	0xf4: "DW_OP_GNU_const_type", 0xf5: "DW_OP_GNU_regval_type", 0xf6: "DW_OP_GNU_deref_type", 0xf7: "DW_OP_GNU_convert",
	0xf9: "DW_OP_GNU_reinterpret", 0xfa: "DW_OP_GNU_parameter_ref", 0xfd: "DW_OP_GNU_variable_value" };

// Where the DWARF operation at b[p] ends (operands included); -1 for an
// operation this reader does not know.
function dwOpEnd(b, p) {
	const op = b[p++];
	const leb = () => { while (b[p++] & 0x80); };
	if (op >= 0x30 && op <= 0x6f || op >= 0x12 && op <= 0x14 || op >= 0x16 && op <= 0x22 || op >= 0x24 && op <= 0x27 ||
	    op >= 0x29 && op <= 0x2e || op === 0x06 || op === 0x96 || op === 0x97 || op === 0x9b || op === 0x9c || op === 0x9f || op === 0xe0 || op === 0xf0)
		return p;
	if (op >= 0x70 && op <= 0x8f) { leb(); return p; }
	switch (op) {
	case 0x03: case 0x0c: case 0x0d: case 0x99: case 0x9a: case 0xfa: case 0xfd: return p + 4;
	case 0x08: case 0x09: case 0x15: case 0x94: case 0x95: return p + 1;
	case 0x0a: case 0x0b: case 0x28: case 0x2f: case 0x98: return p + 2;
	case 0x0e: case 0x0f: return p + 8;
	case 0x10: case 0x11: case 0x23: case 0x90: case 0x91: case 0x93: case 0xa1: case 0xa2: case 0xa8: case 0xa9:
	case 0xf7: case 0xf9: case 0xfb: case 0xfc: leb(); return p;
	case 0x92: case 0x9d: case 0xa5: case 0xf5: leb(); leb(); return p;
	case 0x9e: case 0xa3: case 0xf3: { let n = 0, s = 0, x; do { x = b[p++]; n += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return p + n; }
	case 0xa0: case 0xf2: p += 4; leb(); return p;
	case 0xa4: case 0xf4: leb(); return p + 1 + b[p];
	case 0xa6: case 0xf6: p++; leb(); return p;
	default: return -1;
	}
}

// Text of an expression the way llvm-dwarfdump prints CFI expressions
// ("DW_OP_breg5 EBP-4, DW_OP_deref"), for tooltips and the tests.
function dwExprText(b) {
	const out = [];
	let p = 0;
	const uleb = () => { let r = 0, s = 0, x; do { x = b[p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return r; };
	const sleb = () => { let r = 0, s = 0, x; do { x = b[p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return x & 0x40 ? r - 2 ** s : r; };
	const sgn = (v) => (v < 0 ? "" : "+") + v;
	while (p < b.length) {
		const op = b[p++];
		if (op >= 0x70 && op <= 0x8f) {
			const r = op - 0x70;
			out.push("DW_OP_breg" + r + " " + (CFI_REGS[r] || "r" + r) + sgn(sleb()));
		} else if (op >= 0x50 && op <= 0x6f)
			out.push("DW_OP_reg" + (op - 0x50) + " " + (CFI_REGS[op - 0x50] || ""));
		else if (op >= 0x30 && op <= 0x4f)
			out.push("DW_OP_lit" + (op - 0x30));
		else if (op === 0x23)
			out.push("DW_OP_plus_uconst 0x" + uleb().toString(16));
		else if (op === 0x91)
			out.push("DW_OP_fbreg " + sleb());
		else if (op === 0x11)
			out.push("DW_OP_consts " + sleb());
		else if (op === 0x10) {
			let r = 0n, sh = 0n, x;
			do { x = b[p++]; r |= BigInt(x & 0x7f) << sh; sh += 7n; } while (x & 0x80);
			out.push("DW_OP_constu 0x" + r.toString(16));
		}
		else if (DW_OP_NAMES[op])
			out.push(DW_OP_NAMES[op]);
		else if (op === 0x03) {
			out.push("DW_OP_addr 0x" + ((b[p] | b[p + 1] << 8 | b[p + 2] << 16 | b[p + 3] << 24) >>> 0).toString(16));
			p += 4;
		} else if (op === 0x93)
			out.push("DW_OP_piece 0x" + uleb().toString(16));
		else if (op === 0x90)
			out.push("DW_OP_regx " + (CFI_REGS[uleb()] || "?"));
		else if (op === 0xa3 || op === 0xf3) {
			const n = uleb();
			out.push("DW_OP_entry_value(" + dwExprText(b.subarray(p, p + n)) + ")");
			p += n;
		} else {
			const e = dwOpEnd(b, p - 1);
			out.push((DW_OP_MORE[op] || "DW_OP_0x" + op.toString(16)) + (e > p ? " …" : ""));
			if (e < 0)
				break;
			p = e;
		}
	}
	return out.join(", ");
}

// Evaluate a DWARF expression. ctx: reg(n) -> value or undefined,
// read(addr, size) -> value or undefined, cfa (DW_OP_call_frame_cfa),
// frameBase (DW_OP_fbreg). stack: initial stack (CFI rules push the CFA).
// Returns { value } (an address, or the value with DW_OP_stack_value:
// stackValue true) or { why } with the reason it has no value.
function dwExpr(b, ctx, stack = []) {
	const st = stack.slice();
	let p = 0, stackValue = false;
	// Values are 32-bit (the address size): LEB128 operands are taken modulo
	// 2^32, so clang's 64-bit encodings of negative constants stay exact.
	const uleb = () => { let r = 0, s = 0, x; do { x = b[p++]; if (s < 32) r = (r + (x & 0x7f) * 2 ** s) % 2 ** 32; s += 7; } while (x & 0x80); return r; };
	const sleb = () => {
		let r = 0, s = 0, x;
		do { x = b[p++]; if (s < 32) r = (r + (x & 0x7f) * 2 ** s) % 2 ** 32; s += 7; } while (x & 0x80);
		if (x & 0x40 && s < 32)
			r = (r - 2 ** s + 2 ** 32) % 2 ** 32;
		return r >= 2 ** 31 ? r - 2 ** 32 : r;
	};
	const u = (v) => v >>> 0;
	const pop = () => {
		if (!st.length)
			throw new Error("DWARF expression stack underflow");
		return st.pop();
	};
	const reg = (r) => {
		const v = ctx.reg ? ctx.reg(r) : undefined;
		if (v === undefined)
			throw new Error((CFI_REGS[r] || "register " + r) + " is not known in this frame");
		return v;
	};
	const read = (a, n) => {
		const v = ctx.read ? ctx.read(u(a), n) : undefined;
		if (v === undefined)
			throw new Error("memory at 0x" + u(a).toString(16) + " cannot be read");
		return v;
	};
	try {
		while (p < b.length) {
			const op = b[p++];
			if (op >= 0x30 && op <= 0x4f) { st.push(op - 0x30); continue; }
			if (op >= 0x70 && op <= 0x8f) { st.push(u(reg(op - 0x70) + sleb())); continue; }
			if (op >= 0x50 && op <= 0x6f)
				return { why: "in register " + (CFI_REGS[op - 0x50] || op - 0x50), reg: op - 0x50 };
			switch (op) {
			case 0x03: st.push(u(b[p] | b[p + 1] << 8 | b[p + 2] << 16 | b[p + 3] << 24)); p += 4; break; // addr
			case 0x06: st.push(read(pop(), 4)); break; // deref
			case 0x08: st.push(b[p++]); break;
			case 0x09: st.push(b[p++] << 24 >> 24); break;
			case 0x0a: st.push(b[p] | b[p + 1] << 8); p += 2; break;
			case 0x0b: st.push((b[p] | b[p + 1] << 8) << 16 >> 16); p += 2; break;
			case 0x0c: st.push(u(b[p] | b[p + 1] << 8 | b[p + 2] << 16 | b[p + 3] << 24)); p += 4; break;
			case 0x0d: st.push(b[p] | b[p + 1] << 8 | b[p + 2] << 16 | b[p + 3] << 24); p += 4; break;
			case 0x10: st.push(uleb()); break;
			case 0x11: st.push(sleb()); break;
			case 0x12: st.push(st[st.length - 1]); break; // dup
			case 0x13: pop(); break;
			case 0x14: st.push(st[st.length - 2]); break; // over
			case 0x15: st.push(st[st.length - 1 - b[p++]]); break; // pick
			case 0x16: { const a = pop(), c = pop(); st.push(a, c); break; } // swap
			case 0x17: { const a = pop(), c = pop(), d = pop(); st.push(a, d, c); break; } // rot
			case 0x19: st.push(Math.abs(pop() | 0)); break;
			case 0x1a: { const a = pop(); st.push(u(pop() & a)); break; }
			case 0x1b: { const a = pop() | 0; if (!a) throw new Error("division by zero"); st.push(u(Math.trunc((pop() | 0) / a))); break; }
			case 0x1c: { const a = pop(); st.push(u(pop() - a)); break; }
			case 0x1d: { const a = pop(); if (!a) throw new Error("division by zero"); st.push(u(pop() % a)); break; }
			case 0x1e: { const a = pop(); st.push(u(Math.imul(pop(), a))); break; }
			case 0x1f: st.push(u(-pop())); break;
			case 0x20: st.push(u(~pop())); break;
			case 0x21: { const a = pop(); st.push(u(pop() | a)); break; }
			case 0x22: { const a = pop(); st.push(u(pop() + a)); break; }
			case 0x23: st.push(u(pop() + uleb())); break;
			case 0x24: { const a = pop(); st.push(u(pop() << a)); break; }
			case 0x25: { const a = pop(); st.push(pop() >>> a); break; }
			case 0x26: { const a = pop(); st.push(u(pop() >> a)); break; }
			case 0x27: { const a = pop(); st.push(u(pop() ^ a)); break; }
			case 0x28: { const d = (b[p] | b[p + 1] << 8) << 16 >> 16; p += 2; if (pop()) p += d; break; } // bra
			case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: { // eq ge gt le lt ne
				const a = pop() | 0, c = pop() | 0;
				st.push(+[c === a, c >= a, c > a, c <= a, c < a, c !== a][op - 0x29]);
				break;
			}
			case 0x2f: { const d = (b[p] | b[p + 1] << 8) << 16 >> 16; p += 2; p += d; break; } // skip
			case 0x90: return { why: "in register " + (CFI_REGS[uleb()] || "?"), reg: b[p - 1] };
			case 0x91: { // fbreg
				if (ctx.frameBase === undefined)
					throw new Error("the frame base is not known");
				st.push(u(ctx.frameBase + sleb()));
				break;
			}
			case 0x92: { const r = uleb(); st.push(u(reg(r) + sleb())); break; } // bregx
			case 0x94: { const n = b[p++]; st.push(read(pop(), n)); break; } // deref_size
			case 0x96: break; // nop
			case 0x9c: // call_frame_cfa
				if (ctx.cfa === undefined)
					throw new Error("the CFA is not known");
				st.push(ctx.cfa);
				break;
			case 0x9f: stackValue = true; break;
			case 0xa8: case 0xf7: case 0xa9: case 0xf9: uleb(); break; // convert / reinterpret: 32-bit values stay as they are
			case 0xa5: case 0xf5: { const r = uleb(); uleb(); st.push(reg(r)); break; } // regval_type
			case 0xa4: case 0xf4: { // const_type
				uleb();
				const n = b[p++];
				if (n > 4)
					throw new Error("a " + n + "-byte typed constant is not supported");
				let v = 0;
				for (let k = n - 1; k >= 0; k--) v = v * 256 + b[p + k];
				p += n;
				st.push(v);
				break;
			}
			case 0xa6: case 0xf6: { const n = b[p++]; uleb(); if (n > 4) throw new Error("a " + n + "-byte typed read is not supported"); st.push(read(pop(), n)); break; }
			case 0xa0: case 0xf2: throw new Error("an implicit pointer: it points to a variable the compiler optimized out");
			case 0xfa: throw new Error("optimized out (DW_OP_GNU_parameter_ref: the value its caller passed, not kept)");
			case 0xa3: case 0xf3: return { why: "optimized out (only its value at the function's entry is described)" };
			default: throw new Error("DWARF expression op 0x" + op.toString(16) + " is not supported");
			}
		}
	} catch (e) {
		return { why: e.message };
	}
	if (!st.length)
		return { why: "empty DWARF expression" };
	return { value: u(st[st.length - 1]), stackValue };
}

// Where a variable is, from its DWARF location description b (an
// expression, maybe in pieces) for an object of size bytes:
//   { mem, how }                  the object is in memory at virtual mem
//   { bytes, known, how, whys }   its bytes (known[k] 1 where byte k is
//                                 known): a register, a computed value
//                                 (DW_OP_stack_value), a constant
//                                 (DW_OP_implicit_value) or pieces of these
//   { why }                       no value (optimized out, unreadable)
// ctx: as dwExpr's, plus readBytes(va, n) -> Uint8Array or null.
const DW_REGPARM = { 0: "the 1st", 2: "the 2nd", 1: "the 3rd" };
function dwLocation(b, ctx, size) {
	const segs = [];
	let p = 0, start = 0, pieces = false;
	while (p < b.length) {
		const e = dwOpEnd(b, p);
		if (e < 0 || e > b.length)
			return { why: "DWARF operation 0x" + b[p].toString(16) + " is not supported" };
		if (b[p] === 0x93 || b[p] === 0x9d) {
			const q = { v: p + 1 };
			const uleb = () => { let r = 0, s = 0, x; do { x = b[q.v++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return r; };
			const n = uleb(), off = b[p] === 0x9d ? uleb() : 0;
			segs.push({ e: b.subarray(start, p), n: b[p] === 0x93 ? n : n / 8, bits: b[p] === 0x9d && (n % 8 || off) ? [n, off] : null });
			pieces = true;
			start = e;
		}
		p = e;
	}
	if (!pieces)
		segs.push({ e: b, n: size, bits: null });
	const regName = (r) => CFI_REGS[r] || "register " + r;
	const one = (e) => {
		if (!e.length)
			return { why: "optimized out" };
		if (e[0] >= 0x50 && e[0] <= 0x6f || e[0] === 0x90) {
			let r = e[0] - 0x50;
			if (e[0] === 0x90) { // regx: the operand
				r = 0;
				for (let k = 1, s = 0; k < e.length; k++, s += 7) {
					r += (e[k] & 0x7f) * 2 ** s;
					if (!(e[k] & 0x80))
						break;
				}
			}
			const v = ctx.reg ? ctx.reg(r) : undefined;
			return v === undefined ? { why: "in " + regName(r) + ", which is not known in this frame" +
				(r <= 2 ? " (" + regName(r) + " is not saved across calls)" : "") } : { val: v >>> 0, how: "in " + regName(r) };
		}
		if (e[0] === 0x9e) {
			const q = { v: 1 };
			let n = 0, s = 0, x;
			do { x = e[q.v++]; n += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80);
			return { bytes: e.subarray(q.v, q.v + n), how: "a constant (DW_OP_implicit_value)" };
		}
		for (let k = 0; k < e.length;) {
			if (e[k] === 0xa3 || e[k] === 0xf3) {
				const inner = e.subarray(k + 2, k + 2 + e[k + 1]);
				const r = inner.length === 1 && inner[0] >= 0x50 && inner[0] <= 0x6f ? inner[0] - 0x50 : -1;
				return { why: "optimized out: only its value at the function's entry is described" + (r >= 0 ? " (it arrived in " + regName(r) +
					(DW_REGPARM[r] ? ", " + DW_REGPARM[r] + " argument with -mregparm=3" : "") + "; the register has been reused since)" : "") };
			}
			k = dwOpEnd(e, k);
		}
		const v = dwExpr(e, ctx);
		if (v.why)
			return { why: v.why };
		return v.stackValue ? { val: v.value, how: "computed (" + dwExprText(e) + ")" } : { mem: v.value, how: "at 0x" + v.value.toString(16) + " (" + dwExprText(e) + ")" };
	};
	if (!pieces) {
		const r = one(b);
		if (r.why || r.mem !== undefined)
			return r;
		segs[0].r = r;
	} else
		for (const sg of segs)
			sg.r = one(sg.e);
	const total = segs.reduce((n, sg) => n + Math.ceil(sg.n), 0);
	const bytes = new Uint8Array(Math.max(total, size)), known = new Uint8Array(bytes.length);
	const hows = [], whys = [];
	let at = 0;
	for (const sg of segs) {
		const n = Math.ceil(sg.n), r = sg.r;
		let src = null;
		if (sg.bits)
			r.why = "a bit piece (" + sg.bits[0] + " bits at bit " + sg.bits[1] + ") is not supported";
		if (r.why)
			whys.push(r.why);
		else if (r.mem !== undefined) {
			src = ctx.readBytes ? ctx.readBytes(r.mem, n) : null;
			if (!src)
				whys.push("memory at 0x" + r.mem.toString(16) + " cannot be read");
		} else if (r.bytes)
			src = r.bytes;
		else
			src = Uint8Array.of(r.val & 255, r.val >>> 8 & 255, r.val >>> 16 & 255, r.val >>> 24);
		if (src)
			for (let k = 0; k < n && k < src.length; k++) {
				bytes[at + k] = src[k];
				known[at + k] = 1;
			}
		hows.push((pieces ? n + " bytes " : "") + (r.why ? "not available" : r.how));
		at += n;
	}
	return { bytes, known, how: hows.join("; "), whys };
}

// ---- unwinding --------------------------------------------------------------------

// i386 Linux pt_regs: bx cx dx si di bp ax ds es fs gs orig_ax ip cs flags sp ss.
const PT_REGS_SIZE = 17 * 4;
const PT_REGS_ORDER = [3, 1, 2, 6, 7, 5, 0]; // DWARF numbers of bx cx dx si di bp ax

// Backtrace from the registers at a pause.
//   regs   { eip, esp, ebp, eax, ... } (virtual addresses; eip is where the
//          CPU is, phys its physical address)
//   env    rowAt(pa)   -> cfiRow() result, null (no FDE), or "loading"
//          read32(va)  -> value or undefined (through the page tables)
//          toPhys(va)  -> physical address of code, or null
//          symAt(pa)   -> [name, start] of the code symbol, or null
//          isEntry(name) -> assembly entry code that called C with pt_regs
//          entryEsp(pa, esp) -> the ESP the function at pa was entered
//                        with, if the stepper recorded that call
//          framePointer  -> try the EBP chain where there is no CFI
// Returns { frames, state, why } with frames innermost first:
//   { pc (virtual), pa, lookup (pa for file:line: the call, pc - 1, in
//     callers), cfa, regs (DWARF number -> value, known ones), method }
// method: how the frame was found: "regs" (the CPU's registers: the
// innermost frame), "cfi" (from the frame inside it through that
// function's CFI row), "pt_regs" (the context entry code saved: an
// interrupt, exception or system call; the outermost one is user space
// when that was interrupted), "entry" (the return address at the ESP the
// stepper saw the function entered with), "fp" (EBP chain); state: "ok" (reached the outermost frame), "loading" (rows not
// fetched yet) or "stopped" (why says where and why).
function unwind(regs, env, max = 64) {
	const frames = [];
	let R = { 0: regs.eax, 1: regs.ecx, 2: regs.edx, 3: regs.ebx, 4: regs.esp, 5: regs.ebp, 6: regs.esi, 7: regs.edi };
	let pc = regs.eip >>> 0, interrupted = true, method = "regs";
	const out = (state, why) => ({ frames, state, why });
	for (let n = 0; n < max; n++) {
		const pa = env.toPhys(pc);
		if (pa === null)
			return out("stopped", "0x" + pc.toString(16) + " is not code of this kernel");
		const lookup = interrupted ? pa : pa - 1;
		const f = { pc, pa, lookup, regs: { ...R }, method, cfa: undefined };
		frames.push(f);
		const sym = env.symAt(lookup);
		if (sym && env.isEntry(sym[0]) && n > 0) {
			// Entry code called the previous frame's function with ESP at
			// its pt_regs: that frame's CFA. The context it interrupted
			// (or the system call's user space) is saved there.
			const base = frames[n - 1].cfa;
			const v = [];
			for (let k = 0; k < 17; k++) {
				const x = env.read32(base + 4 * k);
				if (x === undefined)
					return out("stopped", sym[0] + ": its saved registers at 0x" + base.toString(16) + " cannot be read");
				v.push(x);
			}
			const ip = v[12], cs = v[13] & 0xffff, flags = v[14];
			if (!(flags & 2) || cs > 0xff)
				return out("stopped", sym[0] + ": no saved registers (pt_regs) at 0x" + base.toString(16) + " (CS 0x" + cs.toString(16) + ", EFLAGS 0x" + flags.toString(16) + ")");
			f.cfa = base + PT_REGS_SIZE;
			f.ptRegs = base;
			if (cs & 3) {
				frames.push({ pc: ip, pa: null, lookup: null, method: "pt_regs", user: true, cfa: undefined,
					regs: Object.fromEntries(PT_REGS_ORDER.map((r, k) => [r, v[k]]).concat([[CFI_ESP, v[15]]])) });
				return out("ok", "entered from user space");
			}
			const nr = {};
			PT_REGS_ORDER.forEach((r, k) => { nr[r] = v[k]; });
			// pt_regs.sp is filled in for kernel-mode entries too (entry_32.S FIXUP_FRAME).
			nr[CFI_ESP] = v[15];
			R = nr;
			pc = ip;
			interrupted = true;
			method = "pt_regs";
			continue;
		}
		const row = env.rowAt(lookup);
		if (row === "loading")
			return out("loading", "reading the call frame information…");
		let next = null, why = "";
		if (row && !row.why) {
			f.row = row;
			next = cfiStep(row, R, env);
			if (next.why) {
				why = next.why;
				next = null;
			}
		} else
			why = row && row.why ? row.why : "no call frame information for " + (sym ? sym[0] : "0x" + pc.toString(16)) + " (assembly?)";
		let how = "cfi";
		if (!next && env.entryEsp && R[CFI_ESP] !== undefined) {
			// The ESP this function was entered with, if the stepper saw the
			// call: the return address is there (assembly that keeps EBX, ESI,
			// EDI and EBP, as the kernel's thunks and helpers do).
			const e = env.entryEsp(lookup, R[CFI_ESP]);
			const ra = e === undefined ? undefined : env.read32(e);
			if (ra !== undefined && env.toPhys(ra) !== null) {
				next = { cfa: (e + 4) >>> 0, ra, R: { 3: R[3], 5: R[5], 6: R[6], 7: R[7], [CFI_ESP]: (e + 4) >>> 0 } };
				how = "entry";
				f.fallbackWhy = why;
			}
		}
		if (!next && env.framePointer && R[CFI_EBP] !== undefined) {
			// EBP chain: [EBP] is the caller's EBP, [EBP + 4] the return address.
			const ebp = R[CFI_EBP], esp = R[CFI_ESP];
			const save = env.read32(ebp), ra = env.read32(ebp + 4);
			if (save !== undefined && ra !== undefined && (esp === undefined || (ebp >= esp && ebp - esp < 8192)) &&
			    save > ebp && env.toPhys(ra) !== null) {
				next = { cfa: ebp + 8, ra, R: { [CFI_EBP]: save, [CFI_ESP]: ebp + 8 } };
				how = "fp";
				f.fallbackWhy = why;
			}
		}
		if (!next)
			return out("stopped", why);
		f.cfa = next.cfa;
		if (next.end)
			return out("ok", "");
		// Each frame's CFA lies above the one inside it, unless the code
		// switched stacks in between (IRQ stacks: another 8 KiB region).
		if (n && next.cfa <= frames[n - 1].cfa && next.cfa >>> 13 === frames[n - 1].cfa >>> 13)
			return out("stopped", "the stack does not grow back up at 0x" + pc.toString(16) + " (CFA 0x" + next.cfa.toString(16) + ")");
		if (frames.some((g) => g.pc === pc && g.cfa === next.cfa && g !== f))
			return out("stopped", "a loop: 0x" + pc.toString(16) + " with CFA 0x" + next.cfa.toString(16) + " again");
		R = next.R;
		pc = next.ra >>> 0;
		interrupted = false;
		method = how;
	}
	return out("stopped", "more than " + max + " frames");
}

// One CFI step: the caller's registers from the row at this frame.
// { cfa, ra, R } or { end: true } (return address undefined: the
// outermost frame) or { why }.
function cfiStep(row, R, env) {
	let cfa;
	if (row.cfa.expr) {
		const v = dwExpr(row.cfa.expr, { reg: (r) => R[r], read: (a) => env.read32(a) });
		if (v.why)
			return { why: "CFA (" + dwExprText(row.cfa.expr) + "): " + v.why };
		cfa = v.value;
	} else {
		if (R[row.cfa.reg] === undefined)
			return { why: "CFA = " + CFI_REGS[row.cfa.reg] + "+" + row.cfa.off + ", and " + CFI_REGS[row.cfa.reg] + " is not known here" };
		cfa = (R[row.cfa.reg] + row.cfa.off) >>> 0;
	}
	const ctx = { reg: (r) => R[r], read: (a) => env.read32(a) };
	const value = (rule, r) => {
		switch (rule.k) {
		case "off": return env.read32((cfa + rule.v) >>> 0);
		case "valoff": return (cfa + rule.v) >>> 0;
		case "reg": return R[rule.v];
		case "same": return R[r];
		case "undef": return undefined;
		case "expr": { const v = dwExpr(rule.v, ctx, [cfa]); return v.why ? undefined : env.read32(v.value); }
		case "valexpr": { const v = dwExpr(rule.v, ctx, [cfa]); return v.why ? undefined : v.value; }
		}
		return undefined;
	};
	const ra = row.regs[row.ra];
	if (!ra || ra.k === "undef")
		return { end: true, cfa };
	const raV = value(ra, row.ra);
	if (raV === undefined)
		return { why: "the return address at CFA" + (ra.k === "off" ? (ra.v < 0 ? "" : "+") + ra.v : "") + " cannot be read" };
	// Callee-saved registers without a rule keep their value (i386 ABI:
	// EBX, ESI, EDI, EBP); EAX, ECX and EDX are lost across a call.
	const N = { [CFI_ESP]: cfa };
	for (const r of [3, 5, 6, 7])
		if (!row.regs[r] && R[r] !== undefined)
			N[r] = R[r];
	for (const r in row.regs) {
		if (Number(r) === row.ra || Number(r) === CFI_ESP)
			continue;
		const v = value(row.regs[r], Number(r));
		if (v !== undefined)
			N[r] = v >>> 0;
	}
	return { cfa, ra: raV, R: N };
}

// llvm-dwarfdump's text of a row ("CFA=ESP+8: EBP=[CFA-8], EIP=[CFA-4]"),
// for the tests and the frame tooltips.
function cfiRowText(row) {
	const sgn = (v) => (v < 0 ? "" : "+") + v;
	const cfa = row.cfa.expr ? dwExprText(row.cfa.expr) : CFI_REGS[row.cfa.reg] + (row.cfa.off ? sgn(row.cfa.off) : "");
	const regs = Object.keys(row.regs).map(Number).sort((a, b) => a - b).map((r) => {
		const x = row.regs[r];
		const v = { off: () => "[CFA" + sgn(x.v) + "]", valoff: () => "CFA" + sgn(x.v), reg: () => CFI_REGS[x.v], same: () => "same",
			undef: () => "undefined", expr: () => "[" + dwExprText(x.v) + "]", valexpr: () => dwExprText(x.v) }[x.k]();
		return CFI_REGS[r] + "=" + v;
	});
	return "CFA=" + cfa + (regs.length ? ": " + regs.join(", ") : "");
}

if (typeof module !== "undefined")
	module.exports = { dwOpEnd, dwLocation, CFI_REGS, cfiFromElf, cfiParse, cfiSlice, cfiFind, cfiEncode, cfiDecode, cfiRow, cfiRowText, cfiStep, dwExpr, dwExprText, unwind };
