"use strict";
// The Snapshots tab (S1): v86 save_state() snapshots with names, kept in
// IndexedDB per kernel across reloads, and the diff of two of them (or one
// and now). Uses app.js's globals (emulator, image, initrd, stepper, ...).
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
// snapshot is offered only for the kernel it was taken with.
//
// Delta snapshots (R1): the page keeps two 32-bit hashes per 4 KiB page of
// the last snapshot saved or restored (snapBase, from the Worker). The next
// save of the same kernel and RAM size stores only the pages whose hashes
// changed plus v86's state without RAM (snapworker.js documents the
// format): delta = { base: id, pages }, raw = that state's length, full =
// the full state's. Restore, Download and the diff rebuild its RAM from the
// chain (the full snapshot, then each delta's pages). Deleting a base folds
// it into its deltas first (they become deltas of its base, or full).

const MAX_SNAPSHOTS = 8;  // kept in memory when they cannot be stored
const SNAP_DB = "mkb-snapshots";
const SNAP_STORE = "snaps";
const SNAP_HEX_ALL = 64;  // the hexdump shows only changed rows above this many
const SNAP_LZ4_LEVEL = 1; // lz4-wasm has one compressor: LZ4's fast one, what `lz4 -1` (the default) runs

let snapshots = [];
let snapDb = null;        // IDBDatabase, or null when this browser has no storage for the page
let snapDbWhy = "";       // why not
let snapDbOpen = null;    // Promise of snapDb
let snapPersist = null;   // navigator.storage.persist() result
let snapEstimate = null;  // navigator.storage.estimate() result
let snapEditing = false;  // a name is being edited: no redraw of the table
let snapWorkerObj = null;
let snapReqId = 0;
const snapWaiting = new Map();
let snapDiff = null;      // { a, b, pages, ... } of the diff shown
let pageSnapView = null;  // the Page tab shows a snapshot's page: { page, bytes, base, name, against, cur }
let snapDelAllTimer = null;
let snapBase = null;      // the last snapshot saved or restored: { id, kernelKey, ram, hashes }
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

const snapKernelName = () => (image ? image.name + (initrd ? " + " + initrd.name : "") : "");

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

const snapRecord = (s) => {
	const { where, ...rec } = s;
	return rec;
};

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
	renderSnapStore();
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
		...(stepper ? { stop: { text: stepper.text.slice(), textSeen: stepper.textSeen, idleMs: stepper.idleMs } } : {}),
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

function snapDefaultName(meta, time) {
	return snapKernelName() + " @" + snapCount(meta).toLocaleString("en") + (meta.sym ? " " + meta.sym : " " + hex(meta.eip)) +
		" " + new Date(time).toLocaleTimeString("en-GB");
}

async function saveSnapshot() {
	if (!emulator || !emuReady || REV.busy)
		return;
	const em = emulator;
	$("snap_save").disabled = true;
	$("snap_status").textContent = "Saving...";
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
		$("snap_status").textContent = "Saved #" + seq + " (" + s.name + "): save_state " + s.saveMs + " ms" +
			(codec === "lz4" ? ", page hashes and LZ4 level " + SNAP_LZ4_LEVEL + " " + packMs + " ms in a Worker" : "") +
			(delta ? "; a delta of #" + base.seq + ": " + snapPagesText(delta.pages) + ", " + snapFmtSize(s.size) +
				" stored instead of a full snapshot." : ".") + why;
	} catch (e) {
		$("snap_status").textContent = "save_state failed: " + e;
	}
	$("snap_save").disabled = !emulator;
	renderSnapshots();
	snapUpdateEstimate();
}

