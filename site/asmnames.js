"use strict";

// Names for the memory operands of x86 instructions, from the DWARF.
//
// One file for the page (every listing: the Page tab's merged hex + asm,
// the Debugger; vars.js opNamesHtml) and for site/gen-dwarf.js --operands
// (test_dwarf.py --operands checks it against gdb and llvm-dwarfdump), like
// dwarf.js and unwind.js.
//
// An operand is named when its address is known:
//   - an absolute address (`movl 0xc010f540, %eax`, `%fs:0xc12dc0e4`): the
//     variable and member path there (dwarf.js dwWhere), else the ELF data
//     symbol; a per-CPU variable through %fs is the template's name;
//   - a stack slot (`-0x1c(%ebp)`, `0xc(%esp)`) when the CFI row there
//     defines the CFA with that register: the operand is CFA + k, and a
//     parameter or local whose DWARF location there is memory at CFA + k
//     (DW_OP_fbreg with the frame base, DW_OP_breg4/5) is named; this
//     needs no register values, so it works in any listing;
//   - at a pause, the instruction at EIP: base + index * scale + disp from
//     the registers, named like the two above;
//   - a variable's address plus registers (`-0x3fef0fa0(,%edx,4)`): the
//     variable at the displacement, "+%edx*4" after it.
// Anything else gets no name (heap, stack slots of callers, a struct
// member through a pointer register: the type of a register is not known).

// dwarf.js and unwind.js: globals in the page, modules in node.
const asmLib = typeof module !== "undefined" && module.exports ? Object.assign({}, require("./dwarf.js"), require("./unwind.js")) : self;

const ASM_REGS = { eax: 0, ecx: 1, edx: 2, ebx: 3, esp: 4, ebp: 5, esi: 6, edi: 7 };
const ASM_PREFIXES = new Set(["lock", "rep", "repe", "repz", "repne", "repnz", "notrack", "bnd", "data16", "data32", "addr16", "addr32",
	"cs", "ds", "es", "fs", "gs", "ss"]);
// An operand: [%seg:][disp][(%base[,%index[,scale]])].
const ASM_MEM = /^(?:%([a-z]{2}):)?(-?(?:0x[0-9a-f]+|\d+))?(?:\(\s*(?:%([a-z0-9]+))?\s*(?:,\s*(?:%([a-z0-9]+))?\s*(?:,\s*(\d+))?)?\s*\))?$/i;
// A CFA made up for naming stack slots without registers: every address
// is CFA-relative, so any value works.
const ASM_CFA = 0x40000000;

