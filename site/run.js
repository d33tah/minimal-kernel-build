"use strict";

// Running the machine: counting from reset, v86's JIT loop, the stepping
// driver, pause and resume.

import { CLOCK, realNow } from "./clock.js";
import { $, V86_KERNEL_ADDR } from "./util.js";
import { setCounting, countMs, setCountMs, countSteps, setCountSteps, setCountNote, runTo, setRunTo, setPrevRegs,
	setFlashes, stepper, counting, entryPhase, emulator, emuReady, setRunState, runState, jitLoopId, setJitLoopId,
	runToMoved, runToFrom, setRunToMoved, api, runToLabel, provide } from "./page.js";
import { jitPending, jitFlush, JIT, jitChunk, ffActive, setFfAbort, ffResume } from "./fastforward.js";
import { replayMode, inputFlush, replayFinish, inputMark, inputTaint, inputRoom, replayAfterStop, replayActive, inputPatchHere } from "./replay.js";
import { REV, revNeedsJit, revStepTo, revJitTick, revTick, revRoom, revStepRoom, revRate } from "./reverse.js";
import { STEP_CONST } from "./stepper.js";
import { speed, speedReset, updateDebugUi, clockMips, flushScreen, watchJitEnd, endText, finishEntry,
	fastStepping, highRate, COUNT_LIMIT_MS } from "./debugui.js";
import { afterStop, frameUpdate } from "./symview.js";
import { dbgStatus } from "./dbgtab.js";

const { TEXT_PROMPT, TEXT_PANIC } = STEP_CONST;

const SLICE_MS = 25;            // fast stepping: JS time per slice

let driverTimer = null;
let driverChannel = null;
let lastUi = 0;
let uiSteps = -1;        // stepper.steps when the driver last drew the Debugger's state
let lastFrame = 0;
let stepCredit = 0;

function resetCounters() {
	setCounting(false);
	setCountMs(0);
	setCountSteps(0);
	setCountNote("");
	setRunTo(null);
	setPrevRegs(null);
	setFlashes(0);
	$("pages").dataset.flashes = 0;
}

function wantStepped() {
	// A replay steps where its run stepped and runs the JIT where it did.
	const rm = replayMode();
	if (rm && stepper)
		return rm === "stepped";
	// endAhead: the JIT cannot see the stages (the lines, panic()) up to an
	// end this run has recorded or lost by going another way (A3b).
	return !!stepper && (speed() !== "full" || counting || runTo !== null || entryPhase || stepper.endAhead());
}

function stopDriver() {
	clearTimeout(driverTimer);
	driverTimer = null;
	if (driverChannel)
		driverChannel.port1.onmessage = null;
	driverChannel = null;
	if (lastFrame)
		cancelAnimationFrame(lastFrame);
	lastFrame = 0;
}

// Run the VM in whichever way the controls ask for. now: run the first
// instruction at once (Continue, a speed picked: at 1/s it came a second
// later, which looked like a stall); not when the page itself goes on (the
// kernel entry, where the counts from the entry start).
async function resume(now = false) {
	if (!emulator || !emuReady || REV.busy)
		return;
	// Paused on the fast-forward to the entry: it goes on, the same run (A3d).
	if (stepper && runTo === null && !replayActive() && ffResume(stepper))
		return;
	// Stepped back where the JIT ran ahead (R5): forward to where the run
	// had got to first (exactly, from a checkpoint), as stepping would run
	// another timeline. A Run to goes on from here instead (another way).
	if (stepper && runTo === null && revNeedsJit(stepper, Infinity)) {
		await revStepTo(stepper.ahead, "Continue");
		if (!stepper || REV.busy || stepper.behind())
			return;
	}
	jitRunId++; // a running jitLoop stops at its next check
	stopDriver();
	// Keys typed while paused go in here; a replay's inputs (and its mode)
	// that are due here too.
	if (stepper && inputFlush(stepper) === "end") {
		replayFinish();
		return;
	}
	if (wantStepped()) {
		if (emulator.is_running())
			await emulator.stop();
		inputMark(stepper, "stepped");
		setRunState("stepped");
		stepCredit = now ? 1 : 0;
		prevTick = 0;
		CLOCK.set(!!clockMips());
		scheduleDriver();
	} else {
		if (stepper && runState !== "off")
			stepper.markGap();
		if (stepper)
			stepper.noteJit(false);
		if (stepper && clockMips()) {
			// The virtual clock: the page runs the JIT (jitLoop), the same
			// run every time.
			const em = emulator;
			if (em.is_running())
				await em.stop();
			if (em !== emulator)
				return;
			CLOCK.set(true);
			CLOCK.setMips(clockMips()); // from here on (the menu changed while the JIT ran on real time)
			inputMark(stepper, "jit");
			setRunState("jit");
			jitLoop();
		} else {
			// Real time: v86's own loop, with v86's own compiling.
			const em = emulator;
			if (stepper && jitPending(stepper))
				await jitFlush(stepper);
			if (em !== emulator)
				return;
			if (stepper && CLOCK.virtual)
				inputTaint(stepper, "the clock was switched to real time");
			CLOCK.set(false);
			setRunState("jit");
			emulator.run();
		}
	}
	speedReset();
	updateDebugUi();
}

