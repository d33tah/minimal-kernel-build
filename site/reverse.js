"use strict";
// Reverse execution, stepped (R2) and at JIT speed (R5). Uses app.js's globals (emulator,
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
// R5: while the JIT runs (jitLoop) the page keeps checkpoints too: at the
// start of each stretch the JIT runs and then every REV_JIT_MS of JIT
// running (?revjitk= sets a fixed count instead), each a save_state() and
// the reset of v86's hidden state (v86ResetHidden), logged with the run as
// a snapshot's is (R4): from a checkpoint the JIT then runs again as it ran
// the first time. Which stretches of the run the JIT ran and which were
// stepped is kept too (REV.segs), so that a re-run runs each the way it ran.
// Inside a stretch the JIT ran, the re-run stops at the very instruction:
// v86-det.patch's budget (jitChunk's until) keeps compiled code from
// entering a basic block that would cross it and interprets the rest with
// v86's counter as compiled code reads the clock from it. Back to looks
// for its addresses only in the stretches that were stepped: the JIT runs
// compiled code that cannot be watched instruction by instruction.
//
// The history starts anew (checkpoints dropped) where the run cannot be
// repeated: a snapshot was restored, the clock changed or runs on real time,
// a fast-forward or a Run to with breakpoints ran the JIT (they restore
// states of their own and write HLTs into memory), a replay ran the JIT, a
// new machine. Going on another way after stepping back (keys typed,
// Continue at JIT speed from inside a stretch, a Run to) drops what was
// ahead (Stepper.branch).

const REV_MAX = 32;        // checkpoints kept
const REV_GROUP = 8;       // a full one, then deltas; the oldest group goes first
const REV_TARGET_MS = 500; // stepping between two checkpoints
const REV_BYTES = 256 << 20; // packed checkpoints in memory at most (the oldest group goes)
const REV_K_MIN = 50000, REV_K_MAX = 20e6;
// Between two JIT checkpoints: about REV_JIT_MS of JIT running, estimated
// from what the JIT did (instructions, calls of compiled code: v86-det.patch's
// jit_stats), so that a run puts its checkpoints, and the resets that come
// with them, at the same counts every time (measured time would not). The
// weights are a rough fit on Debian in Chromium (d-claude): the estimate
// came out 0.5 to 2 times the time measured in 25 M and 50 M stretches of
// the boot and the initramfs. Each checkpoint costs the JIT its compiled
// code (the reset) and a save_state() (20 to 55 ms): with 1.5 s the det
// JIT's speed against v86's own loop (suite jitdet) went from about 1.0 to
// 0.88, under the suite's 0.9; so 3 s.
const REV_JIT_MS = 3000;
const REV_JIT_MS_PER_INSN = 4e-6, REV_JIT_MS_PER_CALL = 1.2e-3;
const REV_JIT_KMAX = 500e6;  // and at most this many instructions

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
	segs: [],       // R5: { at, kind: "jit" | "step" }: how the run went on from count at
	jitMs: 0,       // JIT running since the last checkpoint (estimated, REV_JIT_MS)
	kJit: 0,        // ?revjitk=: a JIT checkpoint every this many instructions instead
	rerun: false,   // a reverse operation runs the JIT or the stepper again
	inJit: 0,       // jitChunk's own stepping of a halted CPU (part of the JIT's stretch)
};

{
	const k = Number(new URLSearchParams(location.search).get("revk"));
	if (k >= 1)
		REV.kFixed = Math.floor(k);
	const kj = Number(new URLSearchParams(location.search).get("revjitk"));
	if (kj >= 1)
		REV.kJit = Math.floor(kj);
}

const revKText = () => (REV.kFixed ? REV.kFixed : REV.k).toLocaleString("en");

// " every k stepped, n at JIT speed (every ...)" for the history line.
function revEveryText() {
	const jit = REV.cps.filter((c) => c.jit).length, stepped = REV.cps.length - jit;
	const parts = [];
	if (stepped || !jit)
		parts.push((jit ? stepped + " stepped, " : "") + "every " + revKText() + " stepped instructions");
	if (jit)
		parts.push(jit + " at JIT speed, " + (REV.kJit ? "every " + num(REV.kJit) + " instructions" : "about every " + (REV_JIT_MS / 1000).toLocaleString("en") + " s of JIT"));
	return " (" + parts.join("; ") + ")";
}

