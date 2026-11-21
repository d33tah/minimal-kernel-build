"use strict";
// The Snapshots tab (S1): v86 save_state() snapshots with names, kept in
// IndexedDB per kernel across reloads, and the diff of two of them (or one
// and now). Uses the page's state and API (page.js: emulator, image, initrd, stepper, ...).
//
// A snapshot: { id, seq, name, time, kernel, kernelKey, ram, raw, size,
//   codec, level, saveMs, packMs, meta, data: Blob, where: "stored" | "memory" }.
// codec "lz4" (C1: an LZ4 frame made in snapworker.js, lz4frame.js, with
// LZ4's fast compressor, the lz4 tool's default level 1) or "raw".
// Snapshots stored before C1 have no codec but gzip: true and gzipMs
// (CompressionStream, zlib's default level 6); they are still read.
// meta is read on the page at save time: v86's instruction counter (icount),
// the stepper's count (steps), EIP and its physical address and symbol, the
// registers with get_eflags() (save_state() keeps the arithmetic flags lazy)
// and the VGA text lines, and the run up to here (J4, replay.js: boot
// settings, the input log, where it ends). The diff reads RAM and the registers from the
// state itself (snapworker.js documents the layout) and uses meta for the
// rest. kernelKey: SHA-256 of the bzImage (and of the initrd, if any), so a
// snapshot is offered only for the kernel it was taken with. build (E8,
// buildmap.js): for a kernel rebuilt in the page, which build; a snapshot
// of another build of the kernel shown says so (the same edits rebuilt
// give the same bytes and key again).
//
// Delta snapshots (R1): the page keeps two 32-bit hashes per 4 KiB page of
// the last snapshot saved or restored (snapBase, from the Worker). The next
// save of the same kernel and RAM size stores only the pages whose hashes
// changed plus v86's state without RAM (snapworker.js documents the
// format): delta = { base: id, pages }, raw = that state's length, full =
// the full state's. Restore, Download and the diff rebuild its RAM from the
// chain (the full snapshot, then each delta's pages). Deleting a base folds
// it into its deltas first (they become deltas of its base, or full).
//
// Here: storage, save, restore, rename, delete and what the tab shows
// (SNAP, the rows, the store line, the pickers); snapshotspanel.jsx draws
// it (A1). The list, the records and the snapshot Worker are
// snaprecord.js's, the diff snapdiff.js's.

import { CLOCK, GUEST, realNow } from "./clock.js";
import { kib, hex, $ } from "./util.js";
import { controller } from "./ui.jsx";
import { image, initrd, stepper, emulator, emuReady, emuRam, counting, runState, selectedPage, setCounting,
	setCountNote, tab, kernels, api, provide } from "./page.js";
import { jitPending, jitFlush } from "./fastforward.js";
import { v86ResetHidden, v86Restore } from "./cpustate.js";
import { inputReset, INPUT, inputRestore } from "./replay.js";
import { REV, revReset } from "./reverse.js";
import { setStatus } from "./header.js";
import { snapWorker, sha256Hex, SNAP_STORE, SNAP_DB, snapRecord, snapshots, snapKernelName, snapCount, snapById,
	snapDepth, SNAP_DELTA_MAX, snapMeta, SNAP_LZ4_LEVEL, MAX_SNAPSHOTS, snapPagesText, snapFmtSize, snapBlob,
	SNAP_EXT, snapCodec, setSnapshots, snapHow } from "./snaprecord.js";
import { snapDiff, pageSnapView, renderSnapDiff, setSnapDiff, setPageSnapView } from "./snapdiff.js";
import { buildFamily, recFamily, imageLabel } from "./buildmap.js";

let snapDb = null;        // IDBDatabase, or null when this browser has no storage for the page
let snapDbWhy = "";       // why not
let snapDbOpen = null;    // Promise of snapDb
let snapPersist = null;   // navigator.storage.persist() result
let snapEstimate = null;  // navigator.storage.estimate() result
let snapDelAllTimer = null;
let snapBase = null;      // the last snapshot saved or restored: { id, kernelKey, ram, hashes }

