"use strict";

// Jump paths: every control transfer that does not fall through to the
// next instruction (jmp, the taken side of a jcc, call, ret, loop/jecxz,
// int, iret, sysenter/sysexit, far and indirect jumps and calls, and the
// exceptions and interrupts that enter a handler).
//
// Static paths come from the disassembly (disasm.json, or the instructions
// decoded live where there is none): jumpsStatic() reads each instruction's
// text once per list, so the same code serves both. Only direct transfers
// have a target there; ret, iret, indirect jumps and calls, int and the
// exception entries get theirs at run time. Observed paths are recorded by
// the stepper (Stepper.jumpRecord in stepper.js) while it steps, with a
// count, like the executed-addresses bitmap; code v86's JIT runs records
// nothing. Here the paths and their texts (Node runs this too); drawing
// them in the listings is jumpview.js's.

import { hex, escHtml } from "./util.js";
import { disasm, symbols, stepper, api } from "./page.js";
import { asmTargetName } from "./asmnames.js";

// Kinds, as stored by the stepper (a bit per kind in a path's mask).
const JK = { jcc: 1, jmp: 2, call: 3, ret: 4, loop: 5, int: 6, iret: 7, sys: 8, ijmp: 9, icall: 10,
	ljmp: 11, lcall: 12, lret: 13, exc: 14, irq: 15, ud2: 16 };
const JK_TEXT = ["", "conditional jump", "jump", "call", "return", "loop/jecxz", "software interrupt", "iret",
	"sysenter/sysexit", "indirect jump", "indirect call", "far jump", "far call", "far return", "exception",
	"interrupt", "ud2 trap"];
// Kinds drawn as arcs in a listing's gutter (jumps, not calls or returns).
const JK_ARC = new Set([JK.jcc, JK.jmp, JK.loop, JK.ijmp]);

// Mnemonic (llvm-objdump's and iced-x86's AT&T spellings) -> [kind, has a
// direct target, falls through]. ljmp/lcall: "$seg, $offset" is direct.
const JUMP_MNEMONICS = new Map();
{
	const add = (names, kind, direct, fall) => { for (const n of names.split(" ")) JUMP_MNEMONICS.set(n, [kind, direct, fall]); };
	add("jmp jmpl jmpw jmpq", JK.jmp, true, false);
	add("ja jae jb jbe jc je jg jge jl jle jna jnae jnb jnbe jnc jne jng jnge jnl jnle jno jnp jns jnz jo jp jpe jpo js jz",
		JK.jcc, true, true);
	add("jcxz jecxz loop loope loopne loopz loopnz loopl loopw", JK.loop, true, true);
	add("call calll callw callq", JK.call, true, true);
	add("ret retl retw retq retn", JK.ret, false, false);
	add("lret lretl lretw lretq", JK.lret, false, false);
	add("iret iretl iretw iretd iretq", JK.iret, false, false);
	add("ljmp ljmpl ljmpw", JK.ljmp, true, false);
	add("lcall lcalll lcallw", JK.lcall, true, true);
	add("int int3 into int1 icebp", JK.int, false, true);
	add("ud0 ud1 ud2", JK.ud2, false, true); // Linux's WARN() resumes after it
	add("sysexit sysexitl sysret sysretl", JK.sys, false, false);
	add("sysenter syscall", JK.sys, false, true);
}
const JUMP_TEXT = /^(?:(?:lock|rep\w*|notrack|bnd|[c-gs]s|data\d+|addr\d+)\s+)*(j[a-z]+|call[lwq]?|loop[a-z]*|ret[lwqn]?|lret[lwq]?|iret[lwdq]?|ljmp[lwq]?|lcall[lwq]?|int[3o1]?|icebp|ud[012]|sys[a-z]+)(?:\s+(.*))?$/i;

