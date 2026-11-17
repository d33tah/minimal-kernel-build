"use strict";

// Following EIP: the listing as the CPU runs, jumps to an address or a
// function, the symbols and call stack panels.

import { realNow } from "./clock.js";
import { $, PAGE, hex, num } from "./util.js";
import { stepper, runState, emuRam, flashes, setFlashes, selectedPage, tab, setSelectedPage, symbols,
	openFnWant, setOpenFnWant, disasm, provide } from "./page.js";
import { setJumpHit, setJumpHitScroll } from "./jumps.js";
import { cfiRowText } from "./unwind.js";
import { dbgOnChange, dbgFrames, dbgHas, dbgTitle, dbgUnwind, dbgCfiInfo } from "./dbginfo.js";
import { openSource, renderSourceRows, srcLinkEl } from "./source.js";
import { pageWalk, phys32 } from "./cpustate.js";
import { renderTaskRow } from "./vars.js";
import { renderFnRows } from "./fnfacts.js";
import { bmFnNames, bmStarEl, bmAddButton, bmDraftFn } from "./bookmarks.js";
import { scan } from "./scan.js";
import { drawGrid, symDesc } from "./memview.js";
import { inspect, listingFollow, setNavScrollWant, listingLineAt, scrollListingTo, sheetBand,
	insnIndex } from "./listing.js";
import { fastStepping, updateDebugUi, renderContext } from "./debugui.js";
import { writeHash, showTab } from "./tabs.jsx";
import { navJump, NAV_SCROLL_MS, navAt, navDepth, setNavDepth } from "./nav.js";

// Live view while stepping: grid flashes, asm view follows EIP. Drawn
// only when an instruction has executed since the last drawing, and after
// a pause at least as long as that drawing took (VIEW_GAP_MS or more): a
// listing drawn on every animation frame kept a phone's main thread busy
// all the time. The step credit counts real time, so a slow drawing
// shows fewer of the instructions, never slows the run. fast: stepping as
// fast as possible (Run to, counting): every FAST_VIEW_GAP_MS, or after
// twice the drawing's time, so the drawing takes a third of the time at
// most.
const VIEW_GAP_MS = 200, FAST_VIEW_GAP_MS = 300;
let viewStepper = null, viewSteps = -1, viewAt = 0, viewCost = 0;
let lastFrameScan = 0;
function frameUpdate(fast = false) {
	if (stepper === viewStepper && stepper.steps === viewSteps)
		return;
	const t0 = realNow();
	if (stepper === viewStepper && t0 - viewAt < (fast ? Math.max(FAST_VIEW_GAP_MS, 2 * viewCost) : Math.max(VIEW_GAP_MS, viewCost)))
		return;
	viewStepper = stepper;
	viewSteps = stepper.steps;
	frameDraw(t0);
	viewAt = realNow();
	viewCost = viewAt - t0;
}

// The listing is out of date (DWARF arrived): in slow motion it is drawn
// whole at the next frameUpdate (several changes in a row, one drawing),
// otherwise now.
function listingStale() {
	if (runState !== "stepped" || fastStepping()) {
		inspect();
		return;
	}
	$("listing")._drawn = null;
	viewSteps = -1;
}

function frameDraw(now) {
	if (now - lastFrameScan >= (emuRam > 32 * 1024 * 1024 ? 2000 : emuRam > 4 * 1024 * 1024 ? 250 : 0)) {
		lastFrameScan = now;
		scan(false);
		const n = Number($("footprint").dataset.changed || 0);
		$("pages").dataset.flashed = n;
		if (n) {
			setFlashes(flashes + 1);
			$("pages").dataset.flashes = flashes;
		}
	}
	followEip(true);
}

function afterStop() {
	// Baseline for the grid: pages written after this flash.
	scan(false);
	updateDebugUi();
	followEip();
}