// What the Snapshots tab shows besides the list (snapshotspanel.jsx draws
// it): the status line (restored: the id of the snapshot a Restore
// finished), whether Save can start (ready: boot.js, when the machine is
// loaded; saving: a save runs), the name being edited, Delete all armed.
// said counts the status lines written: each is written anew, also when
// its text is the one before (as the element's textContent was).
const SNAP = controller({ status: "", said: 0, restored: "", ready: false, saving: false, editing: null, delArmed: false });

// The status line says text (drawn with the next changed()).
function snapNote(text) {
	SNAP.status = text;
	SNAP.said++;
}

function snapSay(text) {
	snapNote(text);
	SNAP.changed();
}

// The machine can be saved (boot.js: loaded) or not (a boot starts).
function snapReady(v) {
	SNAP.ready = v;
	renderSnapshots();
}

// The identity of the loaded kernel (and initrd), or null while it is computed.
function snapKernelKey() {
	if (!image)
		return null;
	const want = image.bytes.length + "/" + (initrd ? initrd.bytes.length : 0);
	if (image.snapKey && image.snapKeyFor === want)
		return image.snapKey;
	if (image.snapKeyP && image.snapKeyFor === want)
		return null;
	const img = image, ird = initrd;
	img.snapKeyFor = want;
	img.snapKeyP = (async () => {
		let k = await sha256Hex(img.bytes);
		// No WebCrypto (an http: page that is not localhost): name and size.
		k = k ? "sha256:" + k : "name:" + img.name + ":" + img.bytes.length;
		if (ird) {
			const i = await sha256Hex(ird.bytes);
			k += "+" + (i ? "sha256:" + i : "name:" + ird.name + ":" + ird.bytes.length);
		}
		if (img === image && img.snapKeyFor === want) {
			img.snapKey = k;
			renderSnapshots();
		}
		return k;
	})();
	return null;
}

// ---- storage --------------------------------------------------------------

function snapReq(r) {
	return new Promise((res, rej) => {
		r.onsuccess = () => res(r.result);
		r.onerror = () => rej(r.error);
	});
}

function snapTx(mode, fn) {
	return new Promise((res, rej) => {
		const tx = snapDb.transaction(SNAP_STORE, mode);
		let out;
		tx.oncomplete = () => res(out);
		tx.onerror = () => rej(tx.error);
		tx.onabort = () => rej(tx.error || new DOMException("aborted", "AbortError"));
		out = fn(tx.objectStore(SNAP_STORE));
	});
}

function snapOpen() {
	if (snapDbOpen)
		return snapDbOpen;
	snapDbOpen = new Promise((res) => {
		let req;
		try {
			req = indexedDB.open(SNAP_DB, 1);
		} catch (e) {
			snapDbWhy = (e && e.message) || String(e);
			return res(null);
		}
		req.onupgradeneeded = () => req.result.createObjectStore(SNAP_STORE, { keyPath: "id" });
		req.onsuccess = () => {
			snapDb = req.result;
			res(snapDb);
		};
		// No onblocked: "blocked" comes only with an upgrade while other
		// connections are open, and version 1 upgrades only a database
		// that does not exist yet, which nothing can have open.
		req.onerror = () => {
			snapDbWhy = (req.error && req.error.message) || "IndexedDB refused";
			res(null);
		};
	});
	return snapDbOpen;
}

// Store s; on failure keep it in memory and say why.
async function snapStore(s) {
	if (!(await snapOpen())) {
		s.where = "memory";
		return "";
	}
	try {
		await snapTx("readwrite", (st) => st.put(snapRecord(s)));
		s.where = "stored";
		if (snapPersist === null && navigator.storage && navigator.storage.persist)
			navigator.storage.persist().then((p) => {
				snapPersist = p;
				snapUpdateEstimate();
			}).catch(() => {});
		return "";
	} catch (e) {
		s.where = "memory";
		const quota = e && (e.name === "QuotaExceededError" || /quota/i.test(e.message || ""));
		return quota ? " Not stored: this site's storage quota is full (" + e.name + "). Kept in memory until the page is closed; delete stored snapshots to make room." :
			" Not stored (" + ((e && (e.message || e.name)) || e) + "). Kept in memory until the page is closed.";
	}
}

async function snapUpdateEstimate() {
	try {
		snapEstimate = navigator.storage && navigator.storage.estimate ? await navigator.storage.estimate() : null;
	} catch (e) {
		snapEstimate = null;
	}
	SNAP.changed();
}