// The memory operands of an instruction as llvm-objdump or iced-x86 print
// it (AT&T): [{ text, seg, disp, base, index, scale, lea }], disp a signed
// number, registers lowercase without "%". Immediates, registers and
// direct branch targets are not memory operands.
function asmMemOps(text) {
	const s = text.replace(/\s+#.*$/, "").replace(/\s+<[^<>]*>$/, "").trim();
	const words = s.split(/\s+/);
	let k = 0;
	while (k < words.length - 1 && ASM_PREFIXES.has(words[k].toLowerCase()))
		k++;
	const mnem = (words[k] || "").toLowerCase();
	const rest = words.slice(k + 1).join(" ");
	const ops = [];
	let depth = 0, cur = "";
	for (const ch of rest) {
		if (ch === "(")
			depth++;
		else if (ch === ")")
			depth--;
		if (ch === "," && !depth) {
			ops.push(cur.trim());
			cur = "";
		} else
			cur += ch;
	}
	if (cur.trim())
		ops.push(cur.trim());
	const branch = /^(j|call|loop|xbegin)/.test(mnem);
	const out = [];
	for (const op of ops) {
		const ind = op.startsWith("*");
		const o = ind ? op.slice(1) : op;
		if (o.startsWith("$") || /^%[a-z0-9]+$/i.test(o))
			continue;
		const m = ASM_MEM.exec(o);
		if (!m || (m[2] === undefined && !o.includes("(")))
			continue;
		if (branch && !ind && !o.includes("("))
			continue; // a direct branch target
		const disp = m[2] === undefined ? 0 : (m[2].startsWith("-") ? -parseInt(m[2].slice(1)) : parseInt(m[2]));
		out.push({ text: op, seg: m[1] ? m[1].toLowerCase() : "", disp: disp | 0, base: m[3] ? m[3].toLowerCase() : "",
			index: m[4] ? m[4].toLowerCase() : "", scale: m[5] ? Number(m[5]) : 1, lea: /^lea/.test(mnem) });
	}
	return out;
}

// sym(pa) for asmOperandNames from symbols.json's list: the smallest
// symbol with a size holding pa, else one without a size at pa (a linker
// mark such as __irf_end), or null.
function asmSymLookup(list) {
	const sized = list.filter((x) => x.size > 0).sort((a, b) => a.paddr - b.paddr);
	const marks = new Map(list.filter((x) => !x.size).map((x) => [x.paddr, x]));
	return (pa) => {
		let a = 0, b = sized.length;
		while (a < b) { const m = (a + b) >> 1; if (sized[m].paddr <= pa) a = m + 1; else b = m; }
		let best = null;
		for (let k = a - 1; k >= 0 && k >= a - 64; k--) {
			const x = sized[k];
			if (pa < x.paddr + x.size && (!best || x.size < best.size))
				best = x;
		}
		return best || marks.get(pa) || null;
	};
}

// The target of a direct branch as llvm-objdump and iced-x86 print it
// (`calll 0xc01101d0`, `jne 0xC1D86A1E`, after prefixes; a name in <>
// may follow), -1 for anything else (indirect, far, not a branch).
function asmBranchTarget(text) {
	const words = text.trim().split(/\s+/);
	let k = 0;
	while (k < words.length - 1 && ASM_PREFIXES.has(words[k].toLowerCase()))
		k++;
	if (!/^(?:j[a-z]+|call[lwq]?|loop[a-z]*|xbegin[lwq]?)$/i.test(words[k] || "") || !/^0x[0-9a-f]+$/i.test(words[k + 1] || "") ||
		(words.length > k + 2 && !words[k + 2].startsWith("<")))
		return -1;
	return parseInt(words[k + 1], 16);
}

// The name of code at physical pa as llvm-objdump writes a branch target
// (`name`, `name+0x1f`): the smallest sized symbol holding it, or a
// sizeless one exactly there (lookup from asmSymLookup); "" if none.
function asmTargetName(pa, lookup) {
	const s = lookup ? lookup(pa) : null;
	return s ? s.name + (pa > s.paddr ? "+0x" + (pa - s.paddr).toString(16) : "") : "";
}

const asmHex = (n) => (n < 0 ? "-0x" + (-n).toString(16) : "0x" + n.toString(16));

// The global at virtual va: the DWARF variable and member path, else the
// ELF symbol: { name, kind ("var", "percpu", "sym"), detail } or null.
function asmGlobal(va, env) {
	const pa = va - env.off;
	if (pa < 0 || pa >= 2 ** 32)
		return null;
	const w = env.T ? asmLib.dwWhere(env.T, pa) : null;
	if (w) {
		const v = w.var;
		const name = w.path + (w.offset ? "+" + w.offset : "");
		return { name, kind: v[5] & 2 ? "percpu" : "var", var: v[0], varOff: pa - v[1],
			detail: name + (w.note ? " (" + w.note + ")" : "") + ": " + asmLib.dwTypeName(env.T, w.type) + ", in " + v[0] + " (" +
				asmLib.dwTypeName(env.T, v[2]) + ", " + v[3] + " bytes" + (env.T.sections && env.T.sections[v[4]] ? ", " + env.T.sections[v[4]] : "") + ")" +
				(v[5] & 4 ? ", a static local" + (v[6] ? " of " + v[6] + "()" : "") : "") + (v[5] & 2 ? ", per-CPU" : "") };
	}
	const s = env.sym ? env.sym(pa) : null;
	if (s)
		return { name: s.name + (pa > s.paddr ? "+0x" + (pa - s.paddr).toString(16) : ""), kind: "sym", var: s.name, varOff: pa - s.paddr,
			detail: "ELF symbol " + s.name + (s.section ? " in " + s.section : "") + (s.size ? ", " + s.size + " bytes" : "") +
				"; no DWARF variable there" };
	return null;
}

// The parameter or local of the frames at pa whose memory location there
// holds byte ea (an address relative to ctx's registers and CFA):
// { name, kind ("param", "local"), detail } or null.
function asmLocal(ea, pa, chain, ctx, env, how) {
	if (!chain || !chain.length || !env.T)
		return null;
	const frames = asmLib.dwarfLocalFrames(chain);
	if (!frames.length)
		return null;
	const fb = frames[frames.length - 1].fb;
	asmLib.dwSetFrameBase(ctx, fb ? asmLib.dwLocAt(fb, pa) : null);
	for (const f of frames)
		for (const [v, kind] of [...f.params.map((x) => [x, "param"]), ...f.locals.map((x) => [x, "local"])]) {
			const l = asmLib.dwLocAt(v[3], pa);
			if (!l.expr)
				continue;
			const size = asmLib.dwTypeSize(env.T, v[1]) || 4;
			const r = asmLib.dwLocation(l.expr, ctx, size);
			if (r.mem === undefined || ea < r.mem || ea >= r.mem + size)
				continue;
			const p = asmLib.dwPathIn(env.T, v[1], ea - r.mem, v[0]);
			const name = p.path + (p.offset ? "+" + p.offset : "");
			return { name, kind,
				detail: name + ": " + asmLib.dwTypeName(env.T, p.type) + ", " + (kind === "param" ? "parameter" : "local variable") + " " + v[0] +
					" of " + (f.func || "?") + "()" + (f.inlined ? " (inlined)" : "") + "; its location here is " + asmLib.dwExprText(l.expr) + ", " + how };
		}
	return null;
}

// Names of the memory operands of the instruction `text` at physical pa:
// [{ op, name, kind, va?, rel?, detail }] for the named ones. env:
//   off          virtual minus physical of the kernel's data
//   T            dwarf.js DwarfTypes data, or null (only ELF symbols)
//   sym(pa)      the ELF data symbol holding pa ({ name, paddr, size,
//                section }) or null
//   scopes(pa)   dwarf.js dwarfScopesAt of the locals there, null if
//                not known (yet)
//   row(pa)      unwind.js cfiRow there, or null
//   cur          at a pause: { pa (EIP), reg(n) (DWARF numbering), cfa }
//                for the instruction at EIP and stack slots of its frame
function asmOperandNames(text, pa, env) {
	const ops = asmMemOps(text);
	const out = [];
	for (const o of ops) {
		const b = o.base ? ASM_REGS[o.base] : -1, x = o.index ? ASM_REGS[o.index] : -1;
		if (b === undefined || x === undefined)
			continue; // 16-bit or other registers
		const regs = (o.base ? "%" + o.base : "") + (o.index ? (o.base ? "+" : "") + "%" + o.index + (o.scale > 1 ? "*" + o.scale : "") : "");
		const seg = o.seg ? " (%" + o.seg + ": " + (o.seg === "fs" ? "this CPU's copy: the segment base is the CPU's per-CPU offset" :
			"plus the segment base") + ")" : "";
		let r = null;
		const cur = env.cur && env.cur.pa === pa ? env.cur : null;
		if (b < 0 && x < 0) {
			const va = o.disp >>> 0;
			r = asmGlobal(va, env);
			if (r)
				Object.assign(r, { va, detail: r.detail + seg });
		} else if (cur && (b < 0 || cur.reg(b) !== undefined) && (x < 0 || cur.reg(x) !== undefined)) {
			// The instruction at EIP: its address from the registers.
			const va = ((b >= 0 ? cur.reg(b) : 0) + (x >= 0 ? cur.reg(x) * o.scale : 0) + o.disp) >>> 0;
			const ctx = { reg: cur.reg, cfa: cur.cfa };
			const how = "the operand is " + asmHex(va) + " (" + o.text + " with this pause's registers)";
			r = !o.seg ? asmLocal(va, pa, env.scopes(pa), ctx, env, how) : null;
			if (!r && (r = asmGlobal(va, env)))
				r.detail += "; " + how + seg;
			if (r)
				r.va = va;
		}
		if (!r && (b === 4 || b === 5) && x < 0 && !o.seg) {
			// A stack slot. Without registers: the base register gets a
			// made-up value, and the CFA follows from it when the CFI row
			// here defines the CFA with that register (DW_OP_fbreg through
			// DW_OP_call_frame_cfa); DW_OP_breg of the same register needs
			// neither. At a pause, in EIP's frame, the real CFA.
			const row = env.row ? env.row(pa) : null;
			const off = row && !row.why && row.cfa && row.cfa.reg === b ? row.cfa.off : null;
			const real = off !== null && env.cur && env.cur.cfa !== undefined && env.sameFrame && env.sameFrame(pa);
			const rb = real ? (env.cur.cfa - off) >>> 0 : ASM_CFA;
			const ctx = { reg: (n) => (n === b ? rb : undefined), cfa: off !== null ? (rb + off) >>> 0 : undefined };
			const rel = off !== null ? o.disp - off : null;
			const how = "the operand is " + (rel !== null ? "CFA" + (rel < 0 ? "−" + -rel : "+" + rel) + " (CFI here: CFA=" + o.base.toUpperCase() +
				"+" + off + ")" : o.base.toUpperCase() + (o.disp < 0 ? "−" + -o.disp : "+" + o.disp));
			r = asmLocal((rb + o.disp) >>> 0, pa, env.scopes(pa), ctx, env, how);
			if (r) {
				if (rel !== null)
					r.rel = rel;
				if (real)
					r.va = (rb + o.disp) >>> 0;
			}
		}
		if (!r && o.disp >>> 0 >= env.off && env.off && (r = asmGlobal(o.disp >>> 0, env))) {
			// A variable's address plus registers.
			r = { name: r.name + "+" + regs, kind: "base", detail: "address of " + r.detail + ", plus " + regs + seg };
		}
		if (r && o.lea)
			r.name = "&" + r.name; // lea: the address, not the contents
		if (r)
			out.push({ op: o.text, ...r });
	}
	return out;
}

if (typeof module !== "undefined")
	module.exports = { asmMemOps, asmOperandNames, asmSymLookup, asmBranchTarget, asmTargetName, ASM_REGS };
