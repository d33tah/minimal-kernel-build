"use strict";

// Step, Run to (an address, a symbol, a source line), the Debugger's
// controls.

import { realNow, CLOCK } from "./clock.js";
import { num, hex, V86_KERNEL_ADDR } from "./util.js";
import { runState, stepper, symbols, disasm, runToFrom, setRunToFrom, setRunToMoved, setRunTo, setRunToLabel,
	entryPhase, setPrefSpeed, prefSpeed, counting, setCounting, setCountNote, provide } from "./page.js";
import { dbgFrames, dbgLineCodeWait } from "./dbginfo.js";
import { srcLinkEl } from "./srclinks.js";
import { ffActive, ffYield, ffEnabled, ffTarget, codeRange, setFfInfo, fastForward, ffAbort, fastRunTo } from "./fastforward.js";
import { fnSymAt } from "./fnfacts.js";
import { replayActive, replayStop, inputFlush, inputMark, inputRoom, inputTaint, inputPatchHere } from "./replay.js";
import { REV, revNeedsJit, revStepTo, revTick, revRate, revBranch, revReset } from "./reverse.js";
import { scan } from "./scan.js";
import { insnIndex } from "./listing.js";
import { endText, updateDebugUi, finishEntry, SPEED_KEY, renderClock, MIPS_KEY, clockMips,
	renderCounters, COUNT_KEY } from "./debugui.js";
import { showTab } from "./tabs.jsx";
import { navJump } from "./nav.js";
import { afterStop } from "./symview.js";
import { pause, resume } from "./run.js";
import { dbgStatus, runToText, setRunToText } from "./dbgtab.js";
import { setSettings, clockText } from "./machtab.js";

// Pause, and wait for a fast-forward or a fast Run to that is still running
// (they are paused too) to return: a second one started next to it ran two
// loops on one machine (Run to start_kernel during the boot's
// fast-forward went on to the end of the run).
async function pauseAll() {
	if (runState !== "paused" || ffActive)
		await pause();
	while (ffActive)
		await ffYield();
}

async function doStep(n) {
	if (!stepper || REV.busy)
		return;
	await pauseAll();
	if (!stepper)
		return;
	if (stepper.dead()) {
		dbgStatus("The CPU executed hlt with interrupts off: nothing to step.");
		return;
	}
	// The keys typed while paused go in first; the run steps from here (J4).
	if (replayActive())
		replayStop("stopped by Step");
	inputFlush(stepper);
	// Stepped back where the JIT ran (R5): forward on the recorded run from
	// a checkpoint, the JIT stopping at the very instruction.
	if (revNeedsJit(stepper, stepper.steps + n)) {
		await revStepTo(stepper.steps + n, "Step");
		return;
	}
	inputMark(stepper, "stepped");
	const before = stepper.steps;
	// In pieces: a checkpoint for stepping back every so many instructions
	// (R2), and after stepping back the logged keys going in at their counts.
	let reason = "count";
	const t0 = realNow();
	while (stepper.steps - before < n) {
		if (stepper.steps !== before)
			inputFlush(stepper);
		revTick(stepper);
		const s0 = stepper.steps, t1 = realNow();
		reason = stepper.run(Math.min(n - (s0 - before), REV.kFixed || REV.k, inputRoom(stepper)), -1, Math.max(1, 5000 - (t1 - t0)));
		revRate(stepper.steps - s0, realNow() - t1);
		if (reason !== "count")
			break;
		if (realNow() - t0 > 5000) {
			reason = "time";
			break;
		}
	}
	dbgStatus("Stepped " + num(stepper.steps - before) + " instruction" +
		(stepper.steps - before === 1 ? "" : "s") + (reason === "hlt" ? " (halted)" :
			reason === "milestone" ? " (a line appeared on the screen)" : reason === "end" ? " (" + endText() + ")" : reason === "prompt" ? " (at the (initramfs) prompt)" : reason === "time" ? " (5 s limit)" : "") + ".");
	inputPatchHere(stepper);
	scan(false);
	afterStop();
}

// Hex address: physical, or a kernel virtual address (above the kernel's
// virtual-physical offset from symbols.json, 0xC0000000 for Linux).
function parseAddr(text) {
	const v = parseInt(String(text).trim().replace(/^0x/i, ""), 16);
	if (!Number.isFinite(v) || v < 0 || v > 0xffffffff)
		return -1;
	const off = symbols && symbols.symbols.length ? symbols.symbols[0].vaddr - symbols.symbols[0].paddr : 0;
	return off > 0 && v >= off ? v - off : v;
}