async function snapLoad() {
	if (!(await snapOpen())) {
		renderSnapshots();
		return;
	}
	try {
		const all = await snapTx("readonly", (st) => snapReq(st.getAll()));
		const recs = await all;
		const have = new Set(snapshots.map((s) => s.id));
		for (const r of recs)
			if (!have.has(r.id))
				snapshots.push({ ...r, where: "stored" });
		snapshots.sort((a, b) => a.time - b.time || a.seq - b.seq);
	} catch (e) {
		snapDbWhy = "reading IndexedDB failed: " + ((e && e.message) || e);
		snapDb = null;
	}
	renderSnapshots();
	snapUpdateEstimate();
}

function snapDefaultName(meta, time) {
	return snapKernelName() + " @" + snapCount(meta).toLocaleString("en") + (meta.sym ? " " + meta.sym : " " + hex(meta.eip)) +
		" " + new Date(time).toLocaleTimeString("en-GB");
}

async function saveSnapshot() {
	if (!emulator || !emuReady || REV.busy)
		return;
	const em = emulator;
	SNAP.saving = true;
	snapSay("Saving...");
	try {
		snapKernelKey();
		const key = image.snapKeyP ? await image.snapKeyP : null;
		// A delta of the last snapshot saved or restored, if it is of this
		// kernel and RAM size, still there, stored (when this browser
		// stores), and its chain not too long.
		const b = snapBase && snapById(snapBase.id);
		const base = b && snapBase.kernelKey === key && b.kernelKey === key && snapBase.ram === emuRam &&
			(b.where === "stored" || !snapDb) && snapDepth(b) < SNAP_DELTA_MAX ? b : null;
		// Compiled blocks still queued go live first (v86ResetHidden); from
		// the meta to the reset below nothing else runs (no await between).
		while (stepper && jitPending(stepper))
			await jitFlush(stepper);
		if (em !== emulator)
			throw new Error("the machine was replaced");
		const meta = snapMeta(em, true);
		const t0 = realNow();
		let raw = em.v86.save_state(); // synchronous in v86 0.5.471 (the emulator's is an async wrapper)
		const t1 = realNow();
		// The machine goes on from the hidden state a Restore of this
		// snapshot starts from, and the run says so (R4).
		v86ResetHidden(em, "save");
		inputReset(stepper, "a snapshot was saved");
		const rawLen = raw.byteLength;
		// Page hashes, the delta and LZ4 in the Worker; the state is
		// transferred, not copied.
		let data, codec = "lz4", packMs = 0, why = "", delta = null, hashes = null, packedLen = rawLen;
		try {
			const r = await snapWorker({ op: "pack", raw, base: base ? { key: base.id, hashes: snapBase.hashes } : null }, [raw]);
			data = r.data;
			packMs = r.ms;
			hashes = r.hashes;
			packedLen = r.rawLen;
			if (r.delta)
				delta = { base: base.id, pages: r.delta.pages };
		} catch (e) {
			// The worker could not compress (or load the LZ4 module): keep the
			// state raw (taken again if the failed message took it along).
			codec = "raw";
			why = " Not compressed (" + ((e && e.message) || e) + ").";
			data = raw.byteLength ? raw : await em.save_state();
		}
		raw = null;
		const t2 = realNow();
		const time = Date.now();
		const seq = snapshots.reduce((m, s) => Math.max(m, s.seq || 0), 0) + 1;
		const s = {
			id: "s" + time.toString(36) + Math.floor(Math.random() * 36 ** 4).toString(36),
			seq, name: snapDefaultName(meta, time), time, kernel: snapKernelName(), kernelKey: key, ram: emuRam,
			// The build it belongs to (E8): for one built here, its label.
			...(image.build ? { build: imageLabel() } : {}),
			raw: codec === "raw" ? data.byteLength : packedLen, full: codec === "raw" ? data.byteLength : rawLen, delta,
			size: data.byteLength, codec, level: codec === "lz4" ? SNAP_LZ4_LEVEL : 0, data: new Blob([data]),
			saveMs: Math.round(t1 - t0), packMs, packWallMs: Math.round(t2 - t1), meta, where: "memory",
		};
		snapshots.push(s);
		why += await snapStore(s);
		snapBase = hashes ? { id: s.id, kernelKey: key, ram: emuRam, hashes } : null;
		// Without storage only the last few stay.
		const mem = snapshots.filter((x) => x.where === "memory");
		if (mem.length > MAX_SNAPSHOTS)
			await snapDelete(mem.slice(0, mem.length - MAX_SNAPSHOTS), true);
		snapNote("Saved #" + seq + " (" + s.name + "): save_state " + s.saveMs + " ms" +
			(codec === "lz4" ? ", page hashes and LZ4 level " + SNAP_LZ4_LEVEL + " " + packMs + " ms in a Worker" : "") +
			(delta ? "; a delta of #" + base.seq + ": " + snapPagesText(delta.pages) + ", " + snapFmtSize(s.size) +
				" stored instead of a full snapshot." : ".") + why);
	} catch (e) {
		snapNote("save_state failed: " + e);
	}
	SNAP.saving = false;
	renderSnapshots();
	snapUpdateEstimate();
}

