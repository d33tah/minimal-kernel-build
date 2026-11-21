"use strict";

// Debugger tab's drawing: the code around EIP, registers, counters, the
// clock and speed, the end of the run, the run badge (H1).

import { CLOCK, realNow, GUEST } from "./clock.js";
import { $, V86_KERNEL_ADDR, escHtml, h8, hex, h4, num, withV } from "./util.js";
import { runTo, entryPhase, stepper, tab, runState, api, disasm, image, symbols, emuRam, emulator, prevRegs,
	refSaved, countMs, countSteps, counting, countNote, wasmPatchError, setRefSaved, emuReady,
	setEntryPhase, jitLoopId, setCounting, state, vmHasKeys, publishMachine, provide } from "./page.js";
import { jumpsFor, jumpPaths, jumpBadgesHtml } from "./jumps.js";
import { jumpsDrawn, renderEipJumps, jumpInlineMax, jumpInlineHtml, jumpStubAttrs } from "./jumpview.js";
import { G } from "./graphdata.js";
import { dbgHas, dbgFrames } from "./dbginfo.js";
import { srcFocusKeep, srcLinkEl } from "./srclinks.js";
import { srcLineHtml } from "./source.js";
import { liveDecode, iced, icedError, loadIced, bitnessAt, decodeLive, virtOffset } from "./livedis.js";
import { ffActive, ffInfo } from "./fastforward.js";
import { opEnv, insnHtml } from "./varops.js";
import { renderWatches, renderLocals } from "./vars.js";
import { replayActive, replayAfterStop } from "./replay.js";
import { REV, revRender } from "./reverse.js";
import { DBGSTATUS, DBGCODE, dbgStatusText } from "./dbgtab.js";
import { bmCodeStar, bmMenuFit } from "./bookmarks.js";
import { PWA, pwaReload } from "./pwa.js";
import { STEP_CONST } from "./stepper.js";
import { kernelEnd } from "./header.js";
import { symbolAt } from "./memview.js";
import { insnIndex, diffTip, diffBytesHtml, PAGETAB } from "./listing.js";

const { TEXT_PROMPT, TEXT_END } = STEP_CONST;

const COUNT_LIMIT_MS = 12000000; // give up counting after this much stepping (200 min)
const SPEED_KEY = "mkb-speed";
const COUNT_KEY = "mkb-count";
const REF_KEY = "mkb-ref-";
// Instructions from the entry to "Still alive" when no run on this browser
// measured it yet (for the ETA only).
// Instructions from the entry to "Still alive" (the same at every clock rate).
const REF_STEPS = { hello: { any: 432 }, "hello-os": { any: 154 },
	"minified-linux": { any: 723158 }, "minified-linux-os": { any: 132006 } };

const speed = () => $("dbg_speed").value;  // "full" or instructions per second
// Fast stepping (no animation): full speed while counting, Run to, or the
// way from reset to the kernel entry.
const fastStepping = () => speed() === "full" || runTo !== null || entryPhase || replayActive();

// Counting ends at the kernel's end: panic, or hlt with interrupts off.
function countDone() {
	return !stepper || !!stepper.end;
}

// High stepped speeds (1M/s and up) run in the message loop like full
// stepping, limited by the step credit, and redraw at most 10 times a second.
const highRate = () => !fastStepping() && Number(speed()) >= 1e6;

// The Page tab's "C source" toggle is there when the kernel has DWARF
// (pagepanel.jsx reads dbgHas()).
function syncSrcToggle() {
	PAGETAB.changed();
}

// The Debugger tab drawn again (its file:line links keep the keyboard focus).
// The mark of the EIP line in Code at EIP, with its popover.
const EIP_MARK = '<span class="eipmark tipmark" tabindex="0" role="button" aria-label="EIP" data-tip="EIP: the next instruction the CPU ' +
	'runs (the boxed line). Tap the line itself to open it in the Page tab.">▶</span>';

function renderContext() {
	srcFocusKeep(drawContext);
}

