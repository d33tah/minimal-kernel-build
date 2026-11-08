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
// nothing.

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
function jumpsStatic(insns, toPhys) {
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
	return { insns, kind, fall, target, va, inc };
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
//     noDirect (no static path in and the instruction before does not fall
//     through into it, or there is none), inS (whether S has pa) }
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
	let late = 0, noDirect = false;
	if (i >= 0) {
		for (const k of S.inc.get(pa) || [])
			add(into, S.insns[k][0], 1 << S.kind[k], true, 0);
		if (S.target[i] !== -1)
			add(out, S.target[i], 1 << S.kind[i], true, 0, S.va[i]);
		else if (S.kind[i])
			late = S.kind[i];
		const prev = i > 0 ? S.insns[i - 1] : null;
		const fallsIn = prev && prev[0] + prev[1].length / 2 === pa && S.fall[i - 1];
		noDirect = !into.size && !fallsIn;
	}
	if (st && st.jumpIn) {
		for (const e of st.jumpIn.get(pa) || [])
			add(into, e[0], e[2], false, e[3]);
		for (const e of st.jumpOut.get(pa) || [])
			add(out, e[1], e[2], false, e[3]);
	}
	const sort = (m) => [...m.values()].sort((a, b) => b.count - a.count || a.at - b.at);
	return { into: sort(into), out: sort(out), late, noDirect, inS: i >= 0 };
}

// ---- the page: counts per instruction, arcs, the list of paths ----------
//
// Browser only (app.js globals: disasm, symbols, stepper, jumpTo, ...).

// Kinds whose targets only the run shows: "→?" until one is observed.
const JK_LATE = new Set([JK.ret, JK.iret, JK.ijmp, JK.icall, JK.lret, JK.sys, JK.ljmp, JK.lcall]);
const JK_ARC_MASK = [...JK_ARC].reduce((m, k) => m | 1 << k, 0);
const SVG_NS = "http://www.w3.org/2000/svg";

let jumpsKernel = null; // [disasm, symbols, static paths of disasm.insns]
let jumpHit = -1;       // the instruction a path list's "go there" opened
let jumpHitScroll = false; // the Page listing's next drawing scrolls to it

// Static paths of the build's disassembly (made once per kernel, ~20-40 ms
// for the -O0 minified Linux's 48,000 instructions), or of another list
// (instructions decoded live: only the paths inside that list).
function jumpsFor(list) {
	const toPhys = () => jumpsToPhys(symbols && symbols.sections);
	if (disasm && list === disasm.insns) {
		if (!jumpsKernel || jumpsKernel[0] !== disasm || jumpsKernel[1] !== symbols)
			jumpsKernel = [disasm, symbols, jumpsStatic(disasm.insns, toPhys())];
		return jumpsKernel[2];
	}
	return jumpsStatic(list, toPhys());
}

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
	const name = typeof kernelSymLookup === "function" ? asmTargetName(at, kernelSymLookup()) : "";
	return name ? name + " (" + hex(at) + ")" : hex(at);
}

function jumpCountText(e) {
	return e.stat ? (e.count ? "static, taken " + e.count.toLocaleString("en") + "×" : "static, not taken while stepping") :
		"observed " + e.count.toLocaleString("en") + "×";
}

