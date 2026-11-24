"use strict";
// Record and replay guest input (J4). Uses the page's state and API (page.js: stepper,
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

import { CLOCK, GUEST } from "./clock.js";
import { VERSION } from "./util.js";
import { controller } from "./ui.jsx";
import { emulator, stepper, runState, image, initrd, emuRam, prefSpeed, startPaused, kernels, setPrefSpeed,
	setStartPaused, setCounting, api } from "./page.js";
import { setCmdlineFromUrl } from "./symimport.js";
import { ffEnabled, ffOnTheWay } from "./fastforward.js";
import { v86ResetHidden } from "./cpustate.js";
import { showHeader } from "./header.js";
import { imageLabel, RB_SUFFIX } from "./buildmap.js";
import { MSET, CLOCKS, SPEEDS, setSettings, cmdline, setCmdline, addRam, setGuestKeys } from "./machtab.js";

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
	while (n < redo.length && inputDue(redo[n], st))
		n++;
	if (n)
		inputRedoFirst(n);
}

// The first n entries of INPUT.redo go in and back into the log.
function inputRedoFirst(n) {
	const done = INPUT.redo.splice(0, n);
	for (const e of done) {
		if (e.keys)
			for (const c of e.keys)
				INPUT.send(c);
		if (e.reset)
			v86ResetHidden(emulator, "save");
		if (e.patch)
			inputPatchWrite(e.patch);
		INPUT.log.push(e);
	}
	renderRunInfo();
	if (done.some((e) => e.patch))
		api.source.hpSync();
}

// R2: back to the checkpoint whose log had n entries: the rest wait in
// INPUT.redo.
function inputRewind(n) {
	INPUT.redo = INPUT.log.splice(n).concat(INPUT.redo);
	INPUT.pending = [];
	const modes = INPUT.log.filter((e) => e.mode);
	INPUT.mode = modes.length ? modes[modes.length - 1].mode : null;
	renderRunInfo();
	api.source.hpSync();
}

// A hot patch (hotpatch.jsx, plan E7) in the log: its bytes written into
// guest memory as the page's writes (the JIT's code of those pages is
// dropped), at its count, when it is made, replayed or redone.
function inputPatchWrite(p) {
	for (const [pa, hex] of p.writes)
		emulator.write_memory(Uint8Array.from(hex.match(/../g), (x) => parseInt(x, 16)), pa);
}

// The machine stopped at the count of a logged patch (stepped back and
// forward again): the patch was made at this very count before the next
// instruction, so it is in RAM and in the log here, and a snapshot taken
// here keeps it (A3b). The entries before it go in with it.
function inputPatchHere(st) {
	const redo = INPUT.redo;
	let last = -1;
	for (let i = 0; i < redo.length && redo[i].at <= st.steps; i++)
		if (redo[i].patch)
			last = i;
	if (last >= 0)
		inputRedoFirst(last + 1);
}

// A patch made now: written and logged at this count. Made after stepping
// back, it starts another timeline: the checkpoints ahead hold RAM without
// it, and restoring one on Continue lost the patch while the log kept it (A3b).
function inputPatch(st, patch) {
	if (st.behind() || INPUT.redo.length)
		api.step.revBranch(st, "a patch was made at instruction " + st.steps.toLocaleString("en") + " after stepping back");
	inputPatchWrite(patch);
	// Also on the real-time clock: the patches the machine has are the log's.
	INPUT.log.push({ at: st.steps, t: CLOCK.now(), patch });
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
		let mode = null, patched = false;
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
			if (e.patch) {
				inputPatchWrite(e.patch);
				patched = true;
			}
			if (e.mode) {
				rp.mode = INPUT.mode = e.mode;
				mode = e.mode;
			}
			INPUT.log.push(e.keys ? { at: e.at, t: e.t, keys: e.keys.slice() } : e.reset ? { at: e.at, t: e.t, reset: true } :
				e.patch ? { at: e.at, t: e.t, patch: e.patch } : { at: e.at, t: e.t, mode: e.mode });
		}
		renderRunInfo();
		if (patched)
			api.source.hpSync();
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
			api.step.revBranch(st, "keys were typed at instruction " + st.steps.toLocaleString("en") + " after stepping back");
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
	api.source.hpSync(); // the patches in the restored memory are its log's
}