async function restoreSnapshot(s) {
	if (REV.busy)
		return;
	if (!emulator || emuRam !== s.ram) {
		snapSay("“" + s.name + "” was taken with " + kib(s.ram) + " of RAM; boot with that size to restore it.");
		return;
	}
	SNAP.restored = "";
	snapSay("Restoring “" + s.name + "”...");
	try {
		// Another timeline: the reverse history (R2) starts anew from the
		// snapshot, with the recordings as they were.
		// The calls open now end where this timeline stops (the furthest
		// count, also when stepped back): closed later, at the snapshot's
		// earlier count, they would end before they began.
		if (stepper) {
			stepper.closeStack(stepper.behind() ? stepper.ahead : stepper.steps);
			stepper.unrewind();
		}
		INPUT.redo = [];
		revReset("a snapshot was restored");
		// A delta comes back as a full state, rebuilt in the Worker, with
		// the page hashes the next save is compared with.
		const r = await snapWorker({ op: "unpack", s: snapBlob(s), hashes: true });
		const raw = r.raw;
		// The stepper's count and the clock go back to the snapshot's before
		// restore_state, which sets the TSC offset from the clock it reads;
		// the PIT and RTC keep absolute times. Otherwise everything after a
		// restore ran shifted by the time between save and Restore (timer
		// interrupts at other instructions). Snapshots from before D1 have
		// no clock.
		// The clock's own fields when the snapshot has them and the rate is
		// the same (R4): time from base + count, not a rounded time, so the
		// run on reads the very same times as the first time.
		if (s.meta && typeof s.meta.clock === "number") {
			if (stepper && typeof s.meta.steps === "number")
				stepper.steps = s.meta.steps;
			const x = s.meta.clockExact;
			if (x && CLOCK.virtual && x.mips === CLOCK.mips && stepper && typeof s.meta.steps === "number") {
				CLOCK.base = x.base;
				CLOCK.baseSteps = x.baseSteps;
				CLOCK.off = s.meta.clock - realNow();
			} else {
				CLOCK.setTime(s.meta.clock);
			}
		}
		await v86Restore(emulator, raw);
		snapBase = r.hashes ? { id: s.id, kernelKey: s.kernelKey, ram: s.ram, hashes: r.hashes } : null;
		// The RTC's start date and the generator's state (J2). Snapshots
		// taken on real time or before J2 have neither: the start date then
		// goes on from the RTC's last update, the date the guest saw last.
		if (s.meta && typeof s.meta.rtcStart === "number")
			GUEST.setState(s.meta);
		else if (CLOCK.virtual)
			GUEST.rtcStart = emulator.v86.cpu.devices.rtc.last_update - CLOCK.now();
		// v86 does not keep its instruction counter in the state: put back the
		// snapshot's, so the count goes on from where it was.
		if (s.meta && emulator.v86)
			emulator.v86.cpu.instruction_counter[0] = s.meta.icount;
		// What the stepper watches for its stops (R4): the text written, the
		// lines seen, and no stop recorded past the snapshot, so the run from
		// here stops where it stopped from there the first time (the
		// (initramfs) prompt), as a replay from boot does.
		if (stepper && s.meta && s.meta.stop && typeof s.meta.steps === "number")
			stepper.restoreStops(s.meta.steps, s.meta.stop);
		// The run goes on from the snapshot's input log (J4).
		inputRestore(s.meta);
		api.step.speedReset();
		if (stepper)
			stepper.jumpForget();
		if (counting || (stepper && stepper.steps)) {
			setCounting(false);
			setCountNote("A snapshot was restored: the counters stopped there.");
		}
		// The Debugger's note (shown over the screen while paused) said why
		// the machine stopped where it was before ("Paused at the reset
		// vector.", a Run to): now it is here because of the Restore.
		$("dbg_status").textContent = "Restored the snapshot “" + s.name + "”.";
		if (runState === "paused" || runState === "stepped")
			api.step.afterStop();
		SNAP.restored = s.id;
		snapSay("Restored “" + s.name + "” (" + s.kernel + ").");
		if (selectedPage >= 0)
			api.show.inspect();
	} catch (e) {
		snapSay("restore_state failed: " + e);
	}
}