async function restoreSnapshot(s) {
	if (REV.busy)
		return;
	if (!emulator || emuRam !== s.ram) {
		$("snap_status").textContent = "“" + s.name + "” was taken with " + kib(s.ram) +
			" of RAM; boot with that size to restore it.";
		return;
	}
	$("snap_status").textContent = "Restoring “" + s.name + "”...";
	delete $("snap_status").dataset.restored;
	try {
		// Another timeline: the reverse history (R2) starts anew from the
		// snapshot, with the recordings as they were.
		if (stepper)
			stepper.unrewind();
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
		speedReset();
		if (stepper)
			stepper.jumpForget();
		if (counting || (stepper && stepper.steps)) {
			counting = false;
			countNote = "A snapshot was restored: the counters stopped there.";
		}
		// The Debugger's note (shown over the screen while paused) said why
		// the machine stopped where it was before ("Paused at the reset
		// vector.", a Run to): now it is here because of the Restore.
		$("dbg_status").textContent = "Restored the snapshot “" + s.name + "”.";
		if (runState === "paused" || runState === "stepped")
			afterStop();
		$("snap_status").textContent = "Restored “" + s.name + "” (" + s.kernel + ").";
		$("snap_status").dataset.restored = s.id;
		if (selectedPage >= 0)
			inspect();
	} catch (e) {
		$("snap_status").textContent = "restore_state failed: " + e;
	}
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

async function downloadSnapshot(s) {
	let blob = s.data;
	if (s.delta) {
		// A delta downloads as the full state, rebuilt from its chain.
		try {
			blob = new Blob([(await snapWorker({ op: "export", s: snapBlob(s) })).data]);
		} catch (e) {
			$("snap_status").textContent = "Download failed: " + ((e && e.message) || e);
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
		$("snap_status").textContent = "Renamed." + (await snapStore(s));
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
			inspect();
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
			$("snap_status").textContent = "Not deleted: “" + x.name + "” is the base of a delta that could not be rebuilt (" +
				((e && e.message) || e) + ").";
			break;
		}
		snapshots = snapshots.filter((s) => s !== x);
		if (snapBase && snapBase.id === x.id)
			snapBase = null;
		if (snapDb && x.where === "stored")
			try {
				await snapTx("readwrite", (st) => st.delete(x.id));
			} catch (e) {
				$("snap_status").textContent = "Deleting from IndexedDB failed: " + ((e && e.message) || e);
			}
	}
	if (folded && !quiet)
		$("snap_status").textContent = "Deleted; " + folded + " delta snapshot" + (folded === 1 ? " was" : "s were") +
			" rebuilt without " + (list.length === 1 ? "it" : "them") + "." + why;
	if (snapDiff && (ids.has(snapDiff.a.id) || ids.has(snapDiff.b.id))) {
		snapDiff = null;
		renderSnapDiff();
	}
	renderSnapshots();
	snapUpdateEstimate();
}

function snapDeleteAll() {
	const b = $("snap_delall");
	if (!b.dataset.armed) {
		b.dataset.armed = "1";
		b.textContent = "Delete all " + snapshots.length + "? Press again";
		clearTimeout(snapDelAllTimer);
		snapDelAllTimer = setTimeout(() => {
			delete b.dataset.armed;
			b.textContent = "Delete all";
		}, 5000);
		return;
	}
	clearTimeout(snapDelAllTimer);
	delete b.dataset.armed;
	b.textContent = "Delete all";
	const n = snapshots.length;
	snapDelete(snapshots.slice(), true).then(() => {
		$("snap_status").textContent = "Deleted " + n + " snapshot" + (n === 1 ? "" : "s") + ".";
	});
}

function snapSwitch(s) {
	const k = kernels.find((x) => x.name === s.kernel);
	if (!k) {
		$("snap_status").textContent = "“" + s.kernel + "” is not one of the listed kernels: load that bzImage from disk " +
			"(Machine tab) with " + kib(s.ram) + " of RAM to restore “" + s.name + "”.";
		return;
	}
	$("kernel").value = k.name;
	if (![...$("memory").options].some((o) => o.value === String(s.ram)))
		$("memory").add(new Option(kib(s.ram), String(s.ram)));
	$("snap_status").textContent = "Switching to " + k.name + " with " + kib(s.ram) + "...";
	loadKernel(k, String(s.ram)).catch((e) => {
		setStatus(String(e));
		$("snap_status").textContent = "Switching to " + k.name + " failed: " + ((e && e.message) || e) + ".";
	});
}

// ---- the table ------------------------------------------------------------

function snapMatch(s) {
	const key = snapKernelKey();
	if (!key)
		return "";
	return s.kernelKey === key ? "match" : "other";
}

function snapButton(text, cls, title, fn) {
	const b = document.createElement("button");
	b.type = "button";
	b.textContent = text;
	if (cls)
		b.className = cls;
	if (title)
		b.title = title;
	b.addEventListener("click", fn);
	return b;
}

function snapEditName(s, td) {
	snapEditing = true;
	const inp = document.createElement("input");
	inp.className = "snapedit";
	inp.value = s.name;
	inp.setAttribute("aria-label", "Name of snapshot #" + s.seq);
	let done = false;
	const finish = async (save) => {
		if (done)
			return;
		done = true;
		snapEditing = false;
		if (save)
			await snapRename(s, inp.value);
		renderSnapshots();
	};
	inp.addEventListener("keydown", (ev) => {
		ev.stopPropagation();
		if (ev.key === "Enter")
			finish(true);
		else if (ev.key === "Escape")
			finish(false);
	});
	inp.addEventListener("keyup", (ev) => ev.stopPropagation());
	inp.addEventListener("blur", () => finish(true));
	td.textContent = "";
	td.append(inp);
	inp.focus();
	inp.select();
}

function renderSnapshots() {
	if (snapEditing)
		return;
	const tbody = $("snapshots").tBodies[0];
	tbody.textContent = "";
	const key = snapKernelKey();
	for (const s of snapshots) {
		const tr = tbody.insertRow();
		const match = snapMatch(s);
		Object.assign(tr.dataset, { id: s.id, raw: s.raw, full: s.full || s.raw, stored: s.size, kernel: match, where: s.where,
			codec: snapCodec(s), base: s.delta ? s.delta.base : "", depth: snapDepth(s), pages: s.delta ? s.delta.pages : -1 });
		if (match === "other")
			tr.className = "snapother";
		const name = tr.insertCell();
		name.className = "snapcell-name";
		const nb = snapButton(s.name, "snapname", "Rename (Enter saves, Esc cancels)", () => snapEditName(s, name));
		name.append(nb);
		const ratio = s.raw / s.size;
		const day = new Date(s.time);
		const cells = [
			"#" + s.seq + ", " + (day.toDateString() === new Date().toDateString() ? day.toLocaleTimeString("en-GB") :
				day.toLocaleString("en-GB")),
			s.meta ? "instruction " + snapCount(s.meta).toLocaleString("en") + ", " + (s.meta.sym || "EIP " + hex(s.meta.eip)) : "",
			(match === "other" ? "for another kernel: " : "") + s.kernel + ", " + kib(s.ram),
			snapFmtSize(s.size) + (s.where === "stored" ? ", stored" : ", in memory only") + (s.delta ? "; delta of #" +
				((snapById(s.delta.base) || {}).seq || "?") + ": " + snapPagesText(s.delta.pages) : ""),
			s.raw.toLocaleString("en") + (s.delta && s.full ? " (full state " + s.full.toLocaleString("en") + ")" : ""),
			snapCodec(s) !== "raw" ? ratio.toFixed(1) + " : 1 (" + (100 * s.size / s.raw).toFixed(2) + "%)" : "-",
			snapHow(s),
		];
		for (const v of cells)
			tr.insertCell().textContent = v;
		tr.cells[6].className = "ratio";
		const td = tr.insertCell();
		td.className = "snapacts";
		if (match === "other")
			td.append(snapButton("Switch to " + s.kernel, "snapswitch", "Load " + s.kernel + " with " + kib(s.ram) +
				" of RAM; then this snapshot can be restored", () => snapSwitch(s)), " ");
		else if (match === "match")
			td.append(snapButton("Restore", "restore", "", () => restoreSnapshot(s)), " ");
		if (s.meta && s.meta.run)
			td.append(snapButton("Replay", "snapreplay", "Boot " + s.meta.run.kernel + " again and replay this snapshot's run (the same keys at the same " +
				"instruction counts) to " + s.meta.run.end.at.toLocaleString("en") + " instructions: the same state without restoring it",
			() => replayRun(s.meta.run).catch((e) => runStatus("Replay failed: " + e, "error"))), " ");
		td.append(snapButton("Download", "snapdl", "", () => downloadSnapshot(s)), " ",
			snapButton("Delete", "snapdel", "Delete this snapshot" + (s.where === "stored" ? " from this browser" : ""),
				() => snapDelete([s])));
	}
	$("snapshots").hidden = snapshots.length === 0;
	$("snap_delall").hidden = snapshots.length === 0;
	$("snapshots").dataset.kernelKey = key || "";
	renderSnapStore();
	renderSnapPick();
}

function renderSnapStore() {
	const el = $("snap_store");
	const stored = snapshots.filter((s) => s.where === "stored");
	const mem = snapshots.length - stored.length;
	const sum = (l) => l.reduce((n, s) => n + s.size, 0);
	const key = snapKernelKey();
	const mine = snapshots.filter((s) => key && s.kernelKey === key);
	if (!snapDb && snapDbOpen) {
		el.textContent = "Snapshots are not stored in this browser (" + (snapDbWhy || "no IndexedDB") + "): they are kept in memory only, " +
			"the last " + MAX_SNAPSHOTS + ", until the page is closed. " +
			(mem ? mem + " in memory, " + snapFmtSize(sum(snapshots)) + "." : "");
		el.dataset.storage = "none";
		return;
	}
	el.dataset.storage = "indexeddb";
	const nd = stored.filter((s) => s.delta).length;
	const est = snapEstimate && snapEstimate.quota ? " This site uses " + snapFmtSize(snapEstimate.usage || 0) + " of " +
		snapFmtSize(snapEstimate.quota) + " the browser allows it" + (snapPersist === true ? ", kept persistently." :
		snapPersist === false ? "; the browser may evict it when the disk fills up." : ".") : "";
	el.textContent = "Stored in this browser (IndexedDB), kept across reloads: " + stored.length + " snapshot" +
		(stored.length === 1 ? "" : "s") + ", " + snapFmtSize(sum(stored)) +
		(nd ? " (" + nd + " delta" + (nd === 1 ? "" : "s") + ": only the pages changed since the snapshot before)" : "") +
		(image ? "; " + mine.length + " for " + snapKernelName() : "") + "." +
		(mem ? " In memory only (not stored): " + mem + ", " + snapFmtSize(sum(snapshots) - sum(stored)) + "." : "") + est;
}

// The diff's two pickers: this kernel's snapshots and "now".
function renderSnapPick() {
	const key = snapKernelKey();
	const mine = snapshots.filter((s) => key && s.kernelKey === key);
	for (const [id, dflt] of [["snap_a", mine.length >= 2 ? mine[mine.length - 2] : mine[0]], ["snap_b", mine.length >= 2 ? mine[mine.length - 1] : null]]) {
		const sel = $(id), was = sel.value;
		sel.textContent = "";
		for (const s of mine)
			sel.add(new Option("#" + s.seq + " " + s.name, s.id));
		sel.add(new Option("now (the running machine)", "now"));
		// The user's pick stays; otherwise the newest two (a new save moves them).
		const keep = sel.dataset.picked && [...sel.options].some((o) => o.value === was);
		sel.value = keep ? was : dflt ? dflt.id : "now";
	}
	$("snap_diff").disabled = !mine.length || !emulator;
	$("snap_diffbox").hidden = !mine.length;
}

// ---- the diff -------------------------------------------------------------

function snapWorker(msg, transfer) {
	if (!snapWorkerObj) {
		snapWorkerObj = new Worker(withV("snapworker.js"));
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

const snapById = (id) => snapshots.find((s) => s.id === id) || null;

// One side of a diff for the worker. "now" is a save_state() of the
// running machine: one consistent copy of the CPU and RAM, and v86 packs
// only the non-zero pages, so it costs what Save snapshot costs (copying
// all of RAM on the main thread took 295 ms for 64 MiB at a 4x throttle).
async function snapSide(id, fresh) {
	if (id !== "now") {
		const s = snapById(id);
		return snapBlob(s);
	}
	return fresh ? { key: "now", raw: await emulator.save_state() } : { key: "now" };
}

function snapNowInfo() {
	const meta = snapMeta(emulator);
	return { name: "now", meta, ram: emuRam };
}

async function snapRunDiff() {
	const a = $("snap_a").value, b = $("snap_b").value;
	if (!a || !b || !emulator)
		return;
	const out = $("snap_diff_out");
	out.dataset.state = "running";
	$("snap_diff_status").textContent = "Decompressing and comparing...";
	$("snap_diff").disabled = true;
	const t0 = realNow();
	try {
		const now = a === "now" || b === "now" ? snapNowInfo() : null;
		const sa = await snapSide(a, true), sb = await snapSide(b, a !== "now");
		const tr = [sa, sb].filter((x) => x.raw).map((x) => x.raw);
		const r = await snapWorker({ op: "diff", a: sa, b: sb }, tr);
		const info = (id, cpu) => {
			const s = id === "now" ? now : snapById(id);
			const regs = { ...(s.meta ? s.meta.regs : {}), ...cpu.regs };
			return { id, name: s.name, meta: s.meta, regs, ram: s.ram };
		};
		snapDiff = { a: info(a, r.a), b: info(b, r.b), pages: r.pages, ms: Math.round(realNow() - t0), workerMs: r.ms, hex: null };
		renderSnapDiff();
		out.dataset.compared = r.compared >= 0 ? r.compared : "all";
		$("snap_diff_status").textContent = "Compared in " + snapDiff.ms + " ms (the comparison itself " + r.ms + " ms, in a Worker" +
			(r.compared >= 0 ? "; one side is a delta of the other, so only its " + r.compared.toLocaleString("en") + " page" +
				(r.compared === 1 ? " was" : "s were") + " compared" : "") + ").";
		out.dataset.state = "done";
	} catch (e) {
		$("snap_diff_status").textContent = "Diff failed: " + ((e && e.message) || e);
		out.dataset.state = "error";
	}
	$("snap_diff").disabled = false;
}

const SNAP_REGS = ["EIP", "EFLAGS", "EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP", "ESP", "CS", "DS", "SS", "ES", "FS", "GS", "CR0", "CR2", "CR3", "CR4"];

function snapRegText(n, v) {
	if (v === undefined)
		return "-";
	if (/^[CDSEFG]S$/.test(n))
		return "0x" + h4(v);
	let t = "0x" + h8(v);
	if (n === "EFLAGS")
		t += " " + FLAG_BITS.filter(([b]) => v & (1 << b)).map(([, f]) => f).join(" ");
	return t;
}

function renderSnapDiff() {
	const out = $("snap_diff_out");
	const d = snapDiff;
	out.hidden = !d;
	if (!d)
		return;
	const A = d.a, B = d.b;
	const nm = (x) => (x.id === "now" ? "now" : "“" + x.name + "”");
	out.dataset.icountA = A.meta.icount;
	out.dataset.icountB = B.meta.icount;
	const nA = snapCount(A.meta), nB = snapCount(B.meta);
	out.dataset.countA = nA;
	out.dataset.countB = nB;
	const sum = $("snap_diff_sum");
	sum.textContent = "From " + nm(A) + " to " + nm(B) + ": instruction " + nA.toLocaleString("en") + " → " +
		nB.toLocaleString("en") + " (" + (nB >= nA ? "+" + (nB - nA).toLocaleString("en") :
		"earlier by " + (nA - nB).toLocaleString("en")) + "), EIP " +
		(A.meta.sym || hex(A.meta.eip)) + " → " + (B.meta.sym || hex(B.meta.eip)) + "; " + d.pages.length + " of " +
		Math.ceil(Math.max(A.ram, B.ram) / PAGE).toLocaleString("en") + " pages of RAM changed, " +
		d.pages.reduce((n, p) => n + p[1], 0).toLocaleString("en") + " bytes." +
		(A.ram !== B.ram ? " The RAM sizes differ (" + kib(A.ram) + ", " + kib(B.ram) + "): pages on one side only count as changed." : "");
	// registers
	const tb = $("snap_regs").tBodies[0];
	tb.textContent = "";
	const same = [];
	for (const n of SNAP_REGS) {
		const o = A.regs[n], v = B.regs[n];
		if (o === v) {
			same.push(n);
			continue;
		}
		const tr = tb.insertRow();
		Object.assign(tr.dataset, { reg: n, old: o === undefined ? "" : hex(o), new: v === undefined ? "" : hex(v) });
		tr.insertCell().textContent = n;
		tr.insertCell().textContent = snapRegText(n, o);
		tr.insertCell().textContent = "→";
		const c = tr.insertCell();
		c.textContent = snapRegText(n, v);
		c.className = "snapnew";
	}
	$("snap_regs").hidden = !tb.rows.length;
	$("snap_regs_same").textContent = tb.rows.length ? "Unchanged: " + same.join(" ") + "." : "No register changed.";
	// screen
	const sc = $("snap_screen");
	sc.textContent = "";
	const la = A.meta.screen || [], lb = B.meta.screen || [];
	let nScr = 0;
	for (let i = 0; i < Math.max(la.length, lb.length); i++) {
		const o = la[i] || "", v = lb[i] || "";
		if (o === v)
			continue;
		nScr++;
		const row = document.createElement("div");
		row.dataset.line = i;
		row.className = "snapscr";
		const n = document.createElement("span");
		n.className = "snapln";
		n.textContent = "line " + (i + 1);
		const po = document.createElement("pre");
		po.textContent = "- " + o;
		const pv = document.createElement("pre");
		pv.textContent = "+ " + v;
		pv.className = "snapnew";
		row.append(n, po, pv);
		sc.append(row);
	}
	if (!nScr)
		sc.textContent = "No line of the VGA text screen changed.";
	renderSnapPages();
	renderSnapHex();
}

// Changed pages grouped by region / ELF section, with their symbols.
function renderSnapPages() {
	const box = $("snap_pages");
	box.textContent = "";
	const d = snapDiff;
	if (!d.pages.length) {
		box.textContent = "No page of RAM changed.";
		return;
	}
	const groups = new Map();
	for (const [p, n] of d.pages) {
		const g = p * PAGE < emuRam ? pageLabel(p) || "RAM" : "beyond this machine's RAM";
		if (!groups.has(g))
			groups.set(g, []);
		groups.get(g).push([p, n]);
	}
	for (const [g, list] of groups) {
		const det = document.createElement("details");
		det.open = true;
		const sumEl = document.createElement("summary");
		sumEl.textContent = g + ": " + list.length + " page" + (list.length === 1 ? "" : "s") + ", " +
			list.reduce((n, x) => n + x[1], 0).toLocaleString("en") + " bytes changed";
		det.append(sumEl);
		for (const [p, n] of list) {
			const row = document.createElement("div");
			row.className = "snappg";
			Object.assign(row.dataset, { page: p, bytes: n });
			const syms = symbolsInPage(p);
			const addr = document.createElement("code");
			addr.textContent = "0x" + h8(p * PAGE);
			row.append(addr, " " + n.toLocaleString("en") + " byte" + (n === 1 ? "" : "s") +
				(syms.length ? " · " + syms.slice(0, 3).map((s) => s.name).join(", ") + (syms.length > 3 ? " +" + (syms.length - 3) : "") : "") + " ");
			row.append(snapButton("hexdump", "snaphex", "Both sides of this page, byte by byte", () => snapShowHex(p)), " ",
				snapButton("Page tab: " + (d.b.id === "now" ? "now" : "#" + snapById(d.b.id).seq), "snapopen",
					"Open this page in the Page tab as " + (d.b.id === "now" ? "it is now" : "“" + d.b.name + "” had it") +
					"; yellow bytes differ from " + (d.a.id === "now" ? "now" : "“" + d.a.name + "”"), () => snapOpenPage(p, "b")), " ",
				snapButton("Page tab: " + (d.a.id === "now" ? "now" : "#" + snapById(d.a.id).seq), "snapopena",
					"Open this page in the Page tab as " + (d.a.id === "now" ? "it is now" : "“" + d.a.name + "” had it"), () => snapOpenPage(p, "a")));
			det.append(row);
		}
		box.append(det);
	}
}

async function snapPages(p) {
	const d = snapDiff;
	return snapWorker({ op: "pages", a: await snapSide(d.a.id, false), b: await snapSide(d.b.id, false), p });
}

async function snapShowHex(p) {
	const d = snapDiff;
	try {
		const r = await snapPages(p);
		if (d !== snapDiff)
			return;
		d.hex = { p, a: r.a, b: r.b, all: false };
		renderSnapHex();
	} catch (e) {
		$("snap_diff_status").textContent = "Reading the page failed: " + ((e && e.message) || e);
	}
}

function renderSnapHex() {
	const box = $("snap_hex");
	const d = snapDiff, h = d && d.hex;
	box.hidden = !h;
	box.textContent = "";
	if (!h) {
		delete box.dataset.page;
		return;
	}
	const per = window.matchMedia("(max-width: 640px)").matches ? 8 : 16;
	const head = document.createElement("p");
	head.className = "note";
	let nChg = 0;
	for (let i = 0; i < PAGE; i++)
		nChg += h.a[i] !== h.b[i];
	head.append("Page 0x" + h8(h.p * PAGE) + " (" + pageLabel(h.p) + "): " + nChg + " bytes differ, marked yellow" +
		(h.all ? "." : "; rows without a change are left out. "));
	if (!h.all)
		head.append(snapButton("all rows", "", "Show all " + PAGE / per + " rows", () => {
			h.all = true;
			renderSnapHex();
		}));
	const grid = document.createElement("div");
	grid.className = "snaphexgrid";
	for (const [side, mine, other, x] of [["a", h.a, h.b, d.a], ["b", h.b, h.a, d.b]]) {
		const col = document.createElement("div");
		col.className = "snapcol snapcol-" + side;
		const t = document.createElement("b");
		t.textContent = x.id === "now" ? "now" : "#" + snapById(x.id).seq + " " + x.name;
		const pre = document.createElement("pre");
		let html = "";
		for (let o = 0; o < PAGE; o += per) {
			let chg = false;
			for (let i = o; i < o + per; i++)
				chg = chg || mine[i] !== other[i];
			if (!chg && !h.all)
				continue;
			html += h8(h.p * PAGE + o) + " ";
			let asc = "";
			for (let i = o; i < o + per; i++) {
				const b = mine[i].toString(16).padStart(2, "0");
				html += " " + (mine[i] !== other[i] ? "<mark>" + b + "</mark>" : b);
				asc += esc(asciiOf(mine[i]));
			}
			html += "  " + asc + "\n";
		}
		pre.innerHTML = html || "(no row differs)";
		col.append(t, pre);
		grid.append(col);
	}
	box.append(head, grid);
	box.dataset.page = h.p;
}

// The Page tab at a snapshot's page (side "a" or "b"); Back returns here.
async function snapOpenPage(p, which) {
	const d = snapDiff;
	let r;
	try {
		r = await snapPages(p);
	} catch (e) {
		$("snap_diff_status").textContent = "Reading the page failed: " + ((e && e.message) || e);
		return;
	}
	if (d !== snapDiff)
		return;
	const me = which === "a" ? d.a : d.b, other = which === "a" ? d.b : d.a;
	const label = (x) => (x.id === "now" ? "now (when diffed)" : "“" + x.name + "”");
	const view = { page: p, bytes: which === "a" ? r.a : r.b, base: which === "a" ? r.b : r.a,
		id: me.id, name: label(me), againstId: other.id, against: label(other), cur: me.meta.phys };
	navJump(() => {
		pageSnapView = view;
		selectedPage = p;
		drawGrid();
		setLive();
		showTab("page");
		inspect();
	});
}

function pageSnapBanner(on) {
	const el = $("page_snap");
	el.hidden = !on;
	if (on)
		$("page_snap_text").textContent = "Showing snapshot " + pageSnapView.name + "; yellow bytes differ from " +
			pageSnapView.against + ". ";
}

function initSnapshots() {
	$("snap_save").addEventListener("click", saveSnapshot);
	$("snap_save").title = "save_state(), then page hashes and LZ4 level " + SNAP_LZ4_LEVEL + " (lz4-wasm 0.9.2) in a Worker: " +
		"after the first snapshot only the pages changed since the last one saved or restored are kept (a delta); " +
		"a download is the full state as an LZ4 frame (.v86state.lz4) that lz4 -d reads.";
	$("snap_delall").addEventListener("click", snapDeleteAll);
	$("snap_diff").addEventListener("click", snapRunDiff);
	for (const id of ["snap_a", "snap_b"])
		$(id).addEventListener("change", () => ($(id).dataset.picked = "1"));
	$("page_snap_live").addEventListener("click", () => navJump(() => {
		pageSnapView = null;
		inspect();
	}));
	snapLoad();
}
