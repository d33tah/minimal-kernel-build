"use strict";
// Record and replay guest input (J4). Uses app.js's globals (stepper,
// emulator, image, runState, resume, pause, ...).
//
// On the virtual clock a run repeats exactly (J1, J2, J3) until someone
// types: v86's keyboard adapter sends each scancode to the PS/2 controller
// (bus "keyboard-code" -> ps2.kbd_send_code) at the moment of the DOM event,
// a moment of wall time. Here the scancodes typed on the virtual clock are
// held and go in only at a boundary: before the next JIT slice (jitLoop),
// before the next stepper slice (driverTick, Step, the exact stops) or when
// the machine is resumed. Each batch is logged with the stepper's count and
// the virtual clock: a halted CPU lets time pass without instructions, so
// the count alone does not name the moment. The log also notes where the
// machine went from the JIT to stepping and back (Step at a pause, a speed
// picked): the two are different timelines (D1). And where v86's hidden JIT
// state was reset (v86ResetHidden: a snapshot saved or restored, stepping
// back): the JIT's slices depend on it, so a replay resets it at the same
// count (R4).
//
// A run: the boot settings (kernel, RAM, command line, clock, speed, count,
// J2's RTC start and seed), that log, and where it ended (count, clock, v86's
// counter, EIP, a hash of guest RAM and of the VGA text). It is downloadable
// (.mkbrun.json), kept in a snapshot's meta and in a position bookmark.
// Replay boots the same kernel with those settings, puts each logged input
// in at the same count and time (stepping stops right there; JIT slices end
// there anyway, as they did when it was recorded), goes on past the stops
// the run went on past (the initramfs prompt), stops at the run's end and
// compares. Keys typed during a replay are ignored.
//
// What a replay cannot follow is marked in the run (taint), from where it
// happened: a Run to with the JIT's breakpoints, the clock changed. With the
// real-time clock nothing is recorded.

const RUN_FORMAT = "mkb-run";
// 2: the gen-v86.sh build of v86 (R4) and its resets in the log. Its JIT
// ends slices elsewhere than npm's build did, so a run of version 1 does
// not replay.
const RUN_VERSION = 2;

const INPUT = {
	log: [],       // { at, t, keys: [scancodes] }, { at, t, mode: "jit" | "stepped" } and { at, t, reset: true }
	pending: [],   // scancodes typed, held for the next boundary
	mode: null,    // the last mode in the log
	taint: null,   // { at, t, why }: from here on a replay from boot is not this run
	settings: null,  // the boot settings of this run
	send: null,      // the PS/2 controller's own kbd_send_code
	replay: null,    // { run, i, mode, late: [], ignored, done, ok, why }
	next: null,      // a run to replay on the next boot
	redo: [],        // R2: entries after the count stepped back to, going in again at their counts
};

// ---- the boundary -------------------------------------------------------------

const replayActive = () => !!INPUT.replay && !INPUT.replay.done;
// The mode the replayed run is in here, or null.
const replayMode = () => (replayActive() ? INPUT.replay.mode : null);

// The moment (count, clock) of entry e has come.
function inputDue(e, st) {
	return st.steps > e.at || (st.steps === e.at && (CLOCK.now() >= e.t || !st.halted()));
}

// R2: after stepping back (reverse.js), the log's entries ahead of the
// machine go in again as it steps forward, at their counts and times.
function inputRedo(st) {
	const redo = INPUT.redo;
	if (!INPUT.send || !redo.length)
		return;
	let n = 0;
	while (n < redo.length && inputDue(redo[n], st)) {
		const e = redo[n++];
		if (e.keys)
			for (const c of e.keys)
				INPUT.send(c);
		if (e.reset)
			v86ResetHidden(emulator, "save");
		INPUT.log.push(e);
	}
	if (n) {
		redo.splice(0, n);
		renderRunInfo();
	}
}