async function downloadSnapshot(s) {
	let blob = s.data;
	if (s.delta) {
		// A delta downloads as the full state, rebuilt from its chain.
		try {
			blob = new Blob([(await snapWorker({ op: "export", s: snapBlob(s) })).data]);
		} catch (e) {
			snapSay("Download failed: " + ((e && e.message) || e));
			return;
		}
	}
	const a = document.createElement("a");
	a.href = URL.createObjectURL(blob);
	a.download = s.kernel.replace(/[^\w.-]+/g, "_") + "-" + s.ram / 1024 + "k-" + s.seq + "-" +
		s.name.replace(/[^\w.-]+/g, "_").slice(0, 40) + ".v86state" + SNAP_EXT[snapCodec(s)];
	a.click();
	setTimeout(() => URL.revokeObjectURL(a.href), 10000);
}

async function snapRename(s, name) {
	name = name.trim();
	if (!name || name === s.name)
		return;
	s.name = name;
	if (s.where === "stored")
		snapSay("Renamed." + (await snapStore(s)));
	// The diff and a snapshot's page in the Page tab hold their own copy of
	// the name.
	if (snapDiff) {
		for (const x of [snapDiff.a, snapDiff.b])
			if (x.id === s.id)
				x.name = name;
		renderSnapDiff();
	}
	if (pageSnapView) {
		if (pageSnapView.id === s.id)
			pageSnapView.name = "“" + name + "”";
		if (pageSnapView.againstId === s.id)
			pageSnapView.against = "“" + name + "”";
		if (tab === "page")
			api.show.inspect();
	}
}

// d again without its base x (about to be deleted): a delta of x's base
// with the pages of both, or a full snapshot when x was full.
async function snapFold(d, x) {
	const nb = x.delta ? snapById(x.delta.base) : null;
	const r = await snapWorker({ op: "rebase", s: snapBlob(d), base: nb ? snapBlob(nb) : null });
	Object.assign(d, { data: new Blob([r.data]), size: r.data.byteLength, raw: r.rawLen, full: r.full, codec: "lz4",
		level: SNAP_LZ4_LEVEL, delta: r.delta ? { base: nb.id, pages: r.delta.pages } : null });
	return d.where === "stored" ? snapStore(d) : "";
}

// quiet: no status line (the oldest in-memory snapshots dropped on a save).
async function snapDelete(list, quiet) {
	const ids = new Set(list.map((s) => s.id));
	// Newest first: a delta goes before its base, so only bases of
	// snapshots that stay are folded into them.
	const order = list.slice().sort((a, b) => (b.seq || 0) - (a.seq || 0) || b.time - a.time);
	let folded = 0, why = "";
	for (const x of order) {
		try {
			for (const d of snapshots.filter((y) => y.delta && y.delta.base === x.id && !ids.has(y.id))) {
				why += await snapFold(d, x);
				folded++;
			}
		} catch (e) {
			snapSay("Not deleted: “" + x.name + "” is the base of a delta that could not be rebuilt (" + ((e && e.message) || e) + ").");
			break;
		}
		setSnapshots(snapshots.filter((s) => s !== x));
		if (snapBase && snapBase.id === x.id)
			snapBase = null;
		if (snapDb && x.where === "stored")
			try {
				await snapTx("readwrite", (st) => st.delete(x.id));
			} catch (e) {
				snapSay("Deleting from IndexedDB failed: " + ((e && e.message) || e));
			}
	}
	if (folded && !quiet)
		snapNote("Deleted; " + folded + " delta snapshot" + (folded === 1 ? " was" : "s were") +
			" rebuilt without " + (list.length === 1 ? "it" : "them") + "." + why);
	if (snapDiff && (ids.has(snapDiff.a.id) || ids.has(snapDiff.b.id))) {
		setSnapDiff(null);
		renderSnapDiff();
	}
	renderSnapshots();
	snapUpdateEstimate();
}

