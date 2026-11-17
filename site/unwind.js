"use strict";

// Call frame information (DWARF CFI) and the backtrace at a pause.
//
// One file for three users, like dwarf.js: site/gen-dwarf.js (node, CI)
// parses a -g vmlinux's .debug_frame (or .eh_frame) and writes it as
// address shards for the shipped kernels; dwarfworker.js parses an
// imported vmlinux's; the page (dbginfo.js dbgUnwind) runs cfiRow() and
// unwind() on either, so there is one evaluator and one unwinder. DWARF
// expressions (CFA and register rules, variables' locations) are in
// dwexpr.js.
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

import { CFI_REGS, dwExpr, dwExprText } from "./dwexpr.js";

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

// The CFI of an ELF (dwarftables.js dwarfElf): .debug_frame, else .eh_frame.
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
// Written by gen-dwarf.js (cfiSlice, cfiEncode; the page only reads them):
// "MKBC" 1, bias + 1 (0: mixed), CIEs (count, then code alignment,
// data alignment zigzag, return address column, program length, program),
// FDEs (count, then start delta, length, CIE, program length, program),
// numbers unsigned LEB128.

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
// program cannot be run. With each(lo, row): every row of the FDE is
// passed to it in turn (the row object is reused: read it there).
function cfiRow(t, pa, each = null) {
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
		// Whether the row at loc is the one asked for (the program stops there).
		const stop = (to) => {
			if (isInit)
				return false;
			if (each) {
				each(loc, row);
				return false;
			}
			return to > pa;
		};
		const advance = (d) => {
			if (stop(loc + d))
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
				if (stop(to)) return false;
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
		if (each)
			each(loc, row);
	} catch (e) {
		return { why: e.message };
	}
	return { lo: loc, fde: [t.lo[i], t.lo[i] + t.len[i]], ra: c.ra, cfa: row.cfa, regs: row.regs };
}

// What the call frame information says about the frame of the function
// whose FDE covers pa: { fde: [lo, hi], max, fp, fpOff, saved, expr, rows }
//   max    the largest CFA - ESP while the CFA is ESP-based: the bytes the
//          frame takes on the stack (return address, saved registers,
//          locals, outgoing arguments)
//   fp     offset from lo of the first row whose CFA is EBP-based (a frame
//          pointer: what lies below EBP is not described), -1 if none;
//          fpOff that row's CFA - EBP
//   saved  { register number: CFA offset } of every register the
//          function saves (the return address excluded)
//   expr   a CFA computed by a DWARF expression somewhere in it
// null when no FDE covers pa, { why } when its program cannot be run.
function cfiFrameInfo(t, pa) {
	const i = cfiFind(t, pa);
	if (i < 0)
		return null;
	const lo = t.lo[i], hi = lo + t.len[i], ra = t.cies[t.cie[i]].ra;
	const out = { fde: [lo, hi], max: 0, fp: -1, fpOff: 0, saved: {}, expr: false, rows: 0 };
	const r = cfiRow(t, lo, (at, row) => {
		if (at >= hi)
			return;
		out.rows++;
		const c = row.cfa;
		if (c.expr)
			out.expr = true;
		else if (c.reg === CFI_ESP)
			out.max = Math.max(out.max, c.off);
		else if (c.reg === CFI_EBP && out.fp < 0) {
			out.fp = at - lo;
			out.fpOff = c.off;
		}
		for (const n in row.regs)
			if (row.regs[n].k === "off" && Number(n) !== ra && !(n in out.saved))
				out.saved[n] = row.regs[n].v;
	});
	return r && r.why ? { why: r.why } : out;
}

// One line about a frame (cfiFrameInfo) for the page and the tests.
function cfiFrameText(f) {
	if (!f)
		return "no call frame information";
	if (f.why)
		return "call frame information unreadable: " + f.why;
	const saved = Object.keys(f.saved).map(Number).sort((a, b) => f.saved[b] - f.saved[a])
		.map((n) => (CFI_REGS[n] || "r" + n) + " at CFA" + (f.saved[n] < 0 ? "" : "+") + f.saved[n]);
	const regs = saved.length ? "; saves " + saved.join(", ") : "; saves no registers";
	if (f.fp >= 0)
		return "frame pointer: CFA = EBP+" + f.fpOff + " from +0x" + f.fp.toString(16) + " (before that at most ESP+" + f.max +
			"); the call frame information does not say how far below EBP the frame goes" + regs + (f.expr ? "; a CFA expression somewhere" : "");
	return f.max + " bytes of stack at its deepest (CFA = ESP+" + f.max + ": return address, saved registers, locals)" + regs +
		(f.expr ? "; a CFA expression somewhere" : "");
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

export { cfiFromElf, cfiPack, cfiDecode, cfiRow, cfiFrameInfo, cfiFrameText, unwind, cfiRowText };