// While paused or stepping, the Page tab shows the page holding EIP.
// moving: in slow motion (listingFollow may only move the mark).
function followEip(moving = false) {
	if (!stepper || runState === "jit")
		return;
	const phys = stepper.nextPhys();
	if (phys >= 0 && phys < emuRam) {
		const p = Math.floor(phys / PAGE);
		if (p !== selectedPage) {
			setSelectedPage(p);
			writeHash();
			if (tab === "memory")
				drawGrid();
		}
		if (tab === "page" && !(moving && listingFollow()))
			inspect();
	}
	renderContext();
}

// Code symbols for the stepper's symbol history: sized, in .text*, one per
// start address, sorted.
function codeSymbols() {
	if (!symbols)
		return [];
	// Several names often share an address (irq_entries_start and the section
	// marker __irqentry_text_start): name the code by its function, not by a
	// linker marker, so markers sort after real symbols at the same address.
	const marker = (n) => /^__\w*_(start|end)$|^_(s|e)?(text|data|inittext)$/.test(n) ? 1 : 0;
	const list = symbols.symbols.filter((s) => s.size > 0 && /^\.text|^\.init\.text|^\.head\.text/.test(s.section))
		.slice().sort((x, y) => x.paddr - y.paddr || marker(x.name) - marker(y.name));
	const seen = new Set();
	return list.filter((s) => !seen.has(s.paddr) && seen.add(s.paddr));
}

// Open the Page tab at physical address a and scroll its line into view.
function jumpTo(a) {
	if (a < 0 || a >= emuRam)
		return;
	navJump(() => {
		setSelectedPage(Math.floor(a / PAGE));
		drawGrid();
		showTab("page");
		// The listing may be drawn again soon (the Source panel opening
		// narrows it): it keeps this line in view, not EIP's.
		setNavScrollWant({ page: selectedPage, addr: a, until: realNow() + NAV_SCROLL_MS });
		const best = listingLineAt(a);
		if (best)
			scrollListingTo(best);
	});
}

// Open a function in the code browser (the Calls tab's double click, G1):
// the Page tab at its first instruction (the symbol's header line is just
// above it) and the Source panel at the function's line, the frame of its
// inline chain that is the function itself (else the outermost). One
// history entry. While its DWARF shard loads, the panel opens when it
// arrives, if the user is still on that view (openFnWant, page.js).
function openFnSource(pa, name) {
	const r = dbgFrames(pa);
	if (r.state !== "ok")
		return r.state;
	let k = r.frames.findIndex((f) => f.func === name && f.file && f.line);
	if (k < 0)
		k = r.frames.length - 1;
	openSource(pa, k);
	return "ok";
}

// On a phone the Source panel is a sheet over the lower part of the
// screen: scroll the window so the function's header line and first
// instruction sit in the strip between the top bar and the sheet.
function fnHeaderInView(pa) {
	const sp = $("src_panel");
	if (sp.hidden || tab !== "page")
		return;
	const pr = sp.getBoundingClientRect();
	if (pr.left > 0 || pr.top <= 0)
		return;
	// The listing's top under the top bar (as far as the page scrolls),
	// then the line a third of the way down the strip.
	const top = document.querySelector(".top").getBoundingClientRect().bottom;
	window.scrollBy(0, $("listing").getBoundingClientRect().top - top - 4);
	const line = listingLineAt(pa);
	if (line)
		scrollListingTo(line);
	// The symbol's header line (two rows when it wraps) stays in view too.
	const hdr = $("listing").querySelector('.lbl[data-addr="' + pa + '"]'), band = sheetBand($("listing").getBoundingClientRect());
	if (hdr && band && hdr.getBoundingClientRect().top < band[0])
		$("listing").scrollTop -= band[0] - hdr.getBoundingClientRect().top + 4;
}

// outline: the first instruction gets the jump outline (a bookmark, K1).
function openFunction(pa, name, outline = false) {
	setOpenFnWant(null);
	navJump(() => {
		if (outline) {
			setJumpHit(pa);
			setJumpHitScroll(true);
		}
		jumpTo(pa);
		if (openFnSource(pa, name) === "loading")
			setOpenFnWant({ pa, name, page: selectedPage, until: realNow() + 30000 });
		fnHeaderInView(pa);
	});
	if (openFnWant)
		openFnWant.at = navAt;
}