// Delete all: the first press arms it for 5 s, the second deletes.
function snapDeleteAll() {
	clearTimeout(snapDelAllTimer);
	if (!SNAP.delArmed) {
		SNAP.delArmed = true;
		SNAP.changed();
		snapDelAllTimer = setTimeout(() => {
			SNAP.delArmed = false;
			SNAP.changed();
		}, 5000);
		return;
	}
	SNAP.delArmed = false;
	SNAP.changed();
	const n = snapshots.length;
	snapDelete(snapshots.slice(), true).then(() => snapSay("Deleted " + n + " snapshot" + (n === 1 ? "" : "s") + "."));
}

function snapSwitch(s) {
	const k = kernels.find((x) => x.name === s.kernel);
	if (!k) {
		snapSay("“" + s.kernel + "” is not one of the listed kernels: load that bzImage from disk " +
			"(Machine tab) with " + kib(s.ram) + " of RAM to restore “" + s.name + "”.");
		return;
	}
	$("kernel").value = k.name;
	if (![...$("memory").options].some((o) => o.value === String(s.ram)))
		$("memory").add(new Option(kib(s.ram), String(s.ram)));
	snapSay("Switching to " + k.name + " with " + kib(s.ram) + "...");
	api.machine.loadKernel(k, String(s.ram)).catch((e) => {
		setStatus(String(e));
		snapSay("Switching to " + k.name + " failed: " + ((e && e.message) || e) + ".");
	});
}

// ---- the table ------------------------------------------------------------

// "match": of this kernel; "build": of another build of it (E8; one made
// in this page has no entry in the kernel list to switch to); "other".
function snapMatch(s) {
	const key = snapKernelKey();
	// "" while the kernel's hash is computed (a row drawn in those milliseconds).
	return !key ? "" : s.kernelKey === key ? "match" : recFamily(s) === buildFamily() && !kernels.some((k) => k.name === s.kernel) ? "build" : "other";
}

// What a row says of a snapshot of another build of this kernel.
const snapOfBuild = (s) => "of " + (s.build || "an earlier build of " + recFamily(s)) + ", not the build running (" + imageLabel() +
	"): Rebuild and boot with the same edits makes the same bytes, and then it can be restored";

// The name of s is being edited (its button became a text field), or
// the edit ended: save it renamed (Enter, leaving the field) or not (Esc).
function snapEditName(s) {
	SNAP.editing = s.id;
	SNAP.changed();
}

function snapEditEnd(s, name) {
	SNAP.editing = null;
	if (name !== null)
		snapRename(s, name);
	SNAP.changed();
}

// The list, the store line and the pickers changed (or the kernel: its
// hash starts here, at its load, as the bookmarks and runs use it too):
// the tab draws them.
function renderSnapshots() {
	snapKernelKey();
	SNAP.changed();
}