// Code at EIP, the symbol rows, the paths at EIP and the call frames'
// variables, once the tab was shown (debuggerpanel.jsx): the listing and
// the paths are written through their refs (DBGCODE.ctxEl, jumpsEl), the
// rows and the frames are nodes DBGCODE holds, drawn by changed().
function drawContext() {
	const el = DBGCODE.ctxEl;
	if (!el)
		return;
	renderWatches();
	if (!stepper || tab !== "debugger" || runState === "jit" || runState === "off") {
		el.textContent = runState === "jit" ? "Running at full speed (JIT). Pause to see the code at EIP; the symbol history is not recorded at full speed." : "";
		DBGCODE.sym = [];
		renderEipJumps(DBGCODE.jumpsEl, null, -1);
		if (tab === "debugger")
			renderLocals(null);
		DBGCODE.changed();
		return;
	}
	const phys = stepper.nextPhys();
	if (phys < 0) {
		el.textContent = "EIP is not mapped.";
		renderLocals(null);
		renderEipJumps(DBGCODE.jumpsEl, null, -1);
		DBGCODE.changed();
		return;
	}
	renderLocals(api.show.renderSymbols(phys));
	const lines = [];
	let ins = disasm ? disasm.insns : [];
	let i = disasm ? insnIndex(phys) : 0;
	let exact = i < ins.length && ins[i][0] === phys;
	const inKernel = image && phys >= V86_KERNEL_ADDR && phys < kernelEnd(image.hdr);
	let live = false;
	if ((!exact || liveDecode()) && iced) {
		// Decode guest memory from the containing symbol's start (aligned)
		// through EIP and ~10 instructions after it; without a symbol, from EIP.
		const sym = symbols ? symbols.symbols.find((x) => x.size > 0 && x.paddr <= phys && phys < x.paddr + x.size &&
			phys - x.paddr < 0x10000) : null;
		const start = sym ? sym.paddr : phys;
		const stop = Math.min(emuRam, phys + 160);
		if (stop > start) {
			const list = decodeLive(emulator.read_memory(start, stop - start), start, (start + virtOffset(start)) >>> 0, bitnessAt(phys));
			const k = list.findIndex((e) => e[0] === phys);
			if (k >= 0) {
				ins = list;
				i = k;
				exact = true;
				live = true;
				el.dataset.bits = bitnessAt(phys);
			}
		}
	} else if (!exact && !iced && !icedError)
		loadIced();
	if (!exact) {
		const n = Math.max(0, Math.min(16, emuRam - phys));
		const b = n > 0 ? emulator.read_memory(phys, n) : [];
		lines.push('<span class="cur" data-addr="' + phys + '">' + phys.toString(16).padStart(8, "0") + " " +
			Array.from(b, (v) => v.toString(16).padStart(2, "0")).join(" ") + "</span>");
		lines.push('<span class="dim">' + (!inKernel ?
			"EIP is outside the kernel image (BIOS, option ROM or real-mode setup): disasm.json has no code here, raw bytes at EIP." :
			"EIP is not an instruction start in the build's disassembly (the code here is not vmlinux's yet, e.g. the bzImage's relocation stub): raw bytes at EIP." +
			(disasm && i < ins.length && ins[i][0] < phys + 64 ? " Nearest instructions from the build:" : "")) + "</span>");
	}
	el.dataset.source = live ? "live" : exact ? "disasm" : "bytes";
	let jS = null;
	const paths = new Map();
	if (live)
		lines.push('<span class="dim">decoded live from guest memory (iced-x86, ' + bitnessAt(phys) + "-bit)</span>");
	if ((disasm && inKernel) || live) {
		const from = Math.max(0, exact ? i - 6 : i - 3), to = Math.min(ins.length, i + 10);
		if (to > from) {
			// The end of the furthest instruction: llvm-objdump decodes the
			// zero padding before an aligned function as instructions that
			// can run into the function (-O0: "0050c7" at 0x100d4f over
			// 0x100d50), so the last one listed need not end last.
			const lo = ins[from][0];
			let hi = lo;
			for (let k = from; k < to; k++)
				hi = Math.max(hi, ins[k][0] + ins[k][1].length / 2);
			const mem = hi <= emuRam ? emulator.read_memory(lo, hi - lo) : null;
			const sized = symbols ? symbols.symbols.filter((s) => s.size > 0 && s.paddr < hi && s.paddr + s.size > lo) : [];
			const srcState = { key: null };
			const ops = live && bitnessAt(phys) === 16 ? null : opEnv(), opStat = {};
			jS = jumpsFor(ins);
			const shown = new Set(ins.slice(from, to).map((x) => x[0]));
			const inView = (x) => shown.has(x), imax = jumpInlineMax(el);
			for (let k = from; k < to; k++) {
				const [pa, raw, text] = ins[k];
				const srcLine = srcLineHtml(pa, srcState);
				if (srcLine)
					lines.push(srcLine);
				let bytes = "", ok = true;
				for (let j = 0; j < raw.length / 2; j++) {
					const want = raw.substr(j * 2, 2);
					const got = mem ? mem[pa - lo + j].toString(16).padStart(2, "0") : want;
					if (got !== want)
						ok = false;
					if (j < 7)
						bytes += got !== want ? "<mark>" + got + "</mark>" : got;
				}
				const tip = ok || !mem ? "" : diffTip(pa, raw, mem.subarray(pa - lo, pa - lo + raw.length / 2));
				if (tip)
					bytes = diffBytesHtml(bytes, tip);
				bytes += raw.length / 2 > 7 ? "…" : "  ".repeat(7 - raw.length / 2);
				const sym = symbolAt(sized, pa);
				const cls = "ins" + (pa === phys ? " cur" : "") + (ok ? "" : " bad");
				const P = jumpPaths(jS, stepper, pa);
				paths.set(pa, P);
				const J = jumpInlineHtml(jS, P, pa, inView, imax);
				lines.push(J.src + '<span class="' + cls + '" data-addr="' + pa + '"' + (tip ? ' data-tip="' + escHtml(tip) + '"' : "") +
					jumpStubAttrs(J) + ">" + (pa === phys ? EIP_MARK : " ") +
					pa.toString(16).padStart(8, "0") + " " + bytes + " " + insnHtml(pa, text, ops, opStat) +
					(sym ? ' <span class="dim">' + escHtml(sym.name) + "+0x" + (pa - sym.paddr).toString(16) + "</span>" : "") +
					bmCodeStar(pa) + "</span>" + J.dst + jumpBadgesHtml(P, pa));
			}
		}
	}
	el.innerHTML = lines.join("\n");
	el.dataset.draws = Number(el.dataset.draws || 0) + 1;
	el.dataset.jlocal = live ? "instructions decoded live around EIP" : "";
	jumpsDrawn(el, jS, paths);
	renderEipJumps(DBGCODE.jumpsEl, exact ? jS : null, phys);
	el.dataset.phys = phys;
	el.dataset.opsNamed = el.querySelectorAll(".opn").length;
	const cur = el.querySelector(".ins.cur");
	el.dataset.cur = cur ? cur.dataset.addr : "";
	DBGCODE.changed();
}

