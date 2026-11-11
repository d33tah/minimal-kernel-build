"use strict";
// Reverse execution in stepped mode (R2). Uses app.js's globals (emulator,
// stepper, runState, pauseAll, afterStop, ...), snapshots.js's Worker
// (snapWorker: R1's page hashes, deltas and LZ4) and replay.js's input log.
//
// While the page steps the CPU on the virtual clock it keeps checkpoints:
// before a slice, when REV.k instructions have run since the last one (about
// half a second of stepping, ?revk= sets it), v86's save_state(), packed in
// the Worker as a delta of the checkpoint before it (R1; every REV_GROUP-th
// one full), and what lives outside v86's state: the stepper's count, the
// clock exactly (base, count and rate, not a rounded time), J2's RTC start
// and generator, v86's instruction counter, the VGA text and lines the
// stepper mirrors, the idle time, the IRQ0 count and the length of the
// input log (J4). The last REV_MAX are kept, in memory only.
//
// Step back n: restore the last checkpoint at or before the count n back
// and step forward to it. The stepper runs those instructions again without
// recording them again (Stepper.rerun: the paths, calls and coverage stay
// as they were at the furthest count, and the ends, prompts and lines on
// the screen come back as the count gets there), and the keys typed in
// between go in again at their counts (replay.js INPUT.redo). Back to
// (reverse continue): from the last checkpoint backwards, run each stretch
// again watching for the Run to addresses; the machine stops at the last
// hit before now. A stepped run on the virtual clock repeats exactly
// (25b6c3a04, J2), so the machine is then as it was at that count before:
// the same registers, RAM and screen.
//
// The history starts anew (checkpoints dropped) where stepping cannot
// repeat the run: the JIT ran (the JIT and stepping are two timelines, D1;
// JIT replay is R3), a snapshot was restored, the clock changed or runs on
// real time, a new machine. Going on another way after stepping back (keys
// typed, the JIT) drops what was ahead (Stepper.branch).

const REV_MAX = 32;        // checkpoints kept
const REV_GROUP = 8;       // a full one, then deltas; the oldest group goes first
const REV_TARGET_MS = 500; // stepping between two checkpoints
const REV_BYTES = 256 << 20; // packed checkpoints in memory at most (the oldest group goes)
const REV_K_MIN = 50000, REV_K_MAX = 20e6;

const REV = {
	st: null,       // the stepper the checkpoints are of
	cps: [],        // { key, steps, clock, guest, icount, text, textSeen, idleMs, irq0, logLen, full, ready, ... }, by count
	seq: 0,
	rate: 4000,     // instructions per ms of stepping (measured), for k
	k: 2e6,         // instructions between checkpoints
	kFixed: 0,      // ?revk=
	busy: false,    // a step back or Back to is running
	why: "",        // why the history starts where it does
	last: null,     // the last reverse operation: { what, ms, ... }
	cache: null,    // { key, raw }: the full state of the checkpoint restored last (Step back again is quicker)
};

{
	const k = Number(new URLSearchParams(location.search).get("revk"));
	if (k >= 1)
		REV.kFixed = Math.floor(k);
}

const revKText = () => (REV.kFixed ? REV.kFixed : REV.k).toLocaleString("en");

// A new machine, a snapshot restored, the clock changed: no history.
function revReset(why) {
	REV.cps = [];
	REV.cache = null;
	REV.why = why || "";
	REV.st = stepper;
	revRender();
}

// Can the page keep checkpoints of st now: stepping on the virtual clock.
function revCan(st) {
	return !!st && st === stepper && !!emulator && emuReady && CLOCK.virtual && CLOCK.stepper === st && !REV.busy;
}

// Stepping speed of a slice (instructions, ms): k is half a second of it.
function revRate(n, ms) {
	if (n < 20000 || ms <= 0)
		return;
	REV.rate = REV.rate ? 0.8 * REV.rate + 0.2 * (n / ms) : n / ms;
	REV.k = Math.min(REV_K_MAX, Math.max(REV_K_MIN, Math.round(REV.rate * REV_TARGET_MS)));
}

// Before a stepped slice: a checkpoint when there is none or k
// instructions ran since the last one. Not while re-running (those
// counts have theirs already).
function revTick(st) {
	if (REV.st !== st)
		revReset("");
	if (!revCan(st) || st.behind())
		return;
	const last = REV.cps[REV.cps.length - 1];
	if (last && st.steps - last.steps < (REV.kFixed || REV.k))
		return;
	if (last && last.steps === st.steps)
		return;
	revCapture(st);
}