// Full speed on the virtual clock (J1): v86's JIT driven from the page, one
// main_loop() (about 100,000 instructions) at a time, with the clock from
// the instruction count (jitChunk) and compiled code going live only between
// two of them (jitFlush), so a run repeats exactly: same slice ends, same
// timer interrupts, same state at the same count (D1). It yields to the
// page after JIT.yieldMs or JIT.yieldEvery slices, and draws the screen and
// the Debugger meanwhile. A guest that has been idle for a while (halted
// for three quarters of the virtual time over 64 such groups in a row)
// gets a 10 ms sleep after each group: the virtual clock would otherwise
// race through its timer ticks at full CPU (the initramfs shell). Pacing
// and yields take real time only, never guest time.
let jitRunId = 0;  // bumped by resume and pause: a running loop stops
async function jitLoop() {
	const st = stepper, em = emulator, id = ++jitRunId;
	setJitLoopId(id);
	const live = () => st === stepper && em === emulator && runState === "jit" && id === jitRunId;
	const ch = new MessageChannel();
	const yieldNow = () => new Promise((r) => {
		ch.port1.onmessage = () => r();
		ch.port2.postMessage(0);
	});
	const sa = em.screen_adapter; // v86 draws the screen only while it runs
	if (sa)
		sa.continue();
	let idleGroups = 0, drawn = realNow(), screenAt = drawn;
	const end0 = st.end;
	try {
		while (live()) {
			const now = realNow();
			if (now - drawn > 500) { // the count, registers, clock while it runs
				drawn = now;
				updateDebugUi();
			}
			// v86 draws the screen's changed rows in requestAnimationFrame,
			// which this loop's back-to-back yields can starve (the shell's
			// echo stayed off the screen until a pause): draw them here too.
			if (now - screenAt > 33) {
				screenAt = now;
				flushScreen();
			}
			const t0 = realNow(), clock0 = CLOCK.now(), idle0 = st.idleMs;
			for (let n = 0; n < JIT.yieldEvery; n++) {
				if (jitPending(st)) {
					await jitFlush(st);
					if (!live())
						return;
				}
				// Typed keys and a replay's inputs go in between two slices (J4).
				const input = inputFlush(st);
				if (input === "end") {
					replayFinish();
					return;
				}
				if (input === "mode") { // the replayed run stepped from here
					resume(true);
					return;
				}
				// A checkpoint for stepping back (R5); a replayed input due
				// inside the next slice stops the JIT at its very count.
				revJitTick(st);
				const room = inputRoom(st);
				jitChunk(st, false, room === Infinity ? Infinity : st.steps + room);
				if (st.dead()) {
					watchJitEnd();
					return;
				}
				const stop = await jitExactStop(st, live, end0);
				if (stop === "gone")
					return;
				if (stop === "replay-end") {
					replayFinish();
					return;
				}
				if (stop) {
					setCounting(false);
					flushScreen(); // v86 draws the screen only while it runs
					pause(stop === "prompt" ? "at the (initramfs) prompt" : endText()).then(replayAfterStop);
					return;
				}
				if (realNow() - t0 > JIT.yieldMs)
					break;
			}
			const span = CLOCK.now() - clock0;
			idleGroups = span > 0 && st.idleMs - idle0 > 0.75 * span ? idleGroups + 1 : 0;
			if (idleGroups > 64)
				await new Promise((r) => setTimeout(r, 10));
			else
				await yieldNow();
		}
	} finally {
		ch.port1.onmessage = null;
		if (jitLoopId === id) {
			setJitLoopId(0);
			if (sa && !em.is_running())
				sa.pause();
		}
	}
}