dbgOnChange(() => {
	const w = openFnWant;
	if (!w)
		return;
	if (tab !== "page" || selectedPage !== w.page || navAt !== w.at || realNow() > w.until) {
		setOpenFnWant(null);
		return;
	}
	// Part of the jump that asked for it: no entry of its own.
	setNavDepth(navDepth + 1);
	try {
		if (openFnSource(w.pa, w.name) !== "loading") {
			setOpenFnWant(null);
			// The listing is drawn again now (its C lines, the panel
			// narrows it): it keeps the function's line in view, not EIP's.
			setNavScrollWant({ page: w.page, addr: w.pa, until: realNow() + NAV_SCROLL_MS });
			const line = listingLineAt(w.pa);
			if (line)
				scrollListingTo(line);
			fnHeaderInView(w.pa);
		}
	} finally {
		setNavDepth(navDepth - 1);
	}
});

function symButton(text, addr, title) {
	const b = document.createElement("button");
	b.type = "button";
	b.className = "symlink";
	b.textContent = text;
	if (title)
		b.title = title;
	if (addr >= 0)
		b.addEventListener("click", () => jumpTo(addr));
	else
		b.disabled = true;
	return b;
}

// Debugger tab: current and previous symbol, the last few symbols, and
// the disassembly around EIP.
function renderSymbols(phys) {
	const box = $("sym_info");
	box.textContent = "";
	const st = stepper;
	const cur = phys >= 0 ? st.symbolOf(phys) : -1;
	const row = (label, ...nodes) => {
		const d = document.createElement("div");
		const b = document.createElement("b");
		b.textContent = label;
		d.append(b, " ", ...nodes);
		box.append(d);
		return d;
	};
	const c = row("current");
	c.id = "cur_sym";
	c.title = "The symbol (symbols.json or the imported map) that holds EIP: its address range and EIP's offset in it";
	if (cur >= 0) {
		const lo = st.symStart[cur], hi = st.symEnd[cur];
		c.append(symButton(st.symName[cur], lo), " " + hex(lo) + "–" + hex(hi - 1) + ", " + (hi - lo) +
			" bytes, EIP at +0x" + (phys - lo).toString(16));
		Object.assign(c.dataset, { name: st.symName[cur], lo, hi });
	} else {
		c.append(symDesc(st, -1, phys));
		Object.assign(c.dataset, { name: "", lo: "", hi: "" });
	}
	// History entries are symbols the CPU left; the newest one that is not
	// the current symbol is "previous".
	// Stepped back (R2): the history is the furthest count's.
	const hist = st.behind() ? [] : st.history.filter((h) => !(h.sym === cur && h.sym >= 0));
	const prev = hist.length ? hist[hist.length - 1] : null;
	renderSourceRows(box, phys, cur >= 0 ? st.symName[cur] : null);
	if (dbgHas())
		renderFnRows(box, phys);
	renderTaskRow(box);
	const uw = renderCallStack(box, phys);
	const p = row("previous");
	p.id = "prev_sym";
	p.title = "The symbol the CPU was in before the current one, recorded while stepping (not while v86's JIT runs)";
	if (prev && prev.sym >= 0) {
		p.append(symButton(st.symName[prev.sym], prev.last, "Jump to the last instruction it ran\n" + dbgTitle(prev.last, st.symName[prev.sym])),
			" left at " + hex(prev.last));
		p.dataset.name = st.symName[prev.sym];
	} else {
		p.append(st.behind() ? "recorded at instruction " + num(st.ahead) + ", not here (stepped back)" :
			prev ? symDesc(st, prev.sym, prev.last) : "none recorded yet");
		p.dataset.name = "";
	}
	const crumbs = row("path");
	crumbs.className = "crumbs";
	const names = [cur >= 0 ? st.symName[cur] : symDesc(st, -1, phys)];
	const items = [symButton(names[0], cur >= 0 ? st.symStart[cur] : -1, cur >= 0 ? dbgTitle(phys, names[0]) : "")];
	for (let k = hist.length - 1; k >= 0 && items.length < 6; k--) {
		const h = hist[k];
		items.push(symButton(symDesc(st, h.sym, h.last), h.sym >= 0 ? h.last : -1, h.sym >= 0 ? "left at " + hex(h.last) + "\n" + dbgTitle(h.last, st.symName[h.sym]) : ""));
	}
	items.forEach((it, k) => crumbs.append(k ? " ← " : "", it));
	crumbs.dataset.count = items.length;
	return uw;
}