// Instructions until the next checkpoint is due (the driver's slices end
// there), Infinity when none is kept.
function revRoom(st) {
	const last = REV.cps[REV.cps.length - 1];
	if (!last || REV.st !== st || !revCan(st) || st.behind())
		return Infinity;
	return Math.max(1, (REV.kFixed || REV.k) - (st.steps - last.steps));
}

function revCapture(st) {
	const t0 = realNow();
	let raw;
	try {
		raw = emulator.v86.save_state(); // synchronous in v86 0.5.471 (the emulator's is an async wrapper)
	} catch (e) {
		REV.why = "save_state failed: " + ((e && e.message) || e);
		return;
	}
	const prev = REV.cps[REV.cps.length - 1];
	const sinceFull = prev ? REV.cps.length - 1 - REV.cps.map((c) => c.full).lastIndexOf(true) : -1;
	const full = !prev || sinceFull >= REV_GROUP - 1;
	const cp = {
		key: "rev" + (++REV.seq), steps: st.steps, full,
		clock: { base: CLOCK.base, baseSteps: CLOCK.baseSteps, mips: CLOCK.mips },
		guest: GUEST.state(), icount: st.cpu.instruction_counter[0] >>> 0, text: st.text.slice(), textSeen: st.textSeen,
		idleMs: st.idleMs, irq0: st.irq0, logLen: INPUT.log.length, saveMs: realNow() - t0,
		base: null, data: null, size: 0, stored: 0, pages: -1, hashes: null,
	};
	cp.ready = (async () => {
		let base = null;
		if (!full) {
			try {
				await prev.ready;
				base = prev.hashes ? { key: prev.key, hashes: prev.hashes } : null;
			} catch (e) {
				base = null;
			}
		}
		const r = await snapWorker({ op: "pack", raw, base }, [raw]);
		cp.data = new Blob([r.data]);
		cp.size = r.rawLen;
		cp.stored = r.data.byteLength;
		cp.base = r.delta ? prev.key : null;
		cp.pages = r.delta ? r.delta.pages : -1;
		cp.hashes = r.hashes;
		// Only the newest one's hashes are needed (the next delta's base).
		for (const c of REV.cps)
			if (c !== cp && c.hashes && c.steps < cp.steps)
				c.hashes = null;
		revTrim();
		revRender();
	})();
	cp.ready.catch((e) => {
		cp.failed = String((e && e.message) || e);
	});
	REV.cps.push(cp);
	revTrim();
	revRender();
}

// The oldest group goes when there are more than REV_MAX checkpoints or
// they take more than REV_BYTES (a delta's base is the one before it, so a
// group goes whole, up to the next full one).
function revTrim() {
	const bytes = () => REV.cps.reduce((n, c) => n + c.stored, 0);
	while (REV.cps.length > REV_MAX || (bytes() > REV_BYTES && REV.cps.length > REV_GROUP)) {
		const next = REV.cps.findIndex((c, i) => i > 0 && c.full);
		if (next <= 0)
			break;
		REV.cps.splice(0, next);
	}
	if (REV.cache && !REV.cps.some((c) => c.key === REV.cache.key))
		REV.cache = null;
}

// The history's first count, or -1.
const revStart = () => (REV.st === stepper && REV.cps.length ? REV.cps[0].steps : -1);

function revFind(target) {
	let best = null;
	for (const c of REV.cps)
		if (c.steps <= target && !c.failed)
			best = c;
	return best;
}

// The checkpoint's chain, the full one first, as the Worker takes it.
async function revBlob(cp) {
	const list = [cp];
	for (let x = cp; x.base;) {
		await x.ready;
		const b = REV.cps.find((c) => c.key === x.base);
		if (!b)
			throw new Error("checkpoint " + x.key + " lost its base");
		list.unshift(b);
		x = b;
	}
	await Promise.all(list.map((c) => c.ready));
	const one = (c) => ({ key: c.key, blob: c.data, codec: "lz4", size: c.size });
	return { ...one(cp), chain: list.slice(0, -1).map(one) };
}

