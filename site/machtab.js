"use strict";

// The Machine tab's view state (A1): what the modules set and the tab's
// components (machinepanel.jsx) draw. The tab is shown at load, so it is
// mounted at once; the screen (#screen_container, which v86 draws into) is
// index.html's own and no part of it.
//   MSET: the boot and run settings, the values of the tab's menus and of
//     the count box (the kernel, RAM, speed and clock lists; app.js,
//     kernel.js, replay.js, snapshots.js and bookmarks.js choose in them).
//     The command line and the symbol offset are uncontrolled inputs (a
//     test sets the offset's value directly): read from the element.
//   MHEAD: the status line, the header check's problems and the bzImage's
//     header line (header.js), the offline and new version notes (pwa.js).
//   MIMP: the import statuses (symimport.js, genimport.js, srctext.js) and
//     the deployed build (debugui.js loadVersion).
//   MV: what the run part shows (Step back: reverse.js revBackPart) (debugui.js drawDebugUi, at most once per
//     drawn frame, and renderClock four times a second): each part is a
//     field the module replaces only when what it shows differs (mvPut), and
//     each component reads its own field (useField), so a frame redraws only
//     the parts that changed.

import { controller } from "./ui.jsx";
import { kib } from "./util.js";

// The RAM sizes listed (bytes); ?mem=, a snapshot or a replayed run may add one.
const RAM_SIZES = [["2097152", "2 MB"], ["4194304", "4 MB"], ["18874368", "18 MB"], ["33554432", "32 MB"], ["67108864", "64 MB"],
	["100663296", "96 MB"], ["134217728", "128 MB"], ["268435456", "256 MB"], ["536870912", "512 MB"]];
const SPEEDS = [["full", "full (JIT)"], ["10000000", "10M/s"], ["1000000", "1M/s"], ["100000", "100k/s"], ["10000", "10k/s"],
	["1000", "1k/s"], ["100", "100/s"], ["10", "10/s"], ["1", "1/s"]];
const CLOCKS = [["10", "10/µs"], ["100", "100/µs"], ["1000", "1000/µs"], ["real", "real time"]];
const DEFAULT_CMDLINE = "nokaslr cryptomgr.notests";

const MSET = controller({
	kernels: [],    // [name, label]
	kernel: "",
	rams: RAM_SIZES,     // [bytes, label]
	ram: RAM_SIZES[0][0],
	speed: SPEEDS[0][0],
	clock: CLOCKS[0][0],
	count: true,
	cmdlineEl: null,
	offsetEl: null,
	cmdlineTyped: false,
});

// Change settings ({ kernel: "hello", ram: "4194304" }) and draw them.
function setSettings(fields) {
	Object.assign(MSET, fields);
	MSET.changed();
}

const ramHas = (v) => MSET.rams.some(([x]) => x === String(v));

// A RAM size the list does not have yet (?mem=, a snapshot's, a run's).
function addRam(v) {
	if (ramHas(v))
		return;
	MSET.rams = [...MSET.rams, [String(v), kib(Number(v))]];
	MSET.changed();
}

const ramSizes = () => MSET.rams.map(([v]) => Number(v));
const speedText = () => (SPEEDS.find(([v]) => v === MSET.speed) || ["", MSET.speed])[1];
const clockText = () => (CLOCKS.find(([v]) => v === MSET.clock) || ["", MSET.clock])[1];

const cmdline = () => (MSET.cmdlineEl ? MSET.cmdlineEl.value : DEFAULT_CMDLINE);

function setCmdline(v) {
	if (MSET.cmdlineEl)
		MSET.cmdlineEl.value = v;
}

const symOffset = () => (MSET.offsetEl ? MSET.offsetEl.value : "auto");

const MHEAD = controller({ status: "Loading kernel list...", problems: [], kinfo: "", pwaNewer: null, pwaMisses: [] });

function setStatus(text) {
	MHEAD.status = text;
	MHEAD.changed();
}

const MIMP = controller({
	sym: { state: "", text: "", dwarf: "" },
	forget: false,
	gen: { state: "", text: "" },
	src: { show: false, state: "", text: "" },
	initrd: { text: "", clear: false, mismatch: "" },
	build: null, // { local } or { sha, url, when, runUrl, run }
});

function setImp(fields) {
	Object.assign(MIMP, fields);
	MIMP.changed();
}

const MV = controller({
	state: { text: "", kind: "off", loc: null },
	btns: { pause: true, cont: true, step: true },
	back: { back: false, tip: "" },
	overlay: { paused: false },
	clock: { text: "" },
	guestIn: "",
	guestKeys: "",
	counters: { rows: [], text: "", ms: undefined, eta: undefined },
	end: { kind: "", code: "", text: "" },
	entry: { text: "", ffMs: undefined, ffTarget: undefined, steps: undefined },
});
const mvSig = {};

// Put a part of MV that differs from what is drawn; true when it did
// (the caller then calls MV.changed() once for all it put).
function mvPut(key, v, sig = JSON.stringify(v)) {
	if (mvSig[key] === sig)
		return false;
	mvSig[key] = sig;
	MV[key] = v;
	return true;
}

function setGuestKeys(text) {
	if (mvPut("guestKeys", text))
		MV.changed();
}

export { MSET, SPEEDS, CLOCKS, DEFAULT_CMDLINE, setSettings, ramHas, addRam, ramSizes, speedText, clockText, cmdline, setCmdline,
	symOffset, MHEAD, setStatus, MIMP, setImp, MV, mvPut, setGuestKeys };