// "Stopped at 0x… (Run to …)" for a stop at one of n targets.
function runToStopped(label, n, how) {
	const pa = stepper.nextPhys(), r = dbgFrames(pa);
	const f = r.state === "ok" && r.frames[0].file && r.frames[0].line ? r.frames[0] : null;
	return ["Stopped at " + hex(pa), ...(f ? [" ", srcLinkEl(f.file, f.line, "", pa)] : []), " (Run to " + label +
		(n > 1 ? ", the first of its " + n + " breakpoint addresses reached" : "") + ": " + how + ")."];
}

// The kernel section holding phys (symbols.json), or null.
const sectionAt = (phys) => (symbols && symbols.sections.find((s) => phys >= s.paddr && phys < s.paddr + s.size)) || null;

// Why the CPU can never stop at phys, or "": a target the kernel never
// executes makes Run to run on to the end. Kernel data, and the middle
// of an instruction of the build's disassembly.
function notCode(phys) {
	const sec = sectionAt(phys);
	if (sec && sec.class !== "text")
		return hex(phys) + " is in " + sec.name + " (" + (sec.class === "rodata" ? "read-only data" : sec.class) +
			"), not code: the CPU never executes it, so Run to would not stop";
	if (!sec || !disasm || !disasm.insns || !disasm.insns.length)
		return "";
	const i = insnIndex(phys), ins = disasm.insns[i];
	if (ins && ins[0] === phys)
		return "";
	const prev = disasm.insns[i - 1];
	if (prev && phys < prev[0] + prev[1].length / 2) {
		const name = fnSymAt(prev[0]);
		return hex(phys) + " is inside the instruction at " + hex(prev[0]) + (name ? " (" + name + ")" : "") +
			", not at its start: the CPU never fetches an instruction there";
	}
	return "";
}

// A symbol name: the addresses of the code symbols with that name (a
// static function can have several), or { why } naming similar ones.
function symbolTargets(text) {
	const list = symbols ? symbols.symbols : [];
	const exact = list.filter((s) => s.name === text);
	if (!exact.length) {
		const low = text.toLowerCase();
		const like = [...new Set(list.filter((s) => s.name.toLowerCase().includes(low)).map((s) => s.name))]
			.sort((a, b) => (b.toLowerCase().startsWith(low) - a.toLowerCase().startsWith(low)) || a.length - b.length);
		return { why: "Run to " + text + ": no symbol of that name" + (!symbols ? " (this kernel has no symbols)" : "") +
			(like.length ? "; similar: " + like.slice(0, 5).join(", ") + (like.length > 5 ? " and " + (like.length - 5) + " more" : "") : "") +
			". Run to takes a function name, a hex address or file:line." };
	}
	const code = exact.filter((s) => !notCode(s.paddr));
	if (!code.length)
		return { why: "Run to " + text + ": " + notCode(exact[0].paddr) };
	return { targets: [...new Set(code.map((s) => s.paddr))], label: text };
}

// What the Run to field names: { targets: [physical], label } or { why }.
// file:line (the breakpoint addresses of that line's code: dbgLineCode),
// a symbol name (a word that is not hex, or any exact symbol name not
// starting with 0x), or a hex address (physical or kernel virtual). A
// target that is not code is refused: Run to would run to the end.
async function runToTargets(text) {
	const m = /^(.*\D.*):(\d+)$/.exec(text);
	if (m) {
		dbgStatus("Finding the code of " + text + "…");
		const r = await dbgLineCodeWait(m[1], Number(m[2]));
		if (r.state !== "ok")
			return { why: "Run to " + text + ": " + r.why };
		return r.bps.length ? { targets: r.bps, label: r.path + ":" + r.line } : { why: "Run to " + text + ": no breakpoint address" };
	}
	const isHex = /^(0x)?[0-9a-f]+$/i.test(text);
	if (/^[A-Za-z_.$][\w.$]*$/.test(text) && (!isHex || (symbols && symbols.symbols.some((s) => s.name === text))))
		return symbolTargets(text);
	const phys = isHex ? parseAddr(text) : -1;
	if (phys < 0)
		return { why: "Run to " + text + ": not a function name, hex address or file:line" };
	const why = notCode(phys);
	return why ? { why: "Run to " + why + "." } : { targets: [phys], label: hex(phys) };
}

// "Run to file:line" from the Source panel: the Debugger's own Run to.
function runToLine(where) {
	setRunToText(where);
	navJump(() => showTab("debugger"));
	doRunTo();
}