// One row of the table: its data attributes, kind and cells.
function snapRow(s) {
	const match = snapMatch(s);
	const ratio = s.raw / s.size;
	const day = new Date(s.time);
	return {
		s, match,
		data: { id: s.id, raw: s.raw, full: s.full || s.raw, stored: s.size, kernel: match, where: s.where, codec: snapCodec(s),
			base: s.delta ? s.delta.base : "", depth: snapDepth(s), pages: s.delta ? s.delta.pages : -1 },
		cells: [
			"#" + s.seq + ", " + (day.toDateString() === new Date().toDateString() ? day.toLocaleTimeString("en-GB") :
				day.toLocaleString("en-GB")),
			s.meta ? "instruction " + snapCount(s.meta).toLocaleString("en") + ", " + (s.meta.sym || "EIP " + hex(s.meta.eip)) : "",
			match === "build" ? s.kernel + ", " + kib(s.ram) + ", " + snapOfBuild(s) :
				(match === "other" ? (recFamily(s) === buildFamily() ? "for the shipped build: " : "for another kernel: ") : "") +
				(s.build && match === "match" ? s.build : s.kernel) + ", " + kib(s.ram),
			snapFmtSize(s.size) + (s.where === "stored" ? ", stored" : ", in memory only") + (s.delta ? "; delta of #" +
				((snapById(s.delta.base) || {}).seq || "?") + ": " + snapPagesText(s.delta.pages) : ""),
			s.raw.toLocaleString("en") + (s.delta && s.full ? " (full state " + s.full.toLocaleString("en") + ")" : ""),
			snapCodec(s) !== "raw" ? ratio.toFixed(1) + " : 1 (" + (100 * s.size / s.raw).toFixed(2) + "%)" : "-",
			snapHow(s),
		],
	};
}

// The store line under Save: its text and data-storage.
function snapStoreView() {
	const stored = snapshots.filter((s) => s.where === "stored");
	const mem = snapshots.length - stored.length;
	const sum = (l) => l.reduce((n, s) => n + s.size, 0);
	const key = snapKernelKey();
	const mine = snapshots.filter((s) => key && s.kernelKey === key);
	if (!snapDb && snapDbOpen)
		return { storage: "none", text: "Snapshots are not stored in this browser (" + (snapDbWhy || "no IndexedDB") + "): they are kept in memory only, " +
			"the last " + MAX_SNAPSHOTS + ", until the page is closed. " +
			(mem ? mem + " in memory, " + snapFmtSize(sum(snapshots)) + "." : "") };
	const nd = stored.filter((s) => s.delta).length;
	const est = snapEstimate && snapEstimate.quota ? " This site uses " + snapFmtSize(snapEstimate.usage || 0) + " of " +
		snapFmtSize(snapEstimate.quota) + " the browser allows it" + (snapPersist === true ? ", kept persistently." :
		snapPersist === false ? "; the browser may evict it when the disk fills up." : ".") : "";
	return { storage: "indexeddb", text: "Stored in this browser (IndexedDB), kept across reloads: " + stored.length + " snapshot" +
		(stored.length === 1 ? "" : "s") + ", " + snapFmtSize(sum(stored)) +
		(nd ? " (" + nd + " delta" + (nd === 1 ? "" : "s") + ": only the pages changed since the snapshot before)" : "") +
		(image ? "; " + mine.length + " for " + snapKernelName() : "") + "." +
		(mem ? " In memory only (not stored): " + mem + ", " + snapFmtSize(sum(snapshots) - sum(stored)) + "." : "") + est };
}

// The diff's two pickers: this kernel's snapshots and "now"; the user's
// pick stays while it is listed, otherwise the newest two (a new save
// moves them).
function snapPicker(pickA, pickB) {
	const key = snapKernelKey();
	const mine = snapshots.filter((s) => key && s.kernelKey === key);
	const ids = new Set(mine.map((s) => s.id).concat("now"));
	const dA = mine.length >= 2 ? mine[mine.length - 2] : mine[0], dB = mine.length >= 2 ? mine[mine.length - 1] : null;
	return { mine, a: pickA !== null && ids.has(pickA) ? pickA : dA ? dA.id : "now", b: pickB !== null && ids.has(pickB) ? pickB : dB ? dB.id : "now" };
}

function initSnapshots() {
	// The Page tab's "Show live memory" of a snapshot's page (Page tab, A1: goes with its component).
	$("page_snap_live").addEventListener("click", () => api.show.navJump(() => {
		setPageSnapView(null);
		api.show.inspect();
	}));
	snapLoad();
}

provide({ snapKernelKey, snapWorker });

export { snapKernelKey, snapTx, saveSnapshot, restoreSnapshot, renderSnapshots, initSnapshots, SNAP, snapReady, snapRow,
	snapStoreView, snapPicker, snapEditName, snapEditEnd, snapDeleteAll, snapDelete, snapSwitch, downloadSnapshot, snapOfBuild };