// EIP to highlight in the inspector, or -1.
function currentPhys() {
	return stepper && runState !== "jit" && runState !== "off" ? stepper.nextPhys() : -1;
}

const FLAG_BITS = [[0, "CF"], [2, "PF"], [4, "AF"], [6, "ZF"], [7, "SF"], [8, "TF"], [9, "IF"], [10, "DF"], [11, "OF"], [14, "NT"], [16, "RF"], [17, "VM"]];

// The Debugger's registers (debuggerpanel.jsx Regs draws them when the
// machine is published): name, value, title and whether it changed since
// they were last drawn (prevRegs, which Regs sets once drawn), or null
// without a stepper.
function regsView() {
	if (!stepper)
		return null;
	const r = stepper.regs();
	const flags = FLAG_BITS.filter(([b]) => r.eflags & (1 << b)).map(([, n]) => n);
	flags.push("IOPL" + ((r.eflags >> 12) & 3));
	const mode = !(r.cr0 & 1) ? "real mode" : (r.eflags & 0x20000 ? "vm86" : "protected") +
		(r.cr0 & 0x80000000 ? ", paging" : "");
	const phys = stepper.nextPhys();
	const cells = [
		["EIP", h8(r.eip), "Instruction pointer (offset in CS). Linear " + hex(r.linear) + ", physical " + (phys < 0 ? "unmapped" : hex(phys))],
		["ESP", h8(r.esp)], ["EBP", h8(r.ebp)],
		["EAX", h8(r.eax)], ["EBX", h8(r.ebx)], ["ECX", h8(r.ecx)], ["EDX", h8(r.edx)],
		["ESI", h8(r.esi)], ["EDI", h8(r.edi)],
		["EFLAGS", h8(r.eflags) + " " + flags.join(" "), "Set flags of EFLAGS"],
		["CS", h4(r.cs)], ["DS", h4(r.ds)], ["SS", h4(r.ss)], ["ES", h4(r.es)],
		["CR0", h8(r.cr0), "PE=" + (r.cr0 & 1) + " PG=" + (r.cr0 >>> 31)], ["CR3", h8(r.cr3), "Page directory base"],
		["CPL", String(r.cpl), "Current privilege level"],
		["mode", mode + ", " + (r.is32 ? "32" : "16") + "-bit" + (r.hlt ? ", hlt" : "")],
		["phys", phys < 0 ? "unmapped" : h8(phys), "Physical address of the next instruction"],
	].map(([name, value, title]) => {
		const chg = !!prevRegs && prevRegs[name] !== undefined && prevRegs[name] !== value;
		return { name, value, chg, wide: name === "EFLAGS" || name === "mode",
			title: title || chg ? (title ? title + ". " : "") + (chg ? "Yellow: changed since the registers were last shown" : "") : undefined };
	});
	return { cells, eip: r.eip, phys, shown: Object.fromEntries(cells.map((c) => [c.name, c.value])) };
}