async function doRunTo() {
	if (!stepper || REV.busy)
		return;
	const want = await runToTargets(runToText());
	if (!want.targets) {
		dbgStatus(want.why);
		updateDebugUi(); // the paused screen's overlay says it too
		return;
	}
	const { targets, label } = want;
	const at = new Set(targets);
	await pauseAll();
	if (!stepper)
		return;
	if (replayActive())
		replayStop("stopped by Run to");
	// Run to from a target goes on to its next hit, stepped or with the JIT.
	setRunToFrom(stepper.nextPhys());
	setRunToMoved(false);
	// Fast run-to (JIT + breakpoints) for kernel code: first to the
	// kernel's entry when it has not run yet (its code is not in memory
	// before the decompressor), then to the targets.
	const t = ffEnabled() && ffTarget();
	// Kernel code in another section than the entry's (.init.text: e.g.
	// start_kernel) gets no breakpoint (that memory is freed and reused
	// after boot), but BIOS, setup and the decompressor still run on the
	// JIT: stepping them took minutes for the Debian sample.
	const inText = (a) => !!(symbols && symbols.sections.some((s) => s.class === "text" && a >= s.paddr && a < s.paddr + s.size));
	const sameSec = t && targets.every((a) => codeRange(t.phys)[0] <= a && a < codeRange(t.phys)[1]);
	if (t && (sameSec || targets.every((a) => inText(a) && a >= V86_KERNEL_ADDR))) {
		setFfInfo(null);
		inputTaint(stepper, "a Run to with the JIT and breakpoints");
		dbgStatus("Running to " + label + " with the JIT...");
		let ok = true;
		if (stepper.sinceEntry < 0 || stepper.nextPhys() < V86_KERNEL_ADDR)
			ok = await fastForward(t.phys, t.name);
		if (ok && entryPhase && stepper.sinceEntry >= 0)
			finishEntry(); // the entry phase ends here, not in driverTick
		if (ok && at.has(runToFrom) && stepper.nextPhys() === runToFrom)
			stepper.run(1, -1, 100);
		if (ok && sameSec && !ffAbort && !at.has(stepper.nextPhys()))
			ok = await fastRunTo(targets, label);
		if (ok && at.has(stepper.nextPhys())) {
			pause(runToStopped(label, targets.length, sameSec ? "JIT and " + (targets.length > 1 ? "breakpoints" : "a breakpoint") :
				"JIT to the kernel entry, then stepped"));
			return;
		}
		if (ffAbort) {
			afterStop();
			return;
		}
	}
	setRunTo(at);
	setRunToLabel(label);
	dbgStatus("Running to " + (targets.length > 1 ? label + " (" + targets.length + " addresses)" : "physical " + hex(targets[0])) + "...");
	resume();
}

// The Machine tab's run controls (machinepanel.jsx; the menu toggle's
// Pause and Continue and the Settings tab use them too).
const machPause = () => pause("Paused.");

function machContinue() {
	dbgStatus("");
	resume(true);
}

// The speed menu (prefSpeed, remembered): a run goes on at that speed.
function chooseSpeed(v) {
	setSettings({ speed: v });
	setPrefSpeed(v);
	try {
		localStorage.setItem(SPEED_KEY, prefSpeed);
	} catch (e) {
		// storage unavailable: the choice lasts until reload
	}
	if (runState === "stepped" || runState === "jit")
		resume(true);
}

// The clock menu (remembered): the reverse history and a recorded run end there.
function chooseClock(v) {
	setSettings({ clock: v });
	try {
		localStorage.setItem(MIPS_KEY, v);
	} catch (e) {
		// no storage: until reload
	}
	if (stepper && runState !== "off") {
		inputTaint(stepper, "the clock was changed to " + clockText());
		revBranch(stepper, "the clock was changed");
		revReset("the clock was changed at instruction " + num(stepper.steps));
	}
	if (runState === "jit") {
		resume(true); // the JIT from the page (virtual) or v86's own loop (real)
	} else {
		CLOCK.set(!!clockMips());
		if (clockMips())
			CLOCK.setMips(clockMips());
	}
	renderClock();
}

// The count box (remembered): off stops counting a run that counts.
function chooseCount(on) {
	setSettings({ count: on });
	try {
		localStorage.setItem(COUNT_KEY, on ? "1" : "0");
	} catch (e) {
		// no storage: until reload
	}
	if (!on && counting) {
		setCounting(false);
		setCountNote("Counting switched off.");
		if (runState === "stepped")
			resume();
	}
	renderCounters();
}

// The page API's entries of this module (page.js).
provide({ runToStopped, runToLine, pauseAll, runToTargets });

export { pauseAll, doStep, doRunTo, machPause, machContinue, chooseSpeed, chooseClock, chooseCount };