// Debian's stops under the JIT (J3), exactly as stepping finds them: the
// panic's last line is written about 250,000 instructions or more after
// "Kernel panic - not syncing" (D0), so from the slice in which that
// appears the stepper runs to the very write (Stepper.run "end"); the
// initramfs prompt stops at the first halt after it, which is a slice's end
// when the slice ended halted, else the stepper runs to the next hlt.
// Returns null (go on), "end", "prompt", "gone" (paused or replaced) or
// "replay-end" (a replay reached its run's end).
// end0: the end the loop started with (Continue after it runs on).
async function jitExactStop(st, live, end0) {
	if (st.end)
		return st.end !== end0 ? "end" : null;
	const prompt = (st.textSeen & TEXT_PROMPT) && !st.prompt;
	if (!prompt && !(st.textSeen & TEXT_PANIC))
		return null;
	if (prompt && st.halted()) {
		st.prompt = { exact: true, ...st.snapshot(st.nextPhys() - 1) }; // the hlt
		return "prompt";
	}
	// The stepper takes over until one of them. A panic without its last
	// line (another kernel's) ends after panicBudget instructions.
	if (st.panicTextFrom === undefined)
		st.panicTextFrom = st.steps;
	// panic() was entered under the JIT: the stepper would take its first
	// return into panic() for the entry.
	if (!prompt && !st.panicEntered)
		st.panicEntered = { jit: true };
	let n = 0;
	for (;;) {
		if (inputFlush(st) === "end")
			return "replay-end";
		const r = st.run(Math.min(1e6, inputRoom(st)), -1, 20);
		if (r === "end" || r === "prompt")
			return r;
		if (r === "hlt" && st.dead()) {
			st.markHalt();
			return "end";
		}
		if (!prompt && st.steps - st.panicTextFrom >= st.panicBudget) {
			st.end = { kind: "panic-budget", ...st.snapshot() };
			return "end";
		}
		if (++n % 4 === 0) {
			flushScreen();
			await new Promise((r) => setTimeout(r, 0));
			if (!live())
				return "gone";
		}
	}
}

async function pause(note) {
	if (!emulator || !emuReady)
		return;
	if (ffActive)
		setFfAbort(true);
	jitRunId++;
	stopDriver();
	if (emulator.is_running())
		await emulator.stop();
	setRunState("paused");
	setRunTo(null);
	speedReset();
	if (entryPhase && stepper && stepper.sinceEntry >= 0)
		finishEntry(); // a Run to (stepped) went past the kernel entry
	// note: text, or nodes (runToStopped's file:line link).
	if (note !== undefined)
		dbgStatus(note);
	if (stepper)
		inputPatchHere(stepper);
	afterStop();
}

function scheduleDriver() {
	if (fastStepping() || highRate()) {
		// As fast as possible: zero-delay message loop, works in background tabs.
		if (!driverChannel) {
			driverChannel = new MessageChannel();
			driverChannel.port1.onmessage = driverTick;
		}
		driverChannel.port2.postMessage(0);
	} else if (document.hidden) {
		// No animation frames in a hidden tab: a timer (the browser runs it
		// at most once a second there), with the step credit of the time
		// that passed.
		driverTimer = setTimeout(driverTick, 100);
	} else {
		lastFrame = requestAnimationFrame((t) => { lastFrame = 0; driverTick(t); });
	}
}

// The tab hidden or shown, or the page frozen and resumed (phones freeze
// background tabs): the pending frame or timer may never come, so schedule
// anew; measure the speed from here.
function onVisibility() {
	prevTick = 0;
	speedReset();
	if (runState === "stepped") {
		stopDriver();
		scheduleDriver();
	}
}