// The transfer an instruction's text (llvm-objdump's or iced-x86's AT&T)
// makes: { kind, va (direct target, virtual; -1 if known only at run time),
// fall (whether the next instruction can run after it) } or null.
function jumpOf(text) {
	const m = JUMP_TEXT.exec(text);
	const spec = m && JUMP_MNEMONICS.get(m[1].toLowerCase());
	if (!spec)
		return null;
	const [kind, direct, fall] = spec;
	const arg = (m[2] || "").replace(/\s+#.*$/, "");
	if (!direct)
		return { kind, va: -1, fall };
	if (arg.startsWith("*")) // through a register or memory
		return { kind: kind === JK.jmp ? JK.ijmp : kind === JK.call ? JK.icall : kind, va: -1, fall };
	const t = kind === JK.ljmp || kind === JK.lcall ? /^\$0x[0-9a-f]+,\s*\$(0x[0-9a-f]+)/i.exec(arg) : /^(0x[0-9a-f]+)(?:\s|$)/i.exec(arg);
	return { kind, va: t ? parseInt(t[1], 16) : -1, fall };
}

// Virtual -> physical for branch targets: through the sections of
// symbols.json (vaddr, paddr, size); -1 outside them. Without sections
// (hello has them; an image without symbols does not) the address is
// taken as physical.
function jumpsToPhys(sections) {
	const list = (sections || []).filter((s) => s.size > 0);
	if (!list.length)
		return (va) => va;
	return (va) => {
		for (const s of list)
			if (va >= s.vaddr && va < s.vaddr + s.size)
				return va - s.vaddr + s.paddr;
		return -1;
	};
}

// Static paths of a list of instructions ([paddr, "hex bytes", text], in
// address order): per instruction its kind, direct target (physical, -1
// none, -2 a direct target outside the image: va kept), whether it falls
// through; and per target address the instructions that go there.
// A jcc or loop to the very next instruction is no path (its two sides
// are the same); an unconditional jmp or call there is one.
// taken: [[paddr, how], ...] (disasm.json's "taken": "data", "code",
// "extable", "array"; "entry" for a function's first instruction) - instructions
// an indirect jump or call, or an exception fixup, can reach.
function jumpsStatic(insns, toPhys, taken) {
	const n = insns.length;
	const kind = new Uint8Array(n), fall = new Uint8Array(n).fill(1);
	const target = new Float64Array(n).fill(-1), va = new Float64Array(n).fill(-1);
	const inc = new Map();
	for (let i = 0; i < n; i++) {
		const j = jumpOf(insns[i][2]);
		if (!j)
			continue;
		kind[i] = j.kind;
		fall[i] = j.fall ? 1 : 0;
		if (j.va < 0)
			continue;
		const pa = toPhys(j.va);
		const next = insns[i][0] + insns[i][1].length / 2;
		if ((j.kind === JK.jcc || j.kind === JK.loop) && pa === next)
			continue;
		va[i] = j.va;
		target[i] = pa < 0 ? -2 : pa;
		if (pa >= 0) {
			const l = inc.get(pa);
			if (l)
				l.push(i);
			else
				inc.set(pa, [i]);
		}
	}
	return { insns, kind, fall, target, va, inc, taken: new Map(taken || []) };
}

// Linux's arrays of entry stubs and the function that puts start + i ×
// stride into the IDT (arch/x86/kernel/idt.c, head_32.S, entry_32.S).
// Nothing jumps or calls to a stub after the first one and the stub
// before it ends in a jmp, so these are known by name; gen-disasm.py finds
// them in disasm.json by their stride, a live decode has only the names.
const JUMP_STUB_ARRAYS = {
	early_idt_handler_array: "idt_setup_early_handler",
	irq_entries_start: "idt_setup_apic_and_irq_gates",
	spurious_entries_start: "idt_setup_apic_and_irq_gates",
};
const JUMP_PADDING = /^(?:nop|int3)/;
const jumpGcd = (a, b) => b ? jumpGcd(b, a % b) : a;

// Marks the stubs of the arrays in syms ([{ name, paddr, size }], the
// symbols named in JUMP_STUB_ARRAYS) that S holds: inside an array, the
// first instruction after each one that does not fall through (past nop,
// int3 and zero-byte padding) is "array" in S.taken, and S.takenNote says
// which stub it is and who enters it. The stride is the gcd of the stubs'
// offsets from the array's start (with at least 3 of them).
function jumpsStubArrays(S, syms) {
	const ins = S.insns;
	S.takenNote = S.takenNote || new Map();
	for (const sym of syms) {
		const by = JUMP_STUB_ARRAYS[sym.name];
		if (!by || !(sym.size > 0))
			continue;
		let lo = 0, hi = ins.length;
		while (lo < hi) { const m = (lo + hi) >> 1; if (ins[m][0] < sym.paddr) lo = m + 1; else hi = m; }
		const starts = [];
		let after = false;
		for (let i = lo; i < ins.length && ins[i][0] < sym.paddr + sym.size; i++) {
			if (JUMP_PADDING.test(ins[i][2]) || ins[i][1].startsWith("00"))
				continue;
			if (after)
				starts.push(ins[i][0]);
			after = !S.fall[i];
		}
		const offs = starts.map((pa) => pa - sym.paddr);
		const stride = offs.length >= 3 ? offs.reduce(jumpGcd) : 0;
		for (const pa of starts) {
			if (!S.taken.has(pa))
				S.taken.set(pa, "array");
			const k = stride >= 4 ? (pa - sym.paddr) / stride : -1;
			S.takenNote.set(pa, "Reached indirectly: " + (k >= 0 ? "stub " + k + " of " + sym.name + ", entered as " + sym.name + " + " + k + " × " + stride :
				"a stub of " + sym.name + ", entered as " + sym.name + " + i × the stub size") + "; " + by + " puts these addresses in the IDT");
		}
	}
	return S;
}

// Index of the instruction starting at pa in S.insns, or -1.
function jumpsIndex(S, pa) {
	const ins = S.insns;
	let lo = 0, hi = ins.length;
	while (lo < hi) { const m = (lo + hi) >> 1; if (ins[m][0] < pa) lo = m + 1; else hi = m; }
	return lo < ins.length && ins[lo][0] === pa ? lo : -1;
}

// Kinds of a mask as text: "conditional jump", "interrupt, exception".
const jumpKindsText = (mask) => JK_TEXT.filter((_, k) => k && mask & 1 << k).join(", ");

// The paths into and out of the instruction at pa: static ones from S
// (or null) and observed ones from the stepper (st.jumpIn / st.jumpOut,
// or null), merged per other end:
//   { into: [{ at, mask, stat, count }], out: [...same], late (kinds whose
//     targets are known only at run time, for an out path list),
//     noDirect (the ⊘ mark: nothing can be shown to reach pa - no static
//     path in, its address is not taken (S.taken), an instruction ends
//     right before pa and does not fall through, and the stepper has
//     neither run pa, nor seen a path into it, nor stopped at it),
//     taken (how its address is taken, or ""), takenNote (S.takenNote:
//     how a stub of a known array is entered, or ""), inS (whether S has pa),
//     local (S.local: S holds only that many instructions decoded live, so
//     paths from code outside them are not known; 0 for disasm.json) }
// Without an instruction ending right at pa (the first one of a section or
// of a live decode, or one llvm-objdump decoded over pa from padding) what
// comes before is not known, so there is no ⊘.
// at: the other end's physical address (-2: a direct target outside the
// image, va in .va).
function jumpPaths(S, st, pa) {
	const i = S ? jumpsIndex(S, pa) : -1;
	const into = new Map(), out = new Map();
	const add = (m, at, mask, stat, count, va) => {
		const key = at === -2 ? "v" + va : at;
		let e = m.get(key);
		if (!e) {
			e = { at, mask: 0, stat: false, count: 0, va };
			m.set(key, e);
		}
		e.mask |= mask;
		e.stat = e.stat || stat;
		e.count += count;
	};
	let late = 0, noDirect = false, taken = "", takenNote = "";
	if (i >= 0) {
		for (const k of S.inc.get(pa) || [])
			add(into, S.insns[k][0], 1 << S.kind[k], true, 0);
		if (S.target[i] !== -1)
			add(out, S.target[i], 1 << S.kind[i], true, 0, S.va[i]);
		else if (S.kind[i])
			late = S.kind[i];
		const prev = i > 0 ? S.insns[i - 1] : null;
		const endsHere = prev && prev[0] + prev[1].length / 2 === pa;
		taken = S.taken ? S.taken.get(pa) || "" : "";
		takenNote = taken && S.takenNote ? S.takenNote.get(pa) || "" : "";
		noDirect = !into.size && endsHere && !S.fall[i - 1] && !taken;
	}
	if (st && st.jumpIn) {
		for (const e of st.jumpIn.get(pa) || [])
			add(into, e[0], e[2], false, e[3]);
		for (const e of st.jumpOut.get(pa) || [])
			add(out, e[1], e[2], false, e[3]);
	}
	if (noDirect && st && (into.size || st.executed && st.executed(pa) || st.nextPhys && st.nextPhys() === pa))
		noDirect = false;
	const sort = (m) => [...m.values()].sort((a, b) => b.count - a.count || a.at - b.at);
	return { into: sort(into), out: sort(out), late, noDirect, taken, takenNote, inS: i >= 0, local: S && S.local || 0 };
}

// ---- the page: counts per instruction, arcs, the list of paths ----------
//
// Browser only (page.js: disasm, symbols, stepper, api.show.jumpTo, ...).

// Kinds whose targets only the run shows: "→?" until one is observed.
const JK_LATE = new Set([JK.ret, JK.iret, JK.ijmp, JK.icall, JK.lret, JK.sys, JK.ljmp, JK.lcall]);
const JK_ARC_MASK = [...JK_ARC].reduce((m, k) => m | 1 << k, 0);
const SVG_NS = "http://www.w3.org/2000/svg";

let jumpsKernel = null; // [disasm, symbols, static paths of disasm.insns]
let jumpHit = -1;       // the instruction a path list's "go there" opened
let jumpHitScroll = false; // the Page listing's next drawing scrolls to it
// Other modules set them through these (an imported binding is read-only).
const setJumpHit = (pa) => { jumpHit = pa; };
const setJumpHitScroll = (on) => { jumpHitScroll = on; };

// Static paths of the build's disassembly (made once per kernel, ~20-40 ms
// for the -O0 minified Linux's 48,000 instructions), or of another list
// (instructions decoded live: only the paths inside that list).
// A live decode does not know who calls a function: the first instruction
// of each function symbol in it counts as reached ("entry").
function jumpsFor(list) {
	const toPhys = () => jumpsToPhys(symbols && symbols.sections);
	if (disasm && list === disasm.insns) {
		if (!jumpsKernel || jumpsKernel[0] !== disasm || jumpsKernel[1] !== symbols)
			jumpsKernel = [disasm, symbols, jumpsStubArrays(jumpsStatic(disasm.insns, toPhys(), disasm.taken), jumpStubSyms())];
		return jumpsKernel[2];
	}
	const entries = [], look = symbols ? api.symbols.kernelSymLookup() : null;
	if (look)
		for (const [pa] of list) {
			const s = look(pa);
			if (s && s.paddr === pa && /^[Tt]$/.test(s.type || "T"))
				entries.push([pa, "entry"]);
		}
	const S = jumpsStubArrays(jumpsStatic(list, toPhys(), entries), jumpStubSyms());
	S.local = list.length;
	return S;
}

// The symbols of the kernel's stub arrays (JUMP_STUB_ARRAYS), found once per symbol table.
let jumpStubSymsOf = [null, []];
function jumpStubSyms() {
	if (jumpStubSymsOf[0] !== symbols)
		jumpStubSymsOf = [symbols, symbols ? symbols.symbols.filter((s) => Object.hasOwn(JUMP_STUB_ARRAYS, s.name)) : []];
	return jumpStubSymsOf[1];
}

// What "no direct path" rests on: the whole disassembly, or only the
// instructions decoded live (then jumps from other code are not known).
function jumpNoneScope(P) {
	return P.local ? "from the " + P.local.toLocaleString("en") + " instruction" + (P.local > 1 ? "s" : "") + " decoded live" : "";
}

const JUMP_NONE_OTHER = "jumps and calls from other code are not known";

// How an instruction without a direct path in can still be reached (P.taken).
const JUMP_TAKEN_TEXT = {
	data: "Reached indirectly: its address is in the kernel's data (a function pointer, a switch's jump table or a table of handlers)",
	code: "Reached indirectly: its address is in the code (an immediate such as movl $fn or pushl $fn, used as a pointer)",
	extable: "Reached from the exception table: a fault at the instruction it covers continues here",
	array: "Reached indirectly: one of an array of entry stubs whose start is in the kernel's data or code, reached as start + n × stride",
	entry: "A function's entry: its callers are outside the instructions decoded live",
};

// The text of P.taken: the stub of a known array, or how its address is taken.
const jumpTakenText = (P) => P.takenNote || JUMP_TAKEN_TEXT[P.taken];

const jumpSeen = (list) => list.reduce((n, e) => n + e.count, 0);

// Why observed paths may be missing: nothing stepped, or v86's JIT ran.
function jumpsJitNote() {
	if (!stepper)
		return "nothing has run yet";
	const where = [stepper.jitBeforeEntry ? "the fast-forward to the kernel entry" : "", stepper.jitAfterEntry ? "full speed or a Run to" : ""]
		.filter(Boolean).join(" and ");
	return where ? "v86's JIT ran " + where + " and records no paths, so paths taken there are missing" : "";
}

function jumpName(at, va) {
	if (at === -2)
		return hex(va) + " (outside the kernel image)";
	const name = typeof api.symbols.kernelSymLookup === "function" ? asmTargetName(at, api.symbols.kernelSymLookup()) : "";
	return name ? name + " (" + hex(at) + ")" : hex(at);
}

function jumpCountText(e) {
	return e.stat ? (e.count ? "static, taken " + e.count.toLocaleString("en") + "×" : "static, not taken while stepping") :
		"observed " + e.count.toLocaleString("en") + "×";
}

// What ⊘ means at an instruction (P.noDirect): the popover, the list's
// note and the Debugger's paths at EIP. The first line says what the mark
// means, then what that rests on (the whole disassembly or the
// instructions decoded live) and what the run has shown.
function jumpNoneText(P) {
	const jit = jumpsJitNote();
	const lines = P.local ? [
		"No path leads here: none of the instructions decoded live jumps or calls to this one, and the one before it does not fall through.",
		"Only " + P.local.toLocaleString("en") + " instruction" + (P.local > 1 ? "s were" : " was") + " decoded live (no disasm.json), so " +
			JUMP_NONE_OTHER + ".",
	] : [
		"No path leads here: nothing jumps or calls to this instruction, and the one before it does not fall through.",
		"Checked in the whole disassembly. Its address is not stored anywhere in the kernel either (no function pointer, jump table or exception fixup).",
	];
	lines.push(!stepper ? "Nothing has run yet." :
		"It has not run while stepping, and no jump into it has been seen" +
		(jit && jit.startsWith("v86") ? "; " + jit.replace("paths taken there are missing", "code run there is not known") : "") + ".");
	if (!P.local)
		lines.push("Usually alignment padding after a ret or jmp, or a function nothing calls.");
	return lines.join("\n");
}

// The count buttons after an instruction line (and the mark of an
// instruction no path is known to reach); P from jumpPaths. Their texts
// are in data-tip: the page's popover (initTips) shows them on a tap of
// ⊘, and on hover, keyboard focus or a long press of a button.
function jumpBadgesHtml(P, pa) {
	let h = "";
	if (P.noDirect)
		h += ' <span class="jnone tipmark" tabindex="0" role="button" aria-label="No path leads here" data-pa="' + pa + '" data-tip="' +
			escHtml(jumpNoneText(P)) + '">⊘</span>';
	const btn = (dir, n, seen, title) => ' <button type="button" class="jc" data-pa="' + pa + '" data-dir="' + dir + '" data-tip="' + escHtml(title) + '">' +
		(dir === "in" ? "←" : "→") + n + (seen ? "·" + seen.toLocaleString("en") + "×" : "") + "</button>";
	const summary = (list, dir) => {
		const st = list.filter((e) => e.stat).length, seen = jumpSeen(list);
		return list.length + " path" + (list.length > 1 ? "s" : "") + " " + dir + " (" + st + " static" +
			(seen ? ", " + seen.toLocaleString("en") + " transfers observed" : "") + "): tap to list them";
	};
	if (P.into.length)
		h += btn("in", P.into.length, jumpSeen(P.into), summary(P.into, "into this instruction"));
	if (P.out.length)
		h += btn("out", P.out.length, jumpSeen(P.out), summary(P.out, "out of this instruction"));
	else if (JK_LATE.has(P.late))
		h += btn("out", "?", 0, "a " + JK_TEXT[P.late] + ": the target is known only at run time, none observed yet: tap for more");
	return h;
}

export { JK, JK_TEXT, JK_ARC, JUMP_TEXT, jumpOf, jumpsToPhys, jumpsStatic, jumpsStubArrays, jumpsIndex,
	jumpKindsText, jumpPaths, JK_LATE, JK_ARC_MASK, SVG_NS, jumpHit, jumpHitScroll, setJumpHit, setJumpHitScroll,
	jumpsFor, jumpNoneScope, JUMP_NONE_OTHER, jumpTakenText, jumpsJitNote, jumpName, jumpCountText, jumpNoneText,
	jumpBadgesHtml };