// The call stack, innermost first. With DWARF the backtrace is unwound
// from the registers and the stack (dbginfo.js dbgUnwind: the CFI of each
// function, entry code's saved pt_regs, else the EBP chain); where that
// stops, the frames below come from the call stack the stepper
// reconstructed while stepping (stepper.js stackEnter, the ESP
// heuristic), and without DWARF all of them do. Each frame names the
// method that found it, and carries the DWARF file:line and inlined-at
// chain of its instruction (EIP for the top one, the call for the others)
// in its title.
const FRAME_METHODS = {
	regs: ["EIP", "the CPU's registers: where it is now"],
	cfi: ["CFI", "unwound through the call frame information (.debug_frame) of the function it called: exact"],
	pt_regs: ["pt_regs", "the context an interrupt, exception or system call interrupted, from the registers its entry code saved: exact"],
	entry: ["entry ESP", "the return address at the ESP the stepper saw this function's caller call it with, where the function inside had no call frame information: exact when the function (assembly) keeps EBX, ESI, EDI and EBP"],
	fp: ["EBP chain", "frame-pointer chain ([EBP] and [EBP+4]) where the function inside had no call frame information: a function without a frame of its own may be missing"],
	recorded: ["recorded", "the call stack recorded while stepping (calls and returns seen, ESP heuristic): only as complete as the recording"],
};

// Registers and memory for dbgUnwind: guest memory through the page tables
// (cpustate.js pageWalk: read only, no TLB or accessed-bit side effects).
function unwindHere() {
	const st = stepper, cpu = st.cpu;
	const read32 = (va) => {
		const w = pageWalk(cpu, va), w2 = (va & 0xfff) > 0xffc ? pageWalk(cpu, va + 3) : w;
		if (w.phys === null || w2.phys === null)
			return undefined;
		if (w2 === w) {
			const v = phys32(cpu, w.phys);
			return v === null ? undefined : v;
		}
		let v = 0;
		for (let k = 3; k >= 0; k--) {
			const q = pageWalk(cpu, va + k).phys;
			if (q === null || q >= cpu.mem8.length)
				return undefined;
			v = v * 256 + cpu.mem8[q];
		}
		return v;
	};
	// Code of a symbol, or just past one: the return address of a call
	// that does not return can be the end of its function (startup_32's
	// call of hello_main in the hello kernel).
	const toPhys = (va) => {
		const pa = pageWalk(cpu, va).phys;
		return pa !== null && (st.symbolOf(insnStart(pa)) >= 0 || st.symbolOf(insnStart(pa - 1)) >= 0) ? pa : null;
	};
	const symAt = (pa) => {
		const i = st.symbolOf(insnStart(pa));
		return i >= 0 ? [st.symName[i], st.symStart[i]] : null;
	};
	// An open frame the stepper recorded for this function, entered at or
	// above esp on the same stack.
	const entryEsp = (pa, esp) => {
		// The instruction that ran last was a call to here (EIP has not
		// entered the function yet): the return address is at ESP.
		if (st.behind()) // stepped back (R2): the recorded frames are the furthest count's
			return undefined;
		const m = cpu.mem8, lp = st.lastPhys;
		if (esp === st.regs().esp && lp >= 0 && lp + 2 < m.length && ((st.espBefore - 4) >>> 0) === esp &&
		    (m[lp] === 0xe8 || (m[lp] === 0xff && (m[lp + 1] >> 3 & 7) === 2)))
			return esp;
		const i = st.symbolOf(insnStart(pa));
		for (const k of st.allOpen())
			if (st.fr.node(k) === i && st.fr.esp(k) >= esp && st.fr.esp(k) >>> 13 === esp >>> 13 && !st.fr.cont(k))
				return st.fr.esp(k);
		return undefined;
	};
	return dbgUnwind(st.regs(), read32, toPhys, symAt, entryEsp);
}