let prevTick = 0;
function driverTick() {
	if (runState !== "stepped" || !stepper)
		return;
	// Typed keys and a replay's inputs go in between two slices (J4); a
	// replay stops each slice at its next input.
	const input = inputFlush(stepper);
	if (input === "end") {
		replayFinish();
		return;
	}
	if (input === "mode") { // the replayed run ran the JIT from here
		resume(true);
		return;
	}
	// A checkpoint for stepping back (R2), also while the count steps a
	// kernel at full speed: Debian reaches its prompt that way (A3b).
	const full = speed() === "full" && runTo === null;
	const keep = !full || (counting && !entryPhase) || stepper.endAhead();
	if (keep)
		revTick(stepper, full);
	const room = Math.min(inputRoom(stepper), keep ? revRoom(stepper, full) : Infinity, revStepRoom(stepper));
	const now = realNow();
	let reason;
	const t0 = now, s0 = stepper.steps;
	if (entryPhase && runTo === null) {
		const at = stepper.sinceEntry >= 0 || stepper.nextPhys() === V86_KERNEL_ADDR;
		reason = at ? "addr" : stepper.run(room, V86_KERNEL_ADDR, SLICE_MS);
		if (reason === "addr") {
			finishEntry();
			scheduleDriver();
			return;
		}
	} else if (runTo !== null) {
		// Each slice checks its first instruction too, except at the
		// start of the Run to (a slice ended by the time budget or a new
		// line on the screen can stop right before a target).
		const before = stepper.steps;
		reason = stepper.run(room, runTo, SLICE_MS, runToMoved ? -1 : runToFrom);
		setRunToMoved(runToMoved || stepper.steps !== before);
	} else if (speed() === "full" || replayActive()) {
		reason = stepper.run(room, -1, SLICE_MS);
	} else {
		// Credit for the real time since the last tick, however long a
		// drawing took (a cap on the time made a 1 s drawing of the listing
		// cost 0.75 s of credit: 1 instruction in 4 s at 1/s); the burst
		// after a long tick is capped at a second's worth.
		const rate = Number(speed());
		const dt = prevTick ? now - prevTick : 16;
		stepCredit = Math.min(stepCredit + rate * dt / 1000, rate + 1);
		const n = Math.floor(stepCredit);
		stepCredit -= n;
		reason = n ? stepper.run(Math.min(n, room), -1, SLICE_MS) : "count";
	}
	prevTick = now;
	revRate(stepper.steps - s0, realNow() - t0);
	if (counting) {
		setCountMs(countMs + (realNow() - t0));
		setCountSteps(countSteps + (stepper.steps - s0));
	}
	if (reason === "addr") {
		pause(api.step.runToStopped(runToLabel, runTo.size, "stepped"));
		return;
	}
	if (reason === "prompt") {
		setCounting(false);
		flushScreen(); // v86 draws the screen only while it runs
		pause((runTo !== null ? "Run to " + runToLabel + ": not reached before the prompt. " : "") + "at the (initramfs) prompt").then(replayAfterStop);
		return;
	}
	if (reason === "hlt" && stepper.dead())
		stepper.markHalt();
	if (reason === "end" || (reason === "hlt" && stepper.dead())) {
		setCounting(false);
		pause((runTo !== null ? "Run to " + runToLabel + ": not reached before the end. " : "") + endText()).then(replayAfterStop);
		return;
	}
	if (counting && fastStepping() && countMs > COUNT_LIMIT_MS) {
		setCounting(false);
		setCountNote("Gave up after " + Math.round(countMs / 1000) + " s of stepping.");
		if (speed() === "full" && runTo === null) {
			updateDebugUi();
			resume();
			return;
		}
	}
	// Slow motion: the Debugger's state when an instruction has executed
	// (or every 250 ms for the clock), the views that follow EIP when
	// frameUpdate says so; not on every animation frame.
	const slow = !fastStepping();
	const ran = stepper.steps !== uiSteps;
	if (slow && !highRate() ? ran || now - lastUi > 250 : now - lastUi > (highRate() ? 100 : 200)) {
		lastUi = now;
		uiSteps = stepper.steps;
		updateDebugUi();
	}
	// Fast stepping (Run to, counting, the way to the entry) follows EIP
	// too, a few times a second: otherwise Code at EIP and the listing
	// box the instruction where the run started while the count goes on.
	frameUpdate(!slow);
	if (reason === "hlt")
		driverTimer = setTimeout(scheduleDriver, slow ? 16 : 1);
	else
		scheduleDriver();
}

// The page API's entries of this module (page.js).
provide({ pause, resume });

export { stopDriver, resetCounters, resume, pause, onVisibility };