// R2: back to the checkpoint whose log had n entries: the rest wait in
// INPUT.redo.
function inputRewind(n) {
	INPUT.redo = INPUT.log.splice(n).concat(INPUT.redo);
	INPUT.pending = [];
	const modes = INPUT.log.filter((e) => e.mode);
	INPUT.mode = modes.length ? modes[modes.length - 1].mode : null;
	renderRunInfo();
}

// The machine is between two slices: put the inputs that are due in. Returns
// "end" (a replay reached its end), "mode" (a replayed mode change from the
// machine's current one) or null.
function inputFlush(st) {
	if (!st || st !== stepper || !INPUT.send)
		return null;
	const rp = INPUT.replay;
	if (rp && !rp.done) {
		const log = rp.run.log;
		let mode = null;
		while (rp.i < log.length && inputDue(log[rp.i], st)) {
			const e = log[rp.i++];
			const t = CLOCK.now();
			if (st.steps !== e.at || t !== e.t)
				rp.late.push({ at: e.at, t: e.t, nowAt: st.steps, nowT: t });
			if (e.keys)
				for (const c of e.keys)
					INPUT.send(c);
			if (e.reset)
				v86ResetHidden(emulator, "save");
			if (e.mode) {
				rp.mode = INPUT.mode = e.mode;
				mode = e.mode;
			}
			INPUT.log.push(e.keys ? { at: e.at, t: e.t, keys: e.keys.slice() } : e.reset ? { at: e.at, t: e.t, reset: true } :
				{ at: e.at, t: e.t, mode: e.mode });
		}
		renderRunInfo();
		if (rp.run.end && inputDue(rp.run.end, st) && rp.i >= log.length)
			return "end";
		const now = runState === "stepped" ? "stepped" : runState === "jit" ? "jit" : null;
		return mode && now && mode !== now ? "mode" : null;
	}
	if (INPUT.redo.length)
		inputRedo(st);
	if (INPUT.pending.length && CLOCK.virtual) {
		// Typed after stepping back: the run goes another way from here.
		if (st.behind() || INPUT.redo.length)
			revBranch(st, "keys were typed at instruction " + st.steps.toLocaleString("en") + " after stepping back");
		const keys = INPUT.pending.splice(0);
		for (const c of keys)
			INPUT.send(c);
		INPUT.log.push({ at: st.steps, t: CLOCK.now(), keys });
		renderRunInfo();
	}
	return null;
}

// Instructions the stepper may run before the next replayed input or the
// replay's end (at least 1: a halted CPU waits for the clock there).
function inputRoom(st) {
	const rp = INPUT.replay;
	let at = INPUT.redo.length ? INPUT.redo[0].at : Infinity;
	if (rp && !rp.done) {
		at = Math.min(at, rp.i < rp.run.log.length ? rp.run.log[rp.i].at : Infinity);
		if (rp.run.end)
			at = Math.min(at, rp.run.end.at);
	}
	return at === Infinity ? Infinity : Math.max(1, at - st.steps);
}

// The machine goes on in mode ("jit" or "stepped") from here.
function inputMark(st, mode) {
	if (!st || replayActive())
		return;
	if (mode !== INPUT.mode && CLOCK.virtual) {
		INPUT.log.push({ at: st.steps, t: CLOCK.now(), mode });
		INPUT.mode = mode;
	}
}

// From here on a replay from boot would not be this run. A replay running
// stops following its run.
function inputTaint(st, why) {
	if (replayActive()) {
		replayStop("stopped: " + why);
		return;
	}
	if (!INPUT.taint && st)
		INPUT.taint = { at: st.steps, t: CLOCK.now(), why };
	renderRunInfo();
}