function renderCounters() {
	const tbody = $("counters").tBodies[0];
	tbody.textContent = "";
	if (!stepper) {
		$("count_text").textContent = !emulator ? "" : "Not counted: " + (wasmPatchError || "no debugger") + ".";
		return;
	}
	const rows = stepper.milestones.map((m) => [m.text, m.found, m.text]);
	rows.push([stepper.exit ? "exit(" + stepper.exit.code + ")" : "exit()", stepper.exit, "exit"]);
	if (stepper.panicSym >= 0)
		rows.push(["panic() entered", stepper.panicEntered, "panic_entry"]);
	if (stepper.prompt)
		rows.push(["(initramfs) prompt", stepper.prompt, "prompt"]);
	rows.push([stepper.end ? (/panic/.test(stepper.end.kind) ? "panic, stopped" : "halted") : "end", stepper.end, "end"]);
	rows.push(["now", stepper.snapshot(), "now"]);
	for (const [label, c, key] of rows) {
		const tr = tbody.insertRow();
		tr.dataset.milestone = key;
		const cells = c && c.jit ? [label, "run by the JIT", "", ""] :
			c ? [label, num(c.sinceEntry), num(c.uniqueKernel) + "/" + num(c.unique - c.uniqueKernel), num(c.steps)] :
			[label, counting ? "counting..." : "not reached", "", ""];
		for (const v of cells)
			tr.insertCell().textContent = v;
		if (c && !c.jit) {
			tr.dataset.steps = c.steps;
			tr.dataset.entry = c.sinceEntry === null ? "" : c.sinceEntry;
			tr.dataset.unique = c.unique;
			tr.dataset.kernel = c.uniqueKernel;
			tr.cells[0].title = "Reached by the instruction at physical " + hex(c.phys);
		}
	}
	const alive = stepper.milestones.find((m) => m.text === "Still alive");
	// Once per measurement: after a Reset in the Settings tab the next redraw
	// does not write it back.
	if (alive && alive.found && alive.found.sinceEntry && image && refSaved !== alive.found)
		try {
			setRefSaved(alive.found);
			localStorage.setItem(REF_KEY + image.name + "-" + (clockMips() || "real"), alive.found.sinceEntry);
		} catch (e) {
			// no storage: the built-in estimate stays
		}
	// What the stepping did in its time: not the instructions the JIT ran
	// (the fast-forward to the entry: 2.3e9 for Debian, J2 saw "3918091.6
	// M/s") nor the BIOS that the fast-forward steps.
	const rate = countMs > 0 ? countSteps / countMs / 1000 : 0;
	const info = $("count_text");
	$("count_info").dataset.ms = Math.round(countMs);
	if (runState === "stepped" && !fastStepping() && !countDone()) {
		info.textContent = "Counting from the kernel entry: " + num(stepper.steps - Math.max(0, stepper.sinceEntry)) +
			" instructions so far. " + eta() + countNote;
		return;
	}
	info.textContent = (counting ? (runState === "paused" ? "Counting (paused): " : "Counting: ") : stepper.steps ? "Counted " : "") +
		(stepper.steps ? num(stepper.steps) + (stepper.jitBeforeEntry || stepper.jitAfterEntry ? " instructions (stepped and run by the JIT)" : " instructions stepped") +
			(countMs ? ", " + (countMs / 1000).toFixed(2) + " s of JavaScript stepping time (" + rate.toFixed(1) + " M/s)" : "") + ". " : "") +
		(!$("count").checked && !stepper.steps ? "Counting is off: the boot ran at full speed. " : "") +
		(runState === "jit" && !countDone() && stepper.steps ? (CLOCK.virtual ? "Running at full speed on the virtual clock: counted, the same run every time. " :
			"Running at full speed on real time now: not counting. ") : "") +
		countNote;
}

// Virtual clock rate in instructions per microsecond, or 0 for real time.
const MIPS_KEY = "mkb-mips";
const clockMips = () => ($("clock_mips").value === "real" ? 0 : Number($("clock_mips").value));

// Real speed: v86's instruction counter (wasm memory, offset 664) counts
// what the JIT runs and what the stepper steps; sampled 4 times a second.
// The rate is taken over a window (4 s in slow motion, 1 s otherwise),
// counted from the last change (Boot, Pause, Continue, a speed, the entry,
// the tab shown again): a one-second window at 1/s read 0/s whenever it
// held no instruction, and a window reaching back into a pause or another
// speed showed that one. At 10/s a one- or two-second window read 1/s to
// 4/s after one drawing of a new page's listing held a phone's main
// thread for 1.5 to 2.2 s, which the step credit makes up right after.
// Until half the window has passed it says "measuring".
const SPEED_SAMPLE_MS = 250;
let speedHist = [];      // [{ counter, at }] since the last change, oldest first
let realSpeed = null;    // instructions per second, null while measuring
function speedWindowMs() {
	if (runState !== "stepped" || fastStepping() || highRate())
		return 1000;
	return 4000;
}
// Paused or off, unless a fast-forward runs (the boot's to the entry sets
// "paused" under it): that one says "measuring" until it has a rate.
const speedStopped = () => !ffActive && (runState === "paused" || runState === "off");
function speedReset() {
	speedHist = [];
	realSpeed = speedStopped() ? 0 : null;
	if (stepper)
		renderClock();
}
function sampleSpeed() {
	if (!stepper || !emuReady) {
		speedHist = [];
		realSpeed = null;
		return;
	}
	const c = new Uint32Array(stepper.cpu.wasm_memory.buffer, 664, 1)[0];
	const now = realNow();
	speedHist.push({ counter: c, at: now });
	const w = speedWindowMs();
	while (speedHist.length > 2 && now - speedHist[1].at >= w)
		speedHist.shift();
	const first = speedHist[0];
	if (speedStopped())
		realSpeed = 0;
	else if (now - first.at >= w / 2)
		realSpeed = ((c - first.counter) >>> 0) / ((now - first.at) / 1000);
	else
		realSpeed = null;
	renderClock();
}