// The count buttons after an instruction line (and the mark of an
// instruction no direct path reaches); P from jumpPaths.
function jumpBadgesHtml(P, pa) {
	let h = "";
	if (P.noDirect)
		h += ' <span class="jnone" title="No direct path into this instruction: no jump or call in the disassembly goes here and the instruction before it does not fall through (a jmp, ret or iret, or none)' +
			(P.into.length ? "; reached at run time through " + P.into.length + " path" + (P.into.length > 1 ? "s" : "") + " (indirect, interrupt or exception)" : "") +
			'">⊘</span>';
	const btn = (dir, n, seen, title) => ' <button type="button" class="jc" data-pa="' + pa + '" data-dir="' + dir + '" title="' + escHtml(title) + '">' +
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

// The arcs of a listing: jumps (JK_ARC) between instructions it shows.
function jumpArcs(paths) {
	const arcs = [];
	for (const [pa, P] of paths)
		for (const e of P.out)
			if (e.mask & JK_ARC_MASK && e.at !== pa && paths.has(e.at))
				arcs.push({ from: pa, to: e.at, taken: e.count > 0, stat: e.stat });
	return arcs;
}

// Draw arcs in the gutter left of box's lines, like objdump
// --visualize-jumps: the shorter an arc the nearer the code. Columns go
// by address, so they do not depend on how the lines wrap; positions are
// measured from the lines (an SVG over the left padding).
function drawJumpArcs(box, arcs) {
	box._jarcs = arcs;
	const narrow = box.clientWidth < 520;
	const maxCols = narrow ? 4 : 10, COL = narrow ? 5 : 6;
	const iv = arcs.map((a) => ({ a, lo: Math.min(a.from, a.to), hi: Math.max(a.from, a.to), col: -1 }))
		.sort((x, y) => (x.hi - x.lo) - (y.hi - y.lo) || x.lo - y.lo);
	const cols = [];
	let dropped = 0;
	for (const v of iv) {
		let c = 0;
		while (c < maxCols && (cols[c] || []).some(([l, h]) => v.lo <= h && l <= v.hi))
			c++;
		if (c === maxCols) {
			dropped++;
			continue;
		}
		(cols[c] = cols[c] || []).push([v.lo, v.hi]);
		v.col = c;
	}
	const gutter = cols.length ? cols.length * COL + 9 : 0;
	box.style.setProperty("--jgutter", gutter + "px");
	box.dataset.arcs = arcs.length - dropped;
	box.dataset.arcsDropped = dropped;
	let svg = box.querySelector(":scope > svg.jarcs");
	if (!cols.length) {
		if (svg)
			svg.remove();
		return dropped;
	}
	if (!svg) {
		svg = document.createElementNS(SVG_NS, "svg");
		svg.setAttribute("class", "jarcs");
		svg.setAttribute("aria-hidden", "true");
		box.prepend(svg);
	}
	const pad = parseFloat(getComputedStyle(box).paddingLeft) || gutter;
	const br = box.getBoundingClientRect();
	const yOf = new Map();
	const y = (pa) => {
		if (!yOf.has(pa)) {
			const el = box.querySelector('.ins[data-addr="' + pa + '"]');
			const r = el ? el.getClientRects()[0] : null;
			yOf.set(pa, r ? r.top - br.top + box.scrollTop + r.height / 2 : null);
		}
		return yOf.get(pa);
	};
	const xR = pad - 3;
	let out = "";
	for (const v of iv) {
		if (v.col < 0)
			continue;
		const y1 = y(v.a.from), y2 = y(v.a.to);
		if (y1 === null || y2 === null)
			continue;
		const x = xR - 6 - v.col * COL, d = y2 > y1 ? 1 : -1, r = Math.min(3, Math.abs(y2 - y1) / 2);
		const cls = "jarc" + (v.a.taken ? " taken" : "") + (v.a.stat ? "" : " dyn");
		out += '<path class="' + cls + '" data-from="' + v.a.from + '" data-to="' + v.a.to + '" d="M' + xR + " " + y1 + "H" + (x + r) +
			"Q" + x + " " + y1 + " " + x + " " + (y1 + d * r) + "V" + (y2 - d * r) + "Q" + x + " " + y2 + " " + (x + r) + " " + y2 + "H" + (xR - 3) + '"/>' +
			'<path class="jarrow' + (v.a.taken ? " taken" : "") + '" d="M' + xR + " " + y2 + "l-4 -3v6z\"/>";
	}
	svg.setAttribute("width", pad);
	svg.setAttribute("height", box.scrollHeight);
	svg.innerHTML = out;
	return dropped;
}

// After a listing's HTML is in box: its paths for the list, its arcs.
function jumpsDrawn(box, S, paths) {
	box._jS = S;
	return drawJumpArcs(box, jumpArcs(paths));
}

function jumpPopClose(refocus) {
	const pop = $("jump_pop");
	if (pop.hidden)
		return;
	pop.hidden = true;
	if (refocus && pop._from && pop._from.isConnected)
		pop._from.focus();
}

// The list of paths into or out of an instruction, next to the button.
function jumpPopShow(btn) {
	const pop = $("jump_pop");
	const pa = Number(btn.dataset.pa), dir = btn.dataset.dir;
	const box = btn.closest("[data-jbox]");
	const S = box ? box._jS : null;
	const P = jumpPaths(S, stepper, pa);
	const list = dir === "in" ? P.into : P.out;
	pop._from = btn;
	pop.dataset.pa = pa;
	pop.dataset.dir = dir;
	pop.replaceChildren();
	const head = document.createElement("div");
	head.className = "jhead";
	const title = document.createElement("b");
	title.textContent = (dir === "in" ? "Paths into " : "Paths out of ") + jumpName(pa) + ": " + list.length;
	const close = document.createElement("button");
	close.type = "button";
	close.className = "jclose";
	close.textContent = "Close";
	close.addEventListener("click", () => jumpPopClose(true));
	head.append(title, " ", close);
	pop.append(head);
	const ul = document.createElement("ul");
	for (const e of list) {
		const li = document.createElement("li");
		li.dataset.at = e.at;
		li.dataset.count = e.count;
		li.dataset.stat = e.stat ? 1 : 0;
		li.dataset.kinds = jumpKindsText(e.mask);
		const go = document.createElement("button");
		go.type = "button";
		go.className = "symlink jgo";
		go.textContent = jumpName(e.at, e.va);
		if (e.at >= 0)
			go.addEventListener("click", () => { jumpPopClose(false); jumpGo(e.at); });
		else
			go.disabled = true;
		li.append(jumpKindsText(e.mask) + (dir === "in" ? " from " : " to "), go);
		if (e.at >= 0 && typeof dbgHas === "function" && dbgHas()) {
			const r = dbgFrames(e.at);
			if (r.state === "ok" && r.frames[0].file && r.frames[0].line)
				li.append(" ", srcLinkEl(r.frames[0].file, r.frames[0].line, "", e.at));
		}
		li.append(", " + jumpCountText(e));
		ul.append(li);
	}
	pop.append(ul);
	const notes = [];
	if (!list.length && dir === "out" && P.late)
		notes.push("A " + JK_TEXT[P.late] + ": where it goes is known only at run time, and no transfer from here has been observed yet.");
	else if (dir === "out" && JK_LATE.has(P.late))
		notes.push("A " + JK_TEXT[P.late] + ": the targets listed are the ones observed while stepping.");
	if (dir === "in" && P.noDirect)
		notes.push("No direct path: no jump or call in the disassembly goes here and the instruction before does not fall through.");
	if (S && box && box.dataset.jlocal)
		notes.push("Static paths only from the " + box.dataset.jlocal + " (no disasm.json): jumps from other code are not known.");
	const jit = jumpsJitNote();
	if (jit)
		notes.push("Observed paths: " + jit + ".");
	for (const t of notes) {
		const p = document.createElement("p");
		p.className = "note";
		p.textContent = t;
		pop.append(p);
	}
	pop.hidden = false;
	const r = btn.getBoundingClientRect();
	const w = Math.min(420, window.innerWidth - 16);
	pop.style.width = w + "px";
	const x = Math.min(Math.max(8, r.left), window.innerWidth - w - 8);
	let y = r.bottom + 4;
	pop.style.left = x + "px";
	pop.style.top = y + "px";
	const h = pop.offsetHeight;
	if (y + h > window.innerHeight - 8)
		pop.style.top = Math.max(8, r.top - h - 4) + "px";
	(pop.querySelector("button.jgo:not(:disabled)") || close).focus({ preventScroll: true });
}

// "go there": the Page tab at pa, its line marked.
function jumpGo(pa) {
	jumpHit = pa;
	jumpHitScroll = true;
	jumpTo(pa);
}

// The Debugger's summary of EIP's own paths.
function renderEipJumps(S, pa) {
	const el = $("eip_jumps");
	el._jS = S;
	el.replaceChildren();
	if (pa < 0 || !S) {
		el.textContent = pa < 0 ? "" : "Paths at EIP: no disassembly here.";
		return;
	}
	const P = jumpPaths(S, stepper, pa);
	const part = (dir, list) => {
		const span = document.createElement("span");
		span.className = "jpart";
		span.dataset.dir = dir;
		span.dataset.n = list.length;
		span.append((dir === "in" ? "Paths into EIP: " : "Paths out: ") + list.length);
		if (list.length || (dir === "out" && P.late)) {
			const b = document.createElement("button");
			b.type = "button";
			b.className = "jc";
			b.dataset.pa = pa;
			b.dataset.dir = dir;
			b.textContent = "list";
			span.append(" ", b);
		}
		const show = list.slice(0, 3).map((e) => jumpKindsText(e.mask) + (dir === "in" ? " from " : " to ") + jumpName(e.at, e.va) + " (" + jumpCountText(e) + ")");
		if (show.length)
			span.append(": " + show.join("; ") + (list.length > 3 ? "; " + (list.length - 3) + " more" : ""));
		else if (dir === "out" && JK_LATE.has(P.late))
			span.append(" (a " + JK_TEXT[P.late] + ": known at run time)");
		if (dir === "in" && P.noDirect)
			span.append(" (no direct path: nothing jumps or calls here and the instruction before does not fall through)");
		return span;
	};
	el.append(part("in", P.into), document.createElement("br"), part("out", P.out));
	const jit = jumpsJitNote();
	if (jit) {
		el.append(document.createElement("br"), "Observed paths: " + jit + ".");
	}
	el.dataset.pa = pa;
	el.dataset.into = P.into.length;
	el.dataset.out = P.out.length;
}

function initJumps() {
	document.addEventListener("click", (ev) => {
		const b = ev.target.closest ? ev.target.closest("button.jc") : null;
		if (b) {
			ev.stopPropagation();
			ev.preventDefault();
			const pop = $("jump_pop");
			if (!pop.hidden && pop._from === b)
				jumpPopClose(false);
			else
				jumpPopShow(b);
			return;
		}
		const pop = $("jump_pop");
		if (!pop.hidden && !pop.contains(ev.target))
			jumpPopClose(false);
	}, true);
	// v86 takes every key on the window (preventDefault): Enter and Space
	// click the buttons here first, Escape closes the list.
	document.addEventListener("keydown", (ev) => {
		const b = ev.target.closest ? ev.target.closest("button.jc, #jump_pop button") : null;
		if (b && (ev.key === "Enter" || ev.key === " ")) {
			ev.preventDefault();
			ev.stopPropagation();
			b.click();
		} else if (ev.key === "Escape" && !$("jump_pop").hidden) {
			ev.stopPropagation();
			jumpPopClose(true);
		}
	}, true);
	// Lines wrap differently at another width: draw the arcs again.
	if (typeof ResizeObserver === "function") {
		const ro = new ResizeObserver((entries) => {
			for (const en of entries)
				if (en.target._jarcs)
					drawJumpArcs(en.target, en.target._jarcs);
		});
		for (const id of ["listing", "context"])
			ro.observe($(id));
	}
}

if (typeof module !== "undefined")
	module.exports = { JK, JK_TEXT, JK_ARC, jumpOf, jumpsToPhys, jumpsStatic, jumpsIndex, jumpPaths, jumpKindsText };