// v86's hidden state was reset here (v86ResetHidden, a snapshot saved):
// part of the run. A replay running stops following its run, which had no
// reset here.
function inputReset(st, why) {
	if (!st || st !== stepper)
		return;
	if (replayActive())
		replayStop("stopped: " + why);
	if (CLOCK.virtual) {
		const last = INPUT.log[INPUT.log.length - 1];
		if (!(last && last.reset && last.at === st.steps))
			INPUT.log.push({ at: st.steps, t: CLOCK.now(), reset: true });
	}
	renderRunInfo();
}

// A snapshot was restored (snapshots.js): its run goes on from here, with
// the reset of v86's hidden state the restore did (as the save did: the JIT
// then runs on as it did from the save, R4).
function inputRestore(meta) {
	if (replayActive())
		replayStop("stopped by a snapshot restore");
	const run = meta && meta.run;
	INPUT.pending = [];
	INPUT.redo = [];
	if (run && Array.isArray(run.log) && !runCheck(run)) {
		INPUT.log = run.log.map((e) => ({ ...e }));
		INPUT.taint = run.taint || null;
		const s = {};
		for (const k of Object.keys(INPUT.settings || runSettings()))
			s[k] = run[k];
		INPUT.settings = s;
		const modes = INPUT.log.filter((e) => e.mode);
		INPUT.mode = modes.length ? modes[modes.length - 1].mode : null;
	} else if (stepper && !INPUT.taint) {
		INPUT.taint = { at: stepper.steps, t: CLOCK.now(), why: "a snapshot without an input log was restored" };
	}
	if (stepper && CLOCK.virtual)
		INPUT.log.push({ at: stepper.steps, t: CLOCK.now(), reset: true });
	renderRunInfo();
}

// ---- a machine powered on -----------------------------------------------------

// The settings the machine boots with (boot(), before its first instruction).
function runSettings() {
	const p = new URLSearchParams(location.search);
	return {
		kernel: image ? image.name : "", initrd: initrd ? initrd.name : null, ram: emuRam, cmdline: $("cmdline").value,
		clock: clockMips() ? String(clockMips()) : "real", speed: prefSpeed, count: $("count").checked, paused: startPaused,
		fastforward: ffEnabled(), rtc: new Date(GUEST.rtcStart).toISOString(), seed: GUEST.seed,
		panicbudget: p.get("panicbudget") || null,
	};
}

// emulator-loaded: hold the keyboard's scancodes, start a new log (or the
// replay asked for).
function inputBoot(em) {
	const ps2 = em.v86.cpu.devices.ps2;
	const send = ps2.kbd_send_code;
	INPUT.send = (c) => send.call(ps2, c);
	ps2.kbd_send_code = function (c) {
		if (!CLOCK.virtual || !stepper || em !== emulator)
			return send.call(this, c);
		if (replayActive()) {
			INPUT.replay.ignored++;
			renderRunInfo();
			return;
		}
		INPUT.pending.push(c & 0xff);
	};
	INPUT.log = [];
	INPUT.pending = [];
	INPUT.redo = [];
	INPUT.mode = null;
	INPUT.taint = null;
	INPUT.settings = runSettings();
	INPUT.replay = null;
	if (INPUT.next) {
		INPUT.replay = { run: INPUT.next, i: 0, mode: null, late: [], ignored: 0, done: false };
		INPUT.next = null;
		runStatus("Replaying " + runLabel(INPUT.replay.run) + "...", "running");
	}
	renderRunInfo();
}

// ---- the run ------------------------------------------------------------------

// Two 32-bit lanes over guest RAM (one pass, ~35 ms for 64 MiB).
function runRamHash(cpu) {
	const m = cpu.mem8;
	let a = 0x811c9dc5, b = 0x9747b28c;
	if (m.byteOffset % 4 === 0) {
		const w = new Int32Array(m.buffer, m.byteOffset, m.length >> 2);
		for (let i = 0; i < w.length; i++) {
			const x = w[i];
			a = Math.imul(a ^ x, 0x01000193);
			b = Math.imul(b ^ x, 0x5bd1e995) ^ (b >>> 13);
		}
	} else {
		for (let i = 0; i < m.length; i++) {
			a = Math.imul(a ^ m[i], 0x01000193);
			b = Math.imul(b ^ m[i], 0x5bd1e995) ^ (b >>> 13);
		}
	}
	return (a >>> 0).toString(16).padStart(8, "0") + (b >>> 0).toString(16).padStart(8, "0");
}