// Put the machine back to checkpoint cp, coming from count from.
async function revRestore(st, cp, from) {
	if (!REV.cache || REV.cache.key !== cp.key)
		REV.cache = { key: cp.key, raw: (await snapWorker({ op: "unpack", s: await revBlob(cp) })).raw };
	const raw = REV.cache.raw.slice(0); // v86 reads the copy
	if (st !== stepper)
		throw new Error("the machine was replaced");
	// The count and the clock first: restore_state sets the TSC offset
	// from the clock it reads (25b6c3a04). The clock's own fields, so that
	// the re-run reads the very same times.
	st.steps = cp.steps;
	CLOCK.base = cp.clock.base;
	CLOCK.baseSteps = cp.clock.baseSteps;
	CLOCK.mips = cp.clock.mips;
	await v86Restore(emulator, raw);
	GUEST.setState(cp.guest);
	st.cpu.instruction_counter[0] = cp.icount;
	st.text.set(cp.text);
	st.textSeen = cp.textSeen;
	st.idleMs = cp.idleMs;
	st.irq0 = cp.irq0;
	st.vgaDirty = false;
	// v86's hidden state (its JIT, TSC extrapolation) is not the first
	// run's now: the JIT from here would not repeat it (R3), as after a
	// snapshot restore.
	INPUT.restored = true;
	inputRewind(cp.logLen);
	st.rewind(cp.steps, from);
}

// Step forward to count target (re-running), the logged keys going in at
// their counts. Yields to the page now and then.
async function revForward(st, target) {
	let t = realNow();
	while (st.steps < target) {
		inputRedo(st);
		const r = st.run(Math.min(target - st.steps, inputRoom(st)), -1, 50);
		if (r === "hlt" && st.dead())
			break;
		if (realNow() - t > 200) {
			revStatus("Stepping back... at " + num(st.steps) + " of " + num(target), "busy");
			await new Promise((res) => setTimeout(res, 0));
			if (st !== stepper)
				throw new Error("the machine was replaced");
			t = realNow();
		}
	}
}

// The hits of targets in [st.steps, end): the last one before end, or -1.
async function revScan(st, targets, end) {
	let hit = -1, leave = -1, t = realNow();
	while (st.steps < end) {
		inputRedo(st);
		const r = st.run(Math.min(end - st.steps, inputRoom(st)), targets, 50, leave);
		leave = -1;
		if (r === "addr") {
			hit = st.steps;
			leave = st.nextPhys();
		} else if (r === "hlt" && st.dead()) {
			break;
		}
		if (realNow() - t > 200) {
			await new Promise((res) => setTimeout(res, 0));
			if (st !== stepper)
				throw new Error("the machine was replaced");
			t = realNow();
		}
	}
	return hit;
}

function revStatus(text, state, ms) {
	const s = $("dbg_status");
	s.textContent = text;
	s.dataset.rev = state;
	if (ms !== undefined)
		s.dataset.revMs = Math.round(ms);
	else
		delete s.dataset.revMs;
}

// Wait for the machine to stop, then run fn(st) with the history locked.
async function revRun(what, fn) {
	if (!stepper || REV.busy)
		return;
	await pauseAll();
	const st = stepper;
	if (!st || REV.st !== st || !REV.cps.length) {
		revStatus(what + ": " + revNoneText() + ".", "none");
		updateDebugUi();
		return;
	}
	if (replayActive())
		replayStop("stopped by " + what);
	REV.busy = true;
	const t0 = realNow();
	updateDebugUi();
	try {
		const text = await fn(st);
		const ms = realNow() - t0;
		REV.last = { what, ms: Math.round(ms), steps: st.steps };
		speedReset();
		flushScreen(); // v86 draws the screen only while it runs
		revStatus(text + " (" + Math.round(ms) + " ms)", "done", ms);
	} catch (e) {
		revStatus(what + " failed: " + ((e && e.message) || e), "error");
	}
	REV.busy = false;
	afterStop();
	revRender();
}

function revNoneText() {
	if (runState === "off" || !stepper)
		return "no machine";
	if (!CLOCK.virtual)
		return "the clock runs on real time, so a run does not repeat: no reverse history (pick a virtual clock)";
	return "no reverse history yet: it starts when the page steps the CPU" + (REV.why ? " (" + REV.why + ")" : "");
}

// Step back n instructions (to the history's start at most).
function revStepBack(n) {
	return revRun("Step back", async (st) => {
		const from = st.steps, start = REV.cps[0].steps;
		const target = Math.max(start, from - n);
		if (target >= from)
			return "Step back: the reverse history starts here, at instruction " + num(start);
		const cp = revFind(target);
		await revRestore(st, cp, from);
		const restored = cp.steps;
		await revForward(st, target);
		return "Stepped back " + num(from - st.steps) + " instruction" + (from - st.steps === 1 ? "" : "s") + " to " + num(st.steps) +
			(target > from - n ? ", the start of the reverse history" : "") +
			": restored the checkpoint at " + num(restored) + ", ran " + num(st.steps - restored) + " again";
	});
}