// The instruction holding pa (a caller's frame is at its return address - 1:
// inside the call), from the build's disassembly; pa without one.
function insnStart(pa) {
	if (!disasm)
		return pa;
	const k = insnIndex(pa), ins = disasm.insns;
	return k < ins.length && ins[k][0] === pa ? pa : k > 0 && ins[k - 1][0] < pa && pa - ins[k - 1][0] < 16 ? ins[k - 1][0] : pa;
}

function renderCallStack(box, phys) {
	const st = stepper;
	const d = document.createElement("div");
	d.id = "call_stack";
	const b = document.createElement("b");
	b.textContent = "call stack";
	d.append(b, " ");
	// Stepped back (R2): the recorded call stack is the furthest count's.
	const behind = st.behind();
	const top = !behind && st.stack.length ? st.stack[st.stack.length - 1] : -1;
	const rec = [];
	let at = phys;
	// EIP on the first byte of a symbol the stack has not entered yet (a
	// call about to run, e.g. after Run to): that symbol on top, its caller
	// at the instruction that ran last.
	const cur = phys >= 0 ? st.symbolOf(phys) : -1;
	if (!behind && cur >= 0 && phys === st.symStart[cur] && (top < 0 || st.fr.node(top) !== cur)) {
		rec.push([-1, phys, cur]);
		at = st.lastPhys;
	}
	for (let k = top; k >= 0 && rec.length < 24; k = st.fr.parent(k)) {
		rec.push([k, at, st.fr.node(k)]);
		at = st.fr.from(k);
	}
	// [pc, node, method, extra title, cfa, registers known in the frame,
	// virtual EIP or return address, label of a frame that is no code here]
	let frames = rec.map(([k, pc, node]) => [pc, node, "recorded", k >= 0 && st.fr.irq(k) ? ", in an interrupt" : "", undefined]);
	const uw = runState === "paused" || runState === "stepped" ? unwindHere() : { frames: [], state: "none", why: "" };
	if (uw.frames.length) {
		frames = uw.frames.map((f) => f.user ? [-1, -1, f.method, "user space: EIP 0x" + (f.pc >>> 0).toString(16) +
			", ESP 0x" + (f.regs[4] >>> 0).toString(16), undefined, f.regs, f.pc, "user space"] :
			[insnStart(f.lookup), st.symbolOf(insnStart(f.lookup)), f.method,
			(f.cfa !== undefined ? ", CFA 0x" + (f.cfa >>> 0).toString(16) : "") + (f.row ? "\nCFI row: " + cfiRowText(f.row) : "") +
			(f.fallbackWhy ? "\nno CFI step here: " + f.fallbackWhy : "") + (f.ptRegs !== undefined ? "\npt_regs at 0x" + f.ptRegs.toString(16) : ""), f.cfa, f.regs, f.pc]);
		if (uw.state !== "ok") {
			// The recorded frames below the last unwound one: those that
			// entered at or above its ESP on the same stack, minus itself.
			const last = uw.frames[uw.frames.length - 1];
			const esp = last.regs[4];
			let j = esp === undefined ? -1 : rec.findIndex(([k]) => k >= 0 && st.fr.esp(k) >>> 13 === esp >>> 13 && st.fr.esp(k) >= esp);
			if (j >= 0 && rec[j][2] === st.symbolOf(last.lookup))
				j++;
			if (j >= 0)
				for (const [k, pc, node] of rec.slice(j))
					frames.push([pc, node, "recorded", st.fr.irq(k) ? ", in an interrupt" : "", undefined]);
		}
	}
	if (!frames.length)
		d.append(runState === "jit" ? "not recorded at full speed" : "none recorded yet (calls are seen while stepping)");
	const list = [];
	frames.forEach(([pc, node, method, extra, , , , label], n) => {
		const name = label || (node >= 0 ? st.nodeName(node) : "0x" + pc.toString(16));
		const r = pc >= 0 ? dbgFrames(pc) : { state: "none" };
		const f0 = r.state === "ok" && r.frames[0].file && r.frames[0].line ? r.frames[0] : null;
		const inl = r.state === "ok" && r.frames.length > 1 ? " [" + r.frames.length + " inlined]" : "";
		const m = FRAME_METHODS[method];
		const btn = symButton(name, pc, label ? extra + "\n" + m[0] + ": " + m[1] :
			(n ? "calls from " : "at ") + hex(pc) + extra + "\n" + m[0] + ": " + m[1] + "\n" + dbgTitle(pc, name));
		const tag = document.createElement("span");
		tag.className = "fmeth";
		tag.textContent = m[0];
		btn.append(inl, " ", tag);
		btn.dataset.pc = pc;
		btn.dataset.method = method;
		// The frame's name opens its code in the Page tab, its file:line the Source panel.
		const item = document.createElement("span");
		item.className = "cframe";
		item.append(btn);
		// A function bookmark of the frame's function (K1): its ☆, and ★ once bookmarked.
		const fnOf = !label && node >= 0 && node < st.symName.length ? { pa: st.symStart[node], name: st.symName[node] } : null;
		if (fnOf) {
			item.dataset.fnpa = fnOf.pa;
			item.dataset.fn = fnOf.name;
			const names = bmFnNames(fnOf.pa);
			if (names)
				item.append(bmStarEl(names));
			item.append(bmAddButton(() => bmDraftFn(fnOf.pa, fnOf.name), "Bookmark " + fnOf.name + " (a function bookmark)"));
		}
		if (f0)
			item.append(" ", srcLinkEl(f0.file, f0.line, "", pc));
		list.push(item);
	});
	list.forEach((it, n) => d.append(n ? " ← " : "", it));
	if (uw.state === "ok" && uw.why || uw.state === "stopped" || uw.state === "loading") {
		const note = document.createElement("span");
		note.className = "dim";
		note.textContent = uw.state === "ok" ? " (" + uw.why + ")" : uw.state === "loading" ? " (" + uw.why + ")" :
			" (unwinding stopped: " + uw.why + (frames.length > uw.frames.length ? "; the frames below were recorded while stepping" : "") + ")";
		d.append(note);
	}
	d.title = dbgCfiInfo() ? "Unwound with the call frame information: " + dbgCfiInfo() : "";
	d.dataset.count = frames.length;
	d.dataset.unwind = uw.state;
	d.dataset.why = uw.why || "";
	d.dataset.frames = JSON.stringify(frames.map(([pc, node, method, , cfa, regs, va, label]) =>
		({ name: label || (node >= 0 ? st.nodeName(node) : ""), pc, method, cfa: cfa === undefined ? null : cfa, regs, va, user: !!label })));
	box.append(d);
	return uw;
}

// The page API's entries of this module (page.js).
provide({ jumpTo, renderSymbols, unwindHere, insnStart, symButton, FRAME_METHODS, openFunction, afterStop });

// For the tests: a stand-in for openFunction, which graph.js and
// bookmarks.js call too (through the page API).
const setOpenFunction = (f) => {
	openFunction = f;
	provide({ openFunction });
};

export { codeSymbols, afterStop, frameUpdate, jumpTo, listingStale, viewCost, openFunction, setOpenFunction };