// The VGA text window as the guest wrote it (stepper.text) and the start
// address it shows from.
function runScreenHash(st) {
	let h = 0x811c9dc5;
	for (const x of st.text)
		h = Math.imul(h ^ x, 0x01000193);
	const vga = st.cpu.devices.vga;
	h = Math.imul(h ^ (vga && typeof vga.start_address === "number" ? vga.start_address : 0), 0x01000193);
	return (h >>> 0).toString(16).padStart(8, "0");
}

// Where the machine is: what a replay must end at.
function runEnd(st) {
	const c = st.cpu;
	return { at: st.steps, t: CLOCK.now(), icount: c.instruction_counter[0] >>> 0, eip: c.instruction_pointer[0] >>> 0,
		ram: runRamHash(c), screen: runScreenHash(st) };
}

// This run up to now, or null (no machine, or real time).
function runNow() {
	if (!stepper || !emulator || !INPUT.settings || INPUT.settings.clock === "real")
		return null;
	return {
		format: RUN_FORMAT, version: RUN_VERSION, build: VERSION || "", ...INPUT.settings,
		kernelKey: typeof snapKernelKey === "function" ? snapKernelKey() : null,
		log: INPUT.log.map((e) => ({ ...e })), taint: INPUT.taint, end: runEnd(stepper),
	};
}

const runKeys = (run) => run.log.filter((e) => e.keys).reduce((n, e) => n + e.keys.length, 0);
const runLabel = (run) => run.kernel + " to instruction " + run.end.at.toLocaleString("en");

// A run read from a file or storage, or the reason it cannot be replayed.
function runCheck(run) {
	if (!run || typeof run !== "object" || run.format !== RUN_FORMAT)
		return "not a run of this page (no \"format\": \"" + RUN_FORMAT + "\")";
	if (run.version !== RUN_VERSION)
		return "run format version " + run.version + ", this page reads " + RUN_VERSION;
	if (!Array.isArray(run.log) || !run.end || typeof run.end.at !== "number" || !run.kernel)
		return "the run has no log or no end";
	if (run.clock === "real")
		return "the run was on the real-time clock: its inputs were not recorded";
	for (let i = 0; i < run.log.length; i++) {
		const e = run.log[i], p = run.log[i - 1];
		if (typeof e.at !== "number" || typeof e.t !== "number" || !(Array.isArray(e.keys) || e.mode === "jit" || e.mode === "stepped" || e.reset === true) ||
			(p && (e.at < p.at || (e.at === p.at && e.t < p.t))))
			return "the log's entry " + (i + 1) + " is not readable or out of order";
	}
	return "";
}

// ---- replay ---------------------------------------------------------------------