// Back to (reverse continue): the last time before now the CPU was at one
// of targets (Run to's addresses); the history's start when none.
function revBackTo(targets, label) {
	return revRun("Back to " + label, async (st) => {
		const from = st.steps;
		const set = new Set(targets);
		const cps = REV.cps.filter((c) => c.steps < from && !c.failed);
		let ran = 0;
		for (let i = cps.length - 1; i >= 0; i--) {
			const end = i + 1 < cps.length ? cps[i + 1].steps : from;
			revStatus("Back to " + label + ": looking from instruction " + num(cps[i].steps) + "...", "busy");
			await revRestore(st, cps[i], from);
			const hit = await revScan(st, set, end);
			ran += st.steps - cps[i].steps;
			if (hit >= 0) {
				await revRestore(st, cps[i], from);
				await revForward(st, hit);
				return "Back to " + label + ": the CPU was last at " + hex(st.nextPhys()) + " at instruction " + num(st.steps) +
					" (" + num(from - st.steps) + " back; " + num(ran) + " instructions run again to find it)";
			}
		}
		const first = REV.cps[0];
		await revRestore(st, first, from);
		return "Back to " + label + ": not reached since instruction " + num(first.steps) +
			", where the reverse history starts; stopped there";
	});
}

// After stepping back, the run goes another way from here (keys typed, the
// JIT, the clock): the checkpoints ahead go and the recordings ahead too.
function revBranch(st, why) {
	if (!st || (!st.behind() && !st.future.length && !INPUT.redo.length))
		return;
	REV.cps = REV.cps.filter((c) => c.steps <= st.steps);
	revTrim();
	INPUT.redo = [];
	st.branch();
	REV.why = why;
	revRender();
}

// The JIT runs from here: no stepped history across it (D1, R3).
function revJit(st) {
	if (st && st.behind())
		revBranch(st, "the JIT ran");
	if (REV.cps.length && REV.st === st)
		revReset("the JIT ran at instruction " + num(st.steps));
}

const revBehind = () => !!stepper && stepper.behind();

// The Debugger's reverse buttons and the line about the history.
function revRender() {
	const el = $("rev_info");
	if (!el)
		return;
	const st = stepper, on = !!st && REV.st === st && REV.cps.length > 0;
	const ok = on && runState !== "off" && !REV.busy && !ffActive;
	const back = ok && st.steps > REV.cps[0].steps;
	for (const id of ["dbg_back", "dbg_backn"]) {
		const b = $(id);
		if (b)
			b.disabled = !back;
	}
	if ($("dbg_backto"))
		$("dbg_backto").disabled = !back;
	const bytes = REV.cps.reduce((n, c) => n + c.stored, 0);
	let text;
	if (!st || runState === "off")
		text = "";
	else if (!on)
		text = "Reverse: " + revNoneText() + ".";
	else
		text = "Reverse: back to instruction " + num(REV.cps[0].steps) + " · " + REV.cps.length + " checkpoint" +
			(REV.cps.length === 1 ? "" : "s") + " every " + revKText() + " · " + kib(bytes) +
			(st.behind() ? " · stepped back " + num(st.ahead - st.steps) + ": the recorded call stack, path and Calls go on to " +
				num(st.ahead) : "");
	el.textContent = text;
	el.dataset.cps = on ? REV.cps.length : 0;
	el.dataset.start = on ? REV.cps[0].steps : "";
	el.dataset.ahead = st && st.behind() ? st.ahead : "";
	const tip = back ? "" : " (" + (REV.busy ? "busy" : !on ? revNoneText() : "at the start of the reverse history") + ")";
	if ($("dbg_back"))
		$("dbg_back").title = "Undo the last instruction: restore the checkpoint before it and step forward to it" + tip;
	if ($("dbg_backn"))
		$("dbg_backn").title = "Step back this many instructions" + tip;
	if ($("dbg_backto"))
		$("dbg_backto").title = "Reverse continue: back to the last time the CPU was at this address before now" + tip;
}

function initReverse() {
	$("dbg_back").addEventListener("click", () => revStepBack(1));
	$("dbg_backn").addEventListener("click", () => revStepBack(Math.max(1, Math.floor(Number($("dbg_n").value) || 100))));
	$("dbg_backto").addEventListener("click", async () => {
		if (!stepper || REV.busy)
			return;
		const want = await runToTargets($("dbg_addr").value.trim());
		if (!want.targets) {
			$("dbg_status").textContent = want.why.replace(/^Run to /, "Back to ");
			updateDebugUi();
			return;
		}
		revBackTo(want.targets, want.label);
	});
	revRender();
}