const fmtRate = (r) => (r >= 1e6 ? (r / 1e6).toFixed(r >= 1e7 ? 0 : 1) + "M" : r >= 1e3 ? (r / 1e3).toFixed(r >= 1e4 ? 0 : 1) + "k" :
	Math.round(r)) + "/s";

function renderClock() {
	const el = $("vclock");
	if (!stepper) {
		el.textContent = "";
		return;
	}
	const t = CLOCK.now();
	const behind = behindRate();
	el.textContent = (realSpeed === null ? "speed: measuring" : behind !== null ? "speed " + behind + " (set " +
		$("dbg_speed").selectedOptions[0].text + ": too fast for this device)" : "speed " + fmtRate(realSpeed)) + " \u00b7 " + (CLOCK.virtual ? "virtual " : "real ") + "time " + (t / 1000).toFixed(4) +
		" s \u00b7 IRQ0 \u00d7" + num(stepper.irq0) + (stepper.idleMs ? " \u00b7 idle " + (stepper.idleMs / 1000).toFixed(3) + " s" : "");
	el.dataset.speed = realSpeed === null ? "" : Math.round(realSpeed);
	el.title = "Instructions per second actually executed (v86's instruction counter: JIT and stepping), over the last " +
		(speedWindowMs() / 1000) + " s since the last Boot, Pause, Continue or speed change (\"measuring\" for the first half of that); " +
		"the speed menu sets the target, which JavaScript stepping may not reach.";
	el.dataset.irq0 = stepper.irq0;
	el.dataset.ms = t;
	el.dataset.virtual = CLOCK.virtual;
	renderGuestInputs();
}

// The guest's date and random numbers (clock.js GUEST): from the start
// date and the seed on the virtual clock, the browser's on real time.
// (Only renderClock calls it, once there is a stepper.)
function renderGuestInputs() {
	const el = $("guest_in_text");
	const iso = (ms) => new Date(ms).toISOString().replace("T", " ").replace(/\.\d+Z$/, " UTC");
	el.textContent = GUEST.why + (CLOCK.virtual ?
		"RTC " + iso(GUEST.dateNow()) + " (from " + iso(GUEST.rtcStart) + ") \u00b7 RDRAND seed " + GUEST.seed :
		"RTC and RDRAND: the browser's date and random numbers (real time)");
}

// At the kernel's 32-bit entry (or past it): from here on at the chosen speed.
function finishEntry() {
	setEntryPhase(false);
	speedReset(); // the fast part before the entry is not this speed
	const n = stepper.sinceEntry >= 0 ? stepper.sinceEntry : stepper.steps;
	const ff = ffInfo && ffInfo.stopPhys !== undefined && !ffInfo.runTo;
	const at = ff ? ffInfo.target : V86_KERNEL_ADDR;
	$("entry_info").textContent = "Entered the kernel at " + hex(at) + (ff ? " (" + ffInfo.name + ")" : "") + " after " +
		num(n) + " instructions from reset" +
		(ff ? " (fast-forwarded with the JIT in " + (ffInfo.ms / 1000).toFixed(1) + " s: BIOS, setup, decompressor; counted from here)" : "") +
		"; now " + $("dbg_speed").selectedOptions[0].text + ".";
	$("entry_info").dataset.ffMs = ff ? Math.round(ffInfo.ms) : "";
	$("entry_info").dataset.ffTarget = ff ? ffInfo.target : "";
	$("entry_info").dataset.steps = n;
	updateDebugUi();
}

// Time to "Still alive" at the current speed, from the count this browser
// last measured for this kernel or a built-in estimate.
function eta() {
	if (!image || !stepper || stepper.sinceEntry < 0)
		return "";
	let ref = null, measured = false;
	try {
		ref = Number(localStorage.getItem(REF_KEY + image.name + "-" + (clockMips() || "real"))) || null;
		measured = !!ref;
	} catch (e) {
		ref = null;
	}
	const known = REF_STEPS[image.name];
	ref = ref || (known && (known.any || known[clockMips() || "real"]));
	if (!ref)
		return "No estimate of how long this kernel needs. ";
	const left = Math.max(0, ref - (stepper.steps - stepper.sinceEntry));
	const sec = Math.round(left / Number(speed()));
	const t = sec >= 3600 ? Math.floor(sec / 3600) + " h " + Math.floor(sec % 3600 / 60) + " min" :
		sec >= 60 ? Math.floor(sec / 60) + " min " + (sec % 60) + " s" : sec + " s";
	$("count_info").dataset.eta = sec;
	return "\"Still alive\" in about " + t + " at this speed (" + (measured ? "this browser measured " : "estimate: ") +
		"~" + num(ref) + " instructions from the entry). ";
}