// ---- a machine powered on -----------------------------------------------------

// The settings the machine boots with (boot(), before its first instruction).
function runSettings() {
	const p = new URLSearchParams(location.search);
	return {
		kernel: image ? image.name : "", initrd: initrd ? initrd.name : null, ram: emuRam, cmdline: cmdline(),
		clock: api.machine.clockMips() ? String(api.machine.clockMips()) : "real", speed: prefSpeed, count: MSET.count, paused: startPaused,
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
		if (runState === "paused")
			api.show.updateDebugUi(); // the overlay says they wait for Continue
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
	api.source.hpSync();
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
		kernelKey: api.machine.snapKernelKey(),
		// A kernel rebuilt in the page (E8): which build of it.
		...(image && image.build ? { kernelBuild: imageLabel() } : {}),
		// A3f: it ends on the fast-forward to the kernel entry, which no
		// replay can stop inside (runCheck refuses it).
		...(ffOnTheWay(stepper) ? { preEntry: true } : {}),
		log: INPUT.log.map((e) => ({ ...e })), taint: INPUT.taint, end: runEnd(stepper),
	};
}

// A3f: why a run that ends on the fast-forward to the kernel entry cannot
// be replayed (entry: where that boot entered, when known).
const runPreEntryWhy = (at, entry) => "the run ends at instruction " + at.toLocaleString("en") +
	", on the fast-forward to the kernel entry" + (entry ? " (at " + entry.toLocaleString("en") + ")" : "") +
	". The JIT runs that part in slices, so a replay cannot stop at the same instruction. Record a run that goes past the kernel entry";

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
	if (run.preEntry === true)
		return runPreEntryWhy(run.end.at, 0);
	for (let i = 0; i < run.log.length; i++) {
		const e = run.log[i], p = run.log[i - 1];
		if (typeof e.at !== "number" || typeof e.t !== "number" || !(Array.isArray(e.keys) || e.mode === "jit" || e.mode === "stepped" || e.reset === true ||
			(e.patch && Array.isArray(e.patch.writes))) ||
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
	// A kernel rebuilt here (E8): every build has the same name, not the same bytes.
	const key = api.machine.snapKernelKey();
	const built = run.kernel.endsWith(RB_SUFFIX);
	if (built && !(same && (!run.kernelKey || !key || run.kernelKey === key))) {
		runStatus("Cannot replay: this run is of " + (run.kernelBuild || "a build of " + run.kernel.slice(0, -RB_SUFFIX.length) + " made in the page") +
			", and the machine has " + (image ? imageLabel() : "no kernel") + ". Rebuild and boot with that build's edits first: the same edits give the same bytes.", "error");
		return;
	}
	if (!entry && !same) {
		runStatus("Cannot replay: “" + run.kernel + "” is not one of the listed kernels; load that bzImage from disk (Machine tab) first.", "error");
		return;
	}
	if (!CLOCKS.some(([v]) => v === run.clock))
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
	setSettings({ clock: run.clock, count: !!run.count });
	setPrefSpeed(SPEEDS.some(([v]) => v === run.speed) ? run.speed : prefSpeed);
	setStartPaused(!!run.paused);
	setCmdlineFromUrl(run.cmdline);
	setCmdline(run.cmdline);
	const mem = String(run.ram);
	addRam(mem);
	INPUT.next = run;
	runStatus("Booting " + run.kernel + " to replay it...", "running");
	if (same) {
		setSettings({ ram: mem });
		showHeader();
		api.memory.drawMaps();
		await api.machine.boot();
	} else {
		setSettings({ kernel: entry.name });
		await api.machine.loadKernel(entry, mem);
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
	const key = api.machine.snapKernelKey();
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
	setCounting(false);
	api.step.pause(rp.ok ? "Replay finished: the same state as recorded." : "Replay finished: differs from the recording (" + rp.why + ").");
}

// A3f: the fast-forward of a replay's boot reached the kernel entry past
// the run's end (a run of an older page, which did not mark it; newer ones
// are refused before booting). The replay is refused here, at the entry.
// The machine stays as any boot with these settings is there: the count
// stays on and Continue goes on as that boot (a replay's finish turns the
// count off, which ran Debian to its prompt at 1,508,693,176 instead of
// 1,446,429,767).
function replayEndsBeforeEntry() {
	const rp = INPUT.replay, st = stepper;
	if (!rp || rp.done || !st || st.sinceEntry < 0 || rp.run.end.at >= st.sinceEntry)
		return false;
	rp.done = true;
	rp.ok = false;
	rp.why = "it ends before the kernel entry";
	const why = runPreEntryWhy(rp.run.end.at, st.sinceEntry);
	runStatus("Cannot replay " + runLabel(rp.run) + ": " + why + ". The machine is at the kernel entry of a new boot " +
		"with the run's settings; Continue runs that boot.", "error");
	api.step.pause("Replay refused: " + why + ". Continue runs this boot, not the recorded run.");
	return true;
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
	api.step.resume(true);
}

// ---- the page ----------------------------------------------------------------------

// The run box of the Snapshots tab (snapshotspanel.jsx draws it): the
// status of the last download or replay, what this run holds and whether
// Download and Replay can start. renderRunInfo() keeps it, at each change of
// the log (between two slices at most); the box draws only when it differs.
const RUN = controller({
	status: "",
	said: 0,          // status lines written (each anew, also the same text again)
	replay: "",       // the status's state: "running", "match", "differ", "stopped", "error" or ""
	text: "",
	nowable: false,
});

function runStatus(text, state) {
	RUN.status = text;
	RUN.replay = state || "";
	RUN.said++;
	renderRunInfo(true);
}

// The run box (Snapshots tab) and the Machine tab's guest input line;
// changed: the status changed (the box draws then in any case).
function renderRunInfo(changed = false) {
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
	const nowable = runNowable();
	if (changed || text !== RUN.text || nowable !== RUN.nowable) {
		RUN.text = text;
		RUN.nowable = nowable;
		RUN.changed();
	}
	setGuestKeys(!stepper || !INPUT.settings ? "" : INPUT.settings.clock === "real" ? "" :
		rp && !rp.done ? " · keyboard: replaying" : " · keyboard: " + keys + " scancode" + (keys === 1 ? "" : "s") + " recorded");
}

// Download and Replay wait for a replay running to end.
const runNowable = () => !!stepper && !!emulator && !!INPUT.settings && INPUT.settings.clock !== "real" && !replayActive();

function runFileName(run) {
	return run.kernel.replace(/[^\w.-]+/g, "_") + "-" + run.end.at + ".mkbrun.json";
}

function downloadRun() {
	// A3f: as Save (snapshots.js), nothing of a point it could not repeat.
	if (ffOnTheWay(stepper)) {
		runStatus("Not downloaded: this point is on the fast-forward to the kernel entry. The JIT runs that part in slices a replay " +
			"cannot stop inside, so a replay of this run would differ. Press Continue and download after the kernel entry.", "error");
		return;
	}
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

// Replay (the run box's button): this run from boot.
function replayNow() {
	const run = runNow();
	if (run)
		replayRun(run).catch((e) => runStatus("Replay failed: " + e, "error"));
}

// The file input's change: the run in the file picked.
async function replayFile(input) {
	const f = input.files[0];
	if (!f)
		return;
	let run = null;
	try {
		run = JSON.parse(await f.text());
	} catch (e) {
		runStatus("Cannot replay " + f.name + ": not JSON (" + e.message + ").", "error");
		return;
	} finally {
		input.value = "";
	}
	replayRun(run).catch((e) => runStatus("Replay failed: " + e, "error"));
}

function initReplay() {
	renderRunInfo();
}

export { INPUT, replayActive, replayMode, inputRedo, inputRewind, inputFlush, inputRoom, inputMark,
	inputTaint, inputReset, inputRestore, inputBoot, inputPatch, inputPatchHere, runScreenHash, runNow, runCheck, replayRun, replayFinish,
	replayEndsBeforeEntry, replayStop,
	replayAfterStop, runStatus, initReplay, RUN, downloadRun, replayNow, replayFile };