// Boot the run's kernel with its settings and replay it.
async function replayRun(run) {
	const why = runCheck(run);
	if (why) {
		runStatus("Cannot replay: " + why + ".", "error");
		return;
	}
	const entry = kernels.find((k) => k.name === run.kernel);
	const same = image && image.name === run.kernel && (!run.initrd || (initrd && initrd.name === run.initrd));
	if (!entry && !same) {
		runStatus("Cannot replay: “" + run.kernel + "” is not one of the listed kernels; load that bzImage from disk (Machine tab) first.", "error");
		return;
	}
	if (![...$("clock_mips").options].some((o) => o.value === run.clock))
		return runStatus("Cannot replay: this page has no clock of " + run.clock + " instructions per microsecond.", "error");
	// The settings go into the URL (J2's ?rtc= and ?seed=, ?fastforward=,
	// ?panicbudget= are read from it at boot) and the controls.
	const p = new URLSearchParams(location.search);
	const set = (k, v) => (v === null || v === undefined ? p.delete(k) : p.set(k, String(v)));
	set("kernel", run.kernel);
	set("mem", run.ram);
	set("mips", run.clock);
	set("speed", run.speed);
	set("count", run.count ? "1" : "0");
	set("rtc", run.rtc);
	set("seed", run.seed);
	set("paused", run.paused ? "1" : null);
	set("fastforward", run.fastforward === false ? "0" : null);
	set("panicbudget", run.panicbudget);
	set("cmdline", run.cmdline);
	history.replaceState(history.state, "", "?" + p.toString() + location.hash);
	$("clock_mips").value = run.clock;
	prefSpeed = [...$("dbg_speed").options].some((o) => o.value === run.speed) ? run.speed : prefSpeed;
	$("count").checked = !!run.count;
	startPaused = !!run.paused;
	cmdlineFromUrl = run.cmdline;
	$("cmdline").value = run.cmdline;
	const mem = String(run.ram);
	if (![...$("memory").options].some((o) => o.value === mem))
		$("memory").add(new Option(kib(run.ram), mem));
	INPUT.next = run;
	runStatus("Booting " + run.kernel + " to replay it...", "running");
	if (same) {
		$("memory").value = mem;
		showHeader();
		drawMaps();
		await boot();
	} else {
		$("kernel").value = entry.name;
		await loadKernel(entry, mem);
	}
}

// The replay went past its run's end: compare with what was recorded.
function replayFinish() {
	const rp = INPUT.replay, st = stepper;
	if (!rp || rp.done || !st)
		return;
	rp.done = true;
	const want = rp.run.end, got = runEnd(st);
	const names = { at: "instruction count", t: "virtual clock", icount: "v86's counter", eip: "EIP", ram: "RAM", screen: "screen" };
	const diff = Object.keys(names).filter((k) => got[k] !== want[k]);
	const key = typeof snapKernelKey === "function" ? snapKernelKey() : null;
	if (rp.run.kernelKey && key && rp.run.kernelKey !== key)
		diff.unshift("kernel");
	rp.ok = diff.length === 0 && rp.late.length === 0;
	rp.why = rp.ok ? "" : diff.map((k) => names[k] || k).join(", ") + (rp.late.length ? (diff.length ? "; " : "") + rp.late.length + " input" +
		(rp.late.length === 1 ? " not at its recorded moment" : "s not at their recorded moments") : "");
	const n = runKeys(rp.run);
	const text = rp.ok ?
		"Replay matched: " + n + " scancode" + (n === 1 ? "" : "s") + " at the recorded counts, and at instruction " + got.at.toLocaleString("en") +
			" the same EIP, RAM and screen as recorded." :
		"Replay differs from the recording (" + rp.why + ") at instruction " + got.at.toLocaleString("en") + ".";
	runStatus(text + (rp.ignored ? " " + rp.ignored + " scancode" + (rp.ignored === 1 ? "" : "s") + " typed during the replay were ignored." : ""),
		rp.ok ? "match" : "differ");
	counting = false;
	pause(rp.ok ? "Replay finished: the same state as recorded." : "Replay finished: differs from the recording (" + rp.why + ").");
}

// The replay stops following its run (a Step, Run to, Restore, a clock
// change meanwhile); the machine goes on as a new recording from here.
function replayStop(why) {
	const rp = INPUT.replay;
	if (!rp || rp.done)
		return;
	rp.done = true;
	rp.ok = false;
	rp.why = why;
	runStatus("Replay " + why + " at instruction " + (stepper ? stepper.steps.toLocaleString("en") : "?") + ".", "stopped");
	INPUT.taint = { at: stepper ? stepper.steps : 0, t: CLOCK.now(), why: "replay " + why };
}

