"use strict";

// Snapshots' records (snapshots.js): the list, its constants, the
// snapshot Worker, a record's metadata, parts, chain and names.

import { CLOCK, GUEST } from "./clock.js";
import { kib, hex, $, withV } from "./util.js";
import { image, initrd, stepper, counting, provide } from "./page.js";
import { pageWalk } from "./cpustate.js";
import { INPUT, runNow } from "./replay.js";

const MAX_SNAPSHOTS = 8;  // kept in memory when they cannot be stored
const SNAP_DB = "mkb-snapshots";
const SNAP_STORE = "snaps";
const SNAP_LZ4_LEVEL = 1; // lz4-wasm has one compressor: LZ4's fast one, what `lz4 -1` (the default) runs

let snapshots = [];
const setSnapshots = (v) => { snapshots = v; }; // snapDelete (snapshots.js) drops some
let snapWorkerObj = null;
let snapReqId = 0;
const snapWaiting = new Map();
const SNAP_DELTA_MAX = 31; // deltas in a chain before the next save is full again

const snapCodec = (s) => s.codec || (s.gzip ? "gzip" : "raw");
const SNAP_EXT = { lz4: ".lz4", gzip: ".gz", raw: "" };

// "LZ4 level 1, 75 ms" (or gzip, or none) with the save_state() time.
function snapHow(s) {
	const c = snapCodec(s);
	const how = c === "lz4" ? "LZ4 level " + (s.level || SNAP_LZ4_LEVEL) + ", " + s.packMs + " ms" :
		c === "gzip" ? "gzip level 6, " + s.gzipMs + " ms" : "not compressed";
	return how + "; save_state " + s.saveMs + " ms";
}

const snapFmtSize = (n) => (n >= 1 << 20 ? (n / (1 << 20)).toLocaleString("en", { maximumFractionDigits: 1 }) + " MiB" : kib(n));

// ---- kernel identity ------------------------------------------------------

async function sha256Hex(bytes) {
	if (!(window.crypto && crypto.subtle))
		return null;
	const d = await crypto.subtle.digest("SHA-256", bytes);
	return Array.from(new Uint8Array(d), (b) => b.toString(16).padStart(2, "0")).join("");
}

const snapKernelName = () => (image ? image.name + (initrd ? " + " + initrd.name : "") : "");

const snapRecord = (s) => {
	const { where, ...rec } = s;
	return rec;
};

// ---- save, restore, rename, delete ----------------------------------------

// withRun: also the run up to here (a saved snapshot; the diff's "now" does
// not need it, and its RAM hash would cost a main-thread pass).
function snapMeta(em, withRun = false) {
	const c = em.v86.cpu, r = c.reg32, sr = c.sreg;
	const lin = c.instruction_pointer[0] >>> 0;
	const regs = { EAX: r[0] >>> 0, ECX: r[1] >>> 0, EDX: r[2] >>> 0, EBX: r[3] >>> 0, ESP: r[4] >>> 0, EBP: r[5] >>> 0,
		ESI: r[6] >>> 0, EDI: r[7] >>> 0, EIP: (lin - c.segment_offsets[1]) >>> 0, EFLAGS: c.get_eflags() >>> 0,
		ES: sr[0], CS: sr[1], SS: sr[2], DS: sr[3], FS: sr[4], GS: sr[5],
		CR0: c.cr[0] >>> 0, CR2: c.cr[2] >>> 0, CR3: c.cr[3] >>> 0, CR4: c.cr[4] >>> 0 };
	const walk = pageWalk(c, lin);
	const phys = walk.phys === null ? -1 : walk.phys;
	let sym = "";
	if (stepper && phys >= 0) {
		const i = stepper.symbolOf(phys);
		if (i >= 0)
			sym = stepper.symName[i] + (phys > stepper.symStart[i] ? "+" + hex(phys - stepper.symStart[i]) : "");
	}
	return { icount: c.instruction_counter[0] >>> 0, steps: stepper ? stepper.steps : null, clock: CLOCK.now(),
		clockExact: { base: CLOCK.base, baseSteps: CLOCK.baseSteps, mips: CLOCK.mips }, eip: regs.EIP, phys, sym, regs,
		// stepped: the run goes on stepped from here, as the count makes it
		// (or made it, when stepped back): the JIT would be another timeline,
		// with its timer interrupts at other instructions (A3c). The count
		// decides also where the log has no mode yet (A3d: a run stopped
		// before its first resume was restored under the JIT).
		...(stepper ? { stop: { text: stepper.text.slice(), textSeen: stepper.textSeen, idleMs: stepper.idleMs, ...stepper.stagesNow(),
			stepped: counting || (INPUT.mode === "stepped" && stepper.behind()) } } : {}),
		screen: snapScreen(), ...(CLOCK.virtual ? GUEST.state() : {}), ...(withRun ? { run: runNow() } : {}) };
}

function snapScreen() {
	return Array.from($("screen_text").children, (e) => e.textContent.replace(/\s+$/, ""));
}