// What the end of the kernel looked like, for the banner and the status.
function endText() {
	const e = stepper && stepper.end;
	if (!e)
		return "";
	// Nothing was stepped (full JIT speed, count off): no count to report.
	const after = !e.steps ? "" : " after " + num(e.sinceEntry !== null ? e.sinceEntry : e.steps) + " instructions from the kernel entry";
	if (e.kind === "panic-budget")
		return "panic() entered, no end-of-panic text in the next " + num(stepper.panicBudget) + " instructions. Stopped" + after + ".";
	if (e.kind === "panic-text")
		return "Kernel panic: " + e.message + (e.exact ? " (stopped where its last line was written)" : " (stopped by the page)") + after + ".";
	if (e.kind === "panic")
		return "Kernel panic" + (stepper.exit ? " (init exited with code " + stepper.exit.code + ")" : "") + after +
			". Machine stopped here.";
	return "Halted: hlt with interrupts off" + after + ". The machine has stopped.";
}

// version.json, written by the Pages workflow: which commit is deployed.
async function loadVersion() {
	const el = $("build_info");
	let v = null;
	try {
		const resp = await fetch(withV("version.json"));
		v = resp.ok ? await resp.json() : null;
	} catch (e) {
		v = null;
	}
	if (!v || !v.sha) {
		el.textContent = "local build";
		return;
	}
	const repo = "https://github.com/" + (v.repo || "d33tah/minimal-kernel-build");
	const a = document.createElement("a");
	a.href = repo + "/commit/" + v.sha;
	a.textContent = "build " + v.sha.slice(0, 12);
	const date = new Date(v.date);
	const when = isNaN(date) ? v.date : date.toISOString().slice(0, 16).replace("T", " ") + " UTC";
	const run = document.createElement("a");
	run.href = repo + "/actions/runs/" + v.run_id;
	run.textContent = "run #" + v.run_number;
	el.textContent = "";
	el.append(a, " \u00b7 " + when + " \u00b7 ", run);
	el.dataset.sha = v.sha;
	$("build_subject").textContent = "Deployed: " + v.subject + " (" + v.sha.slice(0, 12) + ", " + when + ").";
}

// Distro kernels end with "---[ end Kernel panic - not syncing: ... ]---"
// and then spin; Debian's initramfs shell prompt pauses the machine once.
// The stepper's VGA hook sees them written (Stepper.textSeen), and the
// page's own loops stop exactly there (Stepper.run, jitExactStop, J3).
// v86's own loop (the "real time" clock at full speed) cannot be stopped
// inside: check 4 times a second and stop there, where it has got to.
function watchPanicText() {
	if (!stepper || stepper.end || runState !== "jit" || jitLoopId || ffActive)
		return;
	const st = stepper;
	if ((st.textSeen & TEXT_PROMPT) && !st.prompt) {
		st.prompt = { exact: false, ...st.snapshot() };
		setCounting(false);
		// v86 redraws the screen only while the VM runs: let it show the
		// prompt before pausing.
		setTimeout(() => {
			if (stepper === st && runState === "jit")
				api.step.pause("at the (initramfs) prompt");
		}, 300);
		return;
	}
	if (!(st.textSeen & TEXT_END))
		return;
	st.end = { kind: "panic-text", message: st.panicText(), exact: false, ...st.snapshot() };
	setCounting(false);
	api.step.pause(endText());
}

// At full speed (v86's JIT) the kernel's end, hlt with interrupts off,
// left the page "running (full speed, JIT)" at 0 instructions per second
// (also Continue after the end): pause there with the end, as stepping does.
function watchJitEnd() {
	if (!stepper || runState !== "jit" || ffActive || !stepper.dead())
		return;
	stepper.markHalt();
	setCounting(false);
	api.step.pause(endText()).then(replayAfterStop);
}

function renderEnd() {
	const b = $("end_banner");
	const e = stepper && stepper.end;
	b.hidden = !e;
	b.textContent = e ? endText() : "";
	if (b.dataset.kind !== (e ? e.kind : ""))
		G.dirty = true; // the timeline's "now" marker becomes "end"
	b.dataset.kind = e ? e.kind : "";
	b.dataset.code = stepper && stepper.exit ? stepper.exit.code : "";
}

// Slow motion measured below half the set speed: the rate reached (one
// decimal under 10/s, where "0/s" read as a stall), else null.
function behindRate() {
	const set = Number(speed());
	if (runState !== "stepped" || entryPhase || fastStepping() || realSpeed === null || !(set > 0) || realSpeed >= set / 2)
		return null;
	return realSpeed < 10 ? realSpeed.toFixed(1) + "/s" : fmtRate(realSpeed);
}