// A new machine, a snapshot restored, the clock changed: no history.
function revReset(why) {
	REV.cps = [];
	REV.segs = [];
	REV.jitMs = 0;
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

// jit: a JIT checkpoint (R5): v86's hidden state is reset after the save,
// as a restore of it resets it, and the reset is logged with the run.
function revCapture(st, jit = false) {
	const t0 = realNow();
	let raw;
	try {
		raw = emulator.v86.save_state(); // synchronous in v86 0.5.471 (the emulator's is an async wrapper)
	} catch (e) {
		REV.why = "save_state failed: " + ((e && e.message) || e);
		return false;
	}
	if (jit) {
		v86ResetHidden(emulator, "save");
		inputReset(st, "a reverse checkpoint was taken");
		REV.jitMs = 0;
	}
	const prev = REV.cps[REV.cps.length - 1];
	const sinceFull = prev ? REV.cps.length - 1 - REV.cps.map((c) => c.full).lastIndexOf(true) : -1;
	const full = !prev || sinceFull >= REV_GROUP - 1;
	const cp = {
		key: "rev" + (++REV.seq), steps: st.steps, full,
		clock: { base: CLOCK.base, baseSteps: CLOCK.baseSteps, mips: CLOCK.mips },
		guest: GUEST.state(), icount: st.cpu.instruction_counter[0] >>> 0, text: st.text.slice(), textSeen: st.textSeen,
		idleMs: st.idleMs, irq0: st.irq0, logLen: INPUT.log.length, saveMs: realNow() - t0, jit,
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
	return true;
}

// ---- R5: the JIT's stretches -------------------------------------------------

// The run goes on from st's count by the JIT ("jit") or stepping ("step"):
// noted where that changes. Not while a reverse operation runs it again,
// nor while stepped back (what is ahead is the recorded run).
function revMark(st, kind) {
	if (REV.rerun || REV.inJit || st !== REV.st || st !== stepper)
		return;
	if (st.behind()) {
		// Stepping from inside a stretch the JIT ran: stepping is another
		// timeline (D1), so the run goes another way from here.
		if (kind === "step" && revSegAt(st.steps).kind === "jit")
			revBranch(st, "stepped at instruction " + num(st.steps) + " inside a stretch the JIT ran");
		else
			return;
	}
	const segs = REV.segs, last = segs[segs.length - 1];
	if (last && last.kind === kind)
		return;
	if (last && last.at === st.steps) {
		segs.pop();
		const prev = segs[segs.length - 1];
		if (!prev || prev.kind !== kind)
			segs.push({ at: st.steps, kind });
		return;
	}
	segs.push({ at: st.steps, kind });
}

// The stretch count at is in: { kind, at, end } (end: where the next one
// starts, Infinity for the last). Before the first: stepped.
function revSegAt(at) {
	const segs = REV.segs;
	let i = segs.length - 1;
	while (i >= 0 && segs[i].at > at)
		i--;
	return { kind: i >= 0 ? segs[i].kind : "step", at: i >= 0 ? segs[i].at : -Infinity, end: i + 1 < segs.length ? segs[i + 1].at : Infinity };
}

// Whether the JIT ran any of [from, to).
function revJitIn(from, to) {
	for (let s = revSegAt(from); s.at < to; s = revSegAt(s.end)) {
		if (s.kind === "jit")
			return true;
		if (s.end === Infinity)
			break;
	}
	return false;
}

// Instructions the stepper may run while stepped back before the next
// stretch the JIT ran (Infinity when none is ahead).
function revStepRoom(st) {
	if (!st.behind() || REV.st !== st)
		return Infinity;
	for (let s = revSegAt(st.steps); s.end < st.ahead; s = revSegAt(s.end))
		if (revSegAt(s.end).kind === "jit")
			return Math.max(1, s.end - st.steps);
	return Infinity;
}

// Before a JIT slice of jitLoop: a checkpoint at the start of each stretch
// the JIT runs (the hidden state reset there, so that a re-run from an
// earlier checkpoint finds the JIT as it was) and every REV_JIT_MS of
// JIT running. Not during a replay (its log has no resets here) nor a
// fast-forward.
function revJitTick(st) {
	if (REV.st !== st)
		revReset("");
	if (!revCan(st) || ffActive || replayActive() || jitPending(st))
		return;
	// The JIT after stepping back: another way from here (as revJit says),
	// with a checkpoint where it starts.
	if (st.behind() || INPUT.redo.length)
		revBranch(st, "the JIT ran at instruction " + num(st.steps) + " after stepping back");
	const last = REV.cps[REV.cps.length - 1], seg = REV.segs[REV.segs.length - 1];
	const due = !last || !seg || seg.kind !== "jit" || last.steps < seg.at ||
		(REV.kJit ? st.steps - last.steps >= REV.kJit : REV.jitMs >= REV_JIT_MS || st.steps - last.steps >= REV_JIT_KMAX);
	if (due && !(last && last.steps === st.steps && last.jit))
		revCapture(st, true);
}

// The JIT is about to run a slice (jitChunk): the history goes on with it,
// or starts anew where the JIT does what cannot be repeated.
function revJit(st) {
	if (REV.rerun)
		return;
	if (REV.st !== st)
		revReset("");
	if (ffActive || replayActive() || !CLOCK.virtual) {
		if (REV.cps.length)
			revReset((ffActive ? "a fast-forward or Run to with breakpoints" : replayActive() ? "a replay" : "the real-time clock") +
				" ran the JIT at instruction " + num(st.steps));
		return;
	}
	if (st.behind() || INPUT.redo.length)
		revBranch(st, "the JIT ran at instruction " + num(st.steps) + " after stepping back");
	revMark(st, "jit");
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
	// A checkpoint ahead of the machine (R5: forward on the recorded run):
	// the log's entries before it come back from INPUT.redo first.
	if (INPUT.log.length < cp.logLen)
		INPUT.log.push(...INPUT.redo.splice(0, cp.logLen - INPUT.log.length));
	inputRewind(cp.logLen);
	// v86Restore reset v86's hidden state (its JIT, the TSC's
	// extrapolation): logged at the checkpoint's count, so a replay resets
	// it there too and the JIT from here repeats (R4). The reset of an
	// earlier step back to this checkpoint is not ahead of it.
	while (INPUT.redo.length && INPUT.redo[0].reset && INPUT.redo[0].at === cp.steps)
		INPUT.redo.shift();
	const last = INPUT.log[INPUT.log.length - 1];
	if (CLOCK.virtual && !(last && last.reset && last.at === cp.steps))
		INPUT.log.push({ at: cp.steps, t: CLOCK.now(), reset: true });
	st.rewind(cp.steps, from);
}

// One slice of the JIT again, to until at most (R5), the logged inputs
// due going in first. Returns the instructions it ran.
async function revJitSlice(st, until) {
	inputRedo(st);
	if (jitPending(st))
		await jitFlush(st);
	const next = INPUT.redo.length ? INPUT.redo[0].at : Infinity;
	const s0 = st.steps;
	REV.rerun = true;
	try {
		jitChunk(st, false, Math.min(until, next > st.steps ? next : until));
	} finally {
		REV.rerun = false;
	}
	st.catchUp();
	return st.steps - s0;
}

// Run forward to count target again, each stretch the way it ran (the JIT
// to the very instruction, or stepping), the logged keys and resets going
// in at their counts; with targets (a Set of addresses), the stepped
// stretches stop at them: returns the last hit before target (-1 for none)
// and the instructions the JIT ran meanwhile. Yields now and then.
async function revForward(st, target, targets = null, what = "Stepping back") {
	let t = realNow(), hit = -1, leave = -1, jit = 0;
	while (st.steps < target) {
		const seg = revSegAt(st.steps), end = Math.min(target, seg.end);
		if (seg.kind === "jit") {
			const s0 = st.steps;
			const ran = await revJitSlice(st, end);
			jit += ran;
			if (!ran && st.steps === s0 && !st.halted())
				throw new Error("the JIT did not get on at instruction " + num(st.steps));
			if (st.dead())
				break;
		} else {
			inputRedo(st);
			REV.rerun = true;
			let r;
			try {
				r = st.run(Math.min(end - st.steps, inputRoom(st)), targets || -1, 50, targets ? leave : undefined);
			} finally {
				REV.rerun = false;
			}
			leave = -1;
			if (r === "addr" && targets) {
				hit = st.steps;
				leave = st.nextPhys();
			} else if (r === "hlt" && st.dead()) {
				break;
			}
		}
		if (realNow() - t > 200) {
			revStatus(what + "... at " + num(st.steps) + " of " + num(target), "busy");
			await new Promise((res) => setTimeout(res, 0));
			if (st !== stepper)
				throw new Error("the machine was replaced");
			t = realNow();
		}
	}
	return { hit, jit };
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
	return "no reverse history yet: it starts when the page steps the CPU or runs the JIT on the virtual clock" + (REV.why ? " (" + REV.why + ")" : "");
}

// Step back n instructions (to the history's start at most).
function revStepBack(n) {
	return revRun("Step back", async (st) => {
		const from = st.steps, start = REV.cps[0].steps;
		const target = Math.max(start, from - n);
		if (target >= from)
			return "Step back: the reverse history starts here, at instruction " + num(start);
		let to = target, cp = revFind(to);
		await revRestore(st, cp, from);
		let f = await revForward(st, to);
		// R6: v86 runs the instruction after an sti in the same step as the
		// sti (instr_FB), so no run stops between them: a target there ends
		// one past it (Debian's idle "sti; hlt", the initramfs prompt's
		// halt: Step back 1 from the hlt said "Stepped back 0 instructions").
		// Then the instruction before it, the sti (a few tries at most).
		for (let tries = 0; st.steps > to && to > start && tries < 4; tries++) {
			to--;
			cp = revFind(to);
			await revRestore(st, cp, from);
			f = await revForward(st, to);
		}
		const restored = cp.steps;
		return "Stepped back " + num(from - st.steps) + " instruction" + (from - st.steps === 1 ? "" : "s") + " to " + num(st.steps) +
			(target > from - n ? ", the start of the reverse history" : "") +
			(to < target ? " (" + num(target) + " is inside one step of v86: the instruction after an sti runs with it)" : "") +
			": restored the checkpoint at " + num(restored) + ", ran " + num(st.steps - restored) + " again" + revHow(f.jit, st.steps - restored);
	});
}

// How a re-run of n instructions went, jit of them by the JIT.
function revHow(jit, n) {
	if (!jit)
		return "";
	return jit === n ? " with the JIT, stopped exactly here by its instruction budget" :
		" (" + num(jit) + " with the JIT, stopped exactly by its instruction budget, the rest stepped)";
}

// R5: forward again on the recorded run to count target (not past where it
// got to): restore the last checkpoint before it and run to it. Step and
// Continue use it while stepped back where the JIT ran ahead.
function revStepTo(target, what) {
	return revRun(what, async (st) => {
		const from = st.steps, to = Math.min(target, st.ahead);
		if (to <= from)
			return what + ": nothing ahead";
		const cp = revFind(to);
		await revRestore(st, cp, Math.max(st.ahead, from));
		const restored = cp.steps;
		const f = await revForward(st, to, null, what);
		return what + ": forward " + num(st.steps - from) + " to " + num(st.steps) + " on the recorded run" +
			(st.behind() ? "" : ", where it had got to") + ": restored the checkpoint at " + num(restored) + ", ran " +
			num(st.steps - restored) + " again" + revHow(f.jit, st.steps - restored);
	});
}

// Stepped back with a stretch the JIT ran in [st.steps, to): forward on
// the recorded run needs revStepTo (the stepper would run another timeline).
const revNeedsJit = (st, to) => !!st && REV.st === st && st.behind() && revJitIn(st.steps, Math.min(to, st.ahead));

// Back to (reverse continue): the last time before now the CPU was at one
// of targets (Run to's addresses); the history's start when none.
function revBackTo(targets, label) {
	return revRun("Back to " + label, async (st) => {
		const from = st.steps;
		const set = new Set(targets);
		const cps = REV.cps.filter((c) => c.steps < from && !c.failed);
		let ran = 0, jit = 0;
		// The JIT's stretches are crossed without looking (R5).
		const jitNote = () => (jit ? "; not looked for in the " + num(jit) + " instructions the JIT ran" : "");
		for (let i = cps.length - 1; i >= 0; i--) {
			const end = i + 1 < cps.length ? cps[i + 1].steps : from;
			const jitOnly = !(function stepped() {
				for (let s = revSegAt(cps[i].steps); s.at < end; s = revSegAt(s.end)) {
					if (s.kind === "step")
						return true;
					if (s.end === Infinity)
						break;
				}
				return false;
			})();
			if (jitOnly) {
				jit += end - cps[i].steps;
				continue;
			}
			revStatus("Back to " + label + ": looking from instruction " + num(cps[i].steps) + "...", "busy");
			await revRestore(st, cps[i], from);
			const f = await revForward(st, end, set, "Back to " + label);
			ran += st.steps - cps[i].steps;
			jit += f.jit;
			if (f.hit >= 0) {
				await revRestore(st, cps[i], from);
				await revForward(st, f.hit);
				return "Back to " + label + ": the CPU was last at " + hex(st.nextPhys()) + " at instruction " + num(st.steps) +
					" (" + num(from - st.steps) + " back; " + num(ran) + " instructions run again to find it" + jitNote() + ")";
			}
		}
		const first = REV.cps[0];
		await revRestore(st, first, from);
		return "Back to " + label + ": not reached since instruction " + num(first.steps) +
			", where the reverse history starts" + jitNote() + " (the JIT runs compiled code the page cannot watch per instruction); stopped there";
	});
}

// After stepping back, the run goes another way from here (keys typed, the
// JIT, the clock): the checkpoints ahead go and the recordings ahead too.
function revBranch(st, why) {
	if (!st || (!st.behind() && !st.future.length && !INPUT.redo.length))
		return;
	REV.cps = REV.cps.filter((c) => c.steps <= st.steps);
	REV.segs = REV.segs.filter((s) => s.at <= st.steps);
	revTrim();
	INPUT.redo = [];
	st.branch();
	REV.why = why;
	revRender();
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
			(REV.cps.length === 1 ? "" : "s") + revEveryText() + " · " + kib(bytes) +
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