// The instruction count a snapshot shows: the stepper's, as on the Machine
// tab, the run badge and the Reverse line (X2). v86's instruction_counter
// (meta.icount) runs a few dozen ahead of it (86 or 87 in the first 1.5 M
// instructions of hello and the minified Linux) and stays in the meta for
// Restore and Replay only. A
// snapshot taken with no stepper (nothing counted) has only v86's.
function snapCount(meta) {
	return typeof meta.steps === "number" ? meta.steps : meta.icount;
}

// A3e: why s can be neither restored nor replayed, or "". Pages before A3e
// saved points on the JIT's fast-forward to the kernel entry; a restored
// machine has no JIT state to repeat those slices, so its run differs
// (Debian's prompt 30 to 100 M instructions early or 67 M late, also from
// the stepped BIOS part at the start of that way, measured at 1.2 M). Their
// records have no stop.jitBeforeEntry. A boot started paused or with
// ?fastforward=0 has no fast-forward; there only a counted run at full speed
// differs, since its record does not say it was stepped and Restore ran it
// under the JIT (the minified Linux's end 289 instructions early; measured
// equal uncounted and at 1M/s). A record from before A3b has no sinceEntry:
// paging still off means the kernel has not started.
const SNAP_REFUSED = "Restore and Replay are off: an older version of this page saved this snapshot before the kernel entry and did not " +
	"keep what a restored machine needs to repeat that part, so the run from here would differ. Boot and save after the kernel entry.";
function snapRefused(s) {
	const m = s.meta, stop = m && m.stop;
	if (!m || (stop && typeof stop.jitBeforeEntry === "boolean"))
		return "";
	const before = stop && typeof stop.sinceEntry === "number" ? stop.sinceEntry < 0 : !!m.regs && !(m.regs.CR0 & 0x80000000);
	const run = m.run;
	// No run kept (before J4): nothing says how it went on, except at the reset vector.
	const differs = !run ? snapCount(m) > 0 : (!run.paused && run.fastforward !== false) || (run.speed === "full" && run.count && !(stop && stop.stepped));
	return before && differs ? SNAP_REFUSED : "";
}

// A stored snapshot as the Worker takes it (snapworker.js); a delta with
// its chain of bases, the full one first.
const snapOne = (s) => ({ key: s.id, blob: s.data, codec: snapCodec(s), size: s.raw });
const snapBlob = (s) => ({ ...snapOne(s), chain: snapChain(s).map(snapOne) });

// The bases of s, the full one first.
function snapChain(s) {
	const out = [];
	for (let x = s; x.delta;) {
		const b = snapById(x.delta.base);
		if (!b || out.length > 4096)
			throw new Error("“" + s.name + "” is a delta of a snapshot that is not here");
		out.unshift(b);
		x = b;
	}
	return out;
}

function snapDepth(s) {
	let n = 0;
	for (let x = s; x && x.delta; x = snapById(x.delta.base))
		n++;
	return n;
}

const snapPagesText = (n) => n.toLocaleString("en") + " page" + (n === 1 ? "" : "s") + " changed";

// The state of s, decompressed in the Worker.
async function snapUnpack(s) {
	return (await snapWorker({ op: "unpack", s: snapBlob(s) })).raw;
}

// ---- the diff -------------------------------------------------------------

function snapWorker(msg, transfer) {
	if (!snapWorkerObj) {
		snapWorkerObj = new Worker(withV("snapworker.js"), { type: "module" });
		snapWorkerObj.onmessage = (ev) => {
			const w = snapWaiting.get(ev.data.id);
			if (!w)
				return;
			snapWaiting.delete(ev.data.id);
			ev.data.error ? w.rej(new Error(ev.data.error)) : w.res(ev.data);
		};
		snapWorkerObj.onerror = (ev) => {
			for (const w of snapWaiting.values())
				w.rej(new Error(ev.message || "the snapshot worker failed"));
			snapWaiting.clear();
			snapWorkerObj = null;
		};
	}
	const id = ++snapReqId;
	return new Promise((res, rej) => {
		snapWaiting.set(id, { res, rej });
		snapWorkerObj.postMessage({ ...msg, id }, transfer || []);
	});
}
// For the tests: one that holds answers back in its place (reverse.js calls
// it through page.js too).
const setSnapWorker = (f) => {
	snapWorker = f;
	provide({ snapWorker });
};

const snapById = (id) => snapshots.find((s) => s.id === id) || null;

export { MAX_SNAPSHOTS, SNAP_DB, SNAP_STORE, SNAP_LZ4_LEVEL, snapshots, setSnapshots, SNAP_DELTA_MAX, snapCodec,
	SNAP_EXT, snapHow, snapFmtSize, sha256Hex, snapKernelName, snapRecord, snapMeta, snapCount, snapBlob, snapDepth,
	snapPagesText, snapUnpack, snapWorker, setSnapWorker, snapById, snapRefused };