// The state line's " (reaching 0.3/s: too fast for this device)".
function behindText() {
	const r = behindRate();
	return r === null ? "" : " (reaching " + r + ": too fast for this device)";
}

function updateDebugUi() {
	srcFocusKeep(drawDebugUi);
}

// v86 draws the screen's changed text rows in a requestAnimationFrame loop
// that draws nothing once the VM has stopped, so rows written in the last
// frame before a stop (the panic's last line) and rows the stepper writes
// while paused stayed blank: draw them whenever the Debugger is drawn.
function flushScreen() {
	const sa = emulator && emulator.screen_adapter, vga = emulator && emulator.v86 && emulator.v86.cpu.devices.vga;
	if (sa && typeof sa.update_text === "function" && vga && !vga.graphical_mode)
		sa.update_text();
}

// H1: the menu toggle (#runbadge, bottom right on every tab). Its face says
// whether the VM runs and how fast (a pulsing dot and the speed), else
// paused, halted, panic or off; its menu says the same in words with the
// instruction count. Drawn when the Debugger is (drawDebugUi publishes the
// machine's state, initRunBadge subscribes), so it costs nothing per
// instruction.
function runBadgeState() {
	if (!stepper || runState === "off")
		return "off";
	if (ffActive)
		return "ff";
	if (runState === "stepped" || runState === "jit")
		return "run";
	const e = stepper.end;
	return !e ? "paused" : e.kind.startsWith("panic") ? "panic" : "halted";
}

function renderRunBadge() {
	const box = $("runbadge"), tog = $("runbadge_toggle"), state = runBadgeState();
	// The speed's short text: "1k/s", "full" (the select's "full (JIT)").
	const speed = $("dbg_speed").selectedOptions[0].text.replace(/ \(JIT\)$/, "");
	const face = state === "run" ? speed : state === "ff" ? "JIT" : state;
	const words = { off: "no machine", ff: "fast-forwarding with the JIT", paused: "paused", halted: "halted (the end of the run)",
		panic: "kernel panic (the end of the run)",
		run: (runState === "jit" ? "running at full speed (JIT)" : (entryPhase ? "stepping to the kernel entry, then " + speed : "running at " + speed)) +
			behindText() }[state];
	const newer = PWA.newer ? PWA.newer : "";
	const text = words + (stepper && runState !== "off" ? " \u00b7 " + num(stepper.steps) + " instructions" : "");
	box.dataset.state = state;
	box.dataset.vm = ffActive ? "ff" : runState;
	box.dataset.steps = stepper ? stepper.steps : 0;
	box.dataset.newer = newer;
	tog.dataset.state = state;
	if ($("runbadge_speed").textContent !== face)
		$("runbadge_speed").textContent = face;
	if ($("runbadge_state").textContent !== text)
		$("runbadge_state").textContent = text;
	$("runbadge_toggle").querySelector(".rbnews").hidden = !newer;
	const label = "Menu: " + words + (newer ? ", a new version is deployed" : "") + ". Back, Forward, Pause";
	if (tog.title !== label) {
		tog.title = label;
		tog.setAttribute("aria-label", label);
	}
	const pause = $("dbg_pause"), cont = $("dbg_continue"), b = $("runbadge_pause");
	const canPause = !pause.disabled, ended = !!(stepper && stepper.end);
	// At the end of the run there is nothing to continue (the overlay's Continue goes too).
	b.disabled = canPause ? false : cont.disabled || ended;
	b.textContent = canPause ? "Pause" : "Continue";
	b.title = canPause ? "Stop the CPU (the Machine tab's Pause)" : "Run again at the selected speed (the Machine tab's Continue)";
}

// The "new version" notice (pwa.js) in the menu too: the Machine tab's copy
// is out of sight on the other tabs.
function renderRunBadgePwa() {
	const el = $("runbadge_pwa"), newer = PWA.newer;
	el.hidden = !newer;
	el.textContent = "";
	if (newer) {
		const b = document.createElement("button");
		b.type = "button";
		b.textContent = "Reload";
		b.addEventListener("click", pwaReload);
		el.append("A new version of this page is deployed (" + newer + "). ", b);
	}
	renderRunBadge();
}

function runBadgeOpen(open, focus = false) {
	const tog = $("runbadge_toggle");
	$("runbadge_menu").hidden = !open;
	tog.setAttribute("aria-expanded", open ? "true" : "false");
	bmMenuFit();
	if (focus)
		tog.focus();
}

