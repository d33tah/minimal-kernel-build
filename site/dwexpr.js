"use strict";

// DWARF expressions (unwind.js, dwarflocals.js): their text, their value
// on the stopped machine, and the locations of variables.

const CFI_REGS = ["EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI", "EIP"];

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
	0xa6: "DW_OP_deref_type", 0xa8: "DW_OP_convert", 0xa9: "DW_OP_reinterpret", 0xf0: "DW_OP_GNU_uninit", 0xf2: "DW_OP_GNU_implicit_pointer",
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
			out.push("DW_OP_fbreg " + sgn(sleb()));
		else if (op === 0x11)
			out.push("DW_OP_consts " + sgn(sleb()));
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
			case 0x96: case 0xf0: break; // nop; DW_OP_GNU_uninit (only a hint that the value may not be set yet)
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

// The frame base (DW_AT_frame_base) of a function from its location l
// at the pc (dwarflocals.js dwLocAt), for DW_OP_fbreg: sets ctx.frameBase, or
// ctx.fbWhy when it cannot be had. GCC's is DW_OP_call_frame_cfa (the
// CFA), clang's a register (DW_OP_reg4 ESP: its value).
function dwSetFrameBase(ctx, l) {
	delete ctx.frameBase;
	ctx.fbWhy = "";
	if (!l || !l.expr)
		return;
	const e = l.expr;
	const v = e.length === 1 && e[0] >= 0x50 && e[0] <= 0x6f ?
		(ctx.reg(e[0] - 0x50) === undefined ? { why: CFI_REGS[e[0] - 0x50] + " is not known in this frame" } : { value: ctx.reg(e[0] - 0x50) }) :
		dwExpr(e, ctx);
	if (!v.why)
		ctx.frameBase = v.value;
	ctx.fbWhy = v.why ? "frame base (" + dwExprText(e) + "): " + v.why : "";
}

export { CFI_REGS, dwOpEnd, dwExprText, dwExpr, dwLocation, dwSetFrameBase };