// The machine stopped by itself (a prompt, an end): the run being replayed
// went on from here, so go on (Continue) unless its end is here.
function replayAfterStop() {
	if (!replayActive() || !stepper)
		return;
	const rp = INPUT.replay;
	if ((rp.run.end && inputDue(rp.run.end, stepper) && rp.i >= rp.run.log.length) || stepper.dead()) {
		replayFinish();
		return;
	}
	resume(true);
}

// ---- the page ----------------------------------------------------------------------

function runStatus(text, state) {
	const el = $("run_status");
	el.textContent = text;
	el.dataset.replay = state || "";
	renderRunInfo();
}

// The run box (Snapshots tab) and the Machine tab's guest input line.
function renderRunInfo() {
	const el = $("run_text");
	if (!el)
		return;
	const rp = INPUT.replay;
	const keys = INPUT.log.filter((e) => e.keys).reduce((n, e) => n + e.keys.length, 0);
	const switches = Math.max(0, INPUT.log.filter((e) => e.mode).length - 1);
	let text;
	if (!stepper || !INPUT.settings)
		text = "No machine running.";
	else if (INPUT.settings.clock === "real")
		text = "The machine runs on the real-time clock: typed keys go in when they are typed and are not recorded. Pick a virtual clock (Machine tab) and boot again to record a run.";
	else
		text = (rp && !rp.done ? "Replaying: " + rp.i + " of " + rp.run.log.length + " log entries in. " : "") +
			"This run: " + keys + " scancode" + (keys === 1 ? "" : "s") + " recorded at the instruction count where the guest got them" +
			(switches ? ", " + switches + " switch" + (switches === 1 ? "" : "es") + " between the JIT and stepping" : "") + "." +
			(INPUT.taint ? " A replay from boot follows it only to instruction " + INPUT.taint.at.toLocaleString("en") + ": " + INPUT.taint.why + "." : "");
	el.textContent = text;
	$("run_dl").disabled = $("run_replay").disabled = !runNowable();
	const g = $("guest_keys");
	if (g)
		g.textContent = !stepper || !INPUT.settings ? "" : INPUT.settings.clock === "real" ? "" :
			rp && !rp.done ? " · keyboard: replaying" : " · keyboard: " + keys + " scancode" + (keys === 1 ? "" : "s") + " recorded";
}

// Download and Replay wait for a replay running to end.
const runNowable = () => !!stepper && !!emulator && !!INPUT.settings && INPUT.settings.clock !== "real" && !replayActive();

function runFileName(run) {
	return run.kernel.replace(/[^\w.-]+/g, "_") + "-" + run.end.at + ".mkbrun.json";
}

function downloadRun() {
	const run = runNow();
	if (!run)
		return;
	const a = document.createElement("a");
	a.href = URL.createObjectURL(new Blob([JSON.stringify(run)], { type: "application/json" }));
	a.download = runFileName(run);
	a.click();
	setTimeout(() => URL.revokeObjectURL(a.href), 10000);
	runStatus("Downloaded " + a.download + ": " + run.log.length + " log entries, ends at instruction " + run.end.at.toLocaleString("en") + ".", "");
}

async function replayFile() {
	const f = $("run_file").files[0];
	if (!f)
		return;
	let run = null;
	try {
		run = JSON.parse(await f.text());
	} catch (e) {
		runStatus("Cannot replay " + f.name + ": not JSON (" + e.message + ").", "error");
		return;
	} finally {
		$("run_file").value = "";
	}
	replayRun(run).catch((e) => runStatus("Replay failed: " + e, "error"));
}

function initReplay() {
	$("run_dl").addEventListener("click", downloadRun);
	$("run_replay").addEventListener("click", () => {
		const run = runNow();
		if (run)
			replayRun(run).catch((e) => runStatus("Replay failed: " + e, "error"));
	});
	$("run_file").addEventListener("change", replayFile);
	renderRunInfo();
}