function initRunBadge() {
	const tog = $("runbadge_toggle");
	tog.addEventListener("click", () => runBadgeOpen($("runbadge_menu").hidden));
	$("runbadge_state").addEventListener("click", () => {
		runBadgeOpen(false);
		api.show.showTab("machine");
	});
	$("runbadge_pause").addEventListener("click", () => {
		const b = $("dbg_pause").disabled ? $("dbg_continue") : $("dbg_pause");
		if (!b.disabled)
			b.click();
	});
	// Esc closes the menu (the focus back on the toggle); so does a press
	// anywhere outside it.
	document.addEventListener("keydown", (ev) => {
		if (ev.key === "Escape" && !$("runbadge_menu").hidden && !vmHasKeys) {
			ev.preventDefault();
			ev.stopPropagation();
			runBadgeOpen(false, true);
		}
	}, true);
	document.addEventListener("pointerdown", (ev) => {
		if (!$("runbadge_menu").hidden && !(ev.target.closest && ev.target.closest("#runbadge")))
			runBadgeOpen(false);
	}, true);
	state.subscribe(renderRunBadge, ["machine"]);
	renderRunBadge();
}

function drawDebugUi() {
	if (runState !== "jit")
		flushScreen();
	renderEnd();
	renderClock();
	const st = $("dbg_state");
	// Running: the instruction just executed. Paused: the one at EIP, which
	// the rest of the Debugger shows (at a function's first instruction the
	// last one executed is the caller's call).
	const phys = !stepper ? -1 : runState === "paused" ? stepper.nextPhys() : stepper.lastPhys;
	const sym = !stepper ? -1 : runState === "paused" ? (phys >= 0 ? stepper.symbolOf(phys) : -1) : stepper.curSym;
	const where = sym >= 0 ? ", in " + stepper.symName[sym] : "";
	let loc = null;
	if (where && dbgHas() && runState !== "jit") {
		// file:line only: the text would fetch every file stepped through.
		const r = dbgFrames(phys);
		if (r.state === "ok" && r.frames[0].file && r.frames[0].line)
			loc = r.frames[0];
	}
	// The text is cut with an ellipsis, the file:line link after it stays
	// whole (style.css #dbg_state).
	const text = document.createElement("span");
	text.className = "sttext";
	text.textContent = ffActive && ffInfo ? (ffInfo.runTo !== undefined ? "running to " + ffInfo.runTo : "fast-forwarding to " + ffInfo.name) +
		" with the JIT: " + num(stepper.steps - (ffInfo.start || 0)) + " instructions" : { off: "", jit: "running (full speed, JIT)",
		stepped: (entryPhase ? "stepping to the kernel entry" : fastStepping() ? (counting ? "stepping (counting)" : "stepping") :
			"slow motion " + $("dbg_speed").selectedOptions[0].text + behindText()) + where,
		paused: "paused" + where }[runState];
	st.replaceChildren(text);
	if (loc && !ffActive && (runState === "stepped" || runState === "paused")) {
		const at = document.createElement("span");
		at.className = "stloc";
		at.append("\u00a0(", srcLinkEl(loc.file, loc.line, "", phys), ")");
		st.append(at);
	}
	st.dataset.state = ffActive ? "ff" : runState;
	st.title = st.textContent;
	const can = !!stepper;
	$("dbg_pause").disabled = !can || (!ffActive && (runState === "paused" || runState === "off"));
	$("dbg_continue").disabled = !can || ffActive || runState !== "paused" || REV.busy;
	// "VM paused" over the screen, with why (the debugger's last note, or
	// the end of the run) and Continue unless the run has ended.
	const paused = can && !ffActive && runState === "paused";
	$("vm_overlay").hidden = !paused;
	if (paused) {
		// The note keeps its file:line links (a copy of #dbg_status).
		const end = stepper.end ? "end: " + endText() : "", note = dbgStatusText(), said = DBGSTATUS.note;
		if (end && !note.startsWith("Run to ") || !note)
			$("vm_reason").textContent = end;
		else
			$("vm_reason").replaceChildren(...[].concat(said).map((n) => (typeof n === "string" ? n : n.cloneNode(true))));
		$("vm_continue").hidden = !!stepper.end;
	}
	$("dbg_step").disabled = !can || runState === "off" || REV.busy;
	revRender();
	// The views subscribed to "machine" draw now: the run badge
	// (initRunBadge), the EIP's page in the Memory tab (initLegend), the
	// Debugger's registers, CPU state and buttons once it was shown.
	publishMachine();
	renderCounters();
	if (runState !== "stepped")
		renderContext();
}

// Code at EIP is drawn again each time the Debugger tab is shown (showTab).
state.subscribe(() => {
	if (tab === "debugger")
		renderContext();
}, ["tabShows"]);

// The page API's entries of this module (page.js).
provide({ currentPhys, renderContext, speedReset, updateDebugUi, syncSrcToggle, clockMips, flushScreen, FLAG_BITS,
	runBadgeOpen, renderRunBadgePwa });

export { REF_KEY, regsView, renderContext, updateDebugUi, clockMips, finishEntry, speed, speedReset, flushScreen, watchJitEnd,
	endText, fastStepping, highRate, COUNT_LIMIT_MS, SPEED_KEY, renderClock, MIPS_KEY, renderCounters, COUNT_KEY,
	syncSrcToggle, watchPanicText, sampleSpeed, SPEED_SAMPLE_MS, loadVersion, initRunBadge };
