// The page's state store and API (R2): what the page's modules share
// without importing each other in a cycle. README.md, "Page state and
// API", has the rules.
//
// State: `state`, a Store (store.js) of the values below. A module reads a
// value through its binding here (`stepper`, `runState`: a live import,
// as cheap as a variable) and changes it with its setter (setStepper,
// setRunState), which updates the binding and the store and tells the
// store's listeners. A view that shows a value subscribes to its key
// (state.subscribe(fn, ["runState"])); R3's React components read
// state.getSnapshot() through useSyncExternalStore.
//
// The machine's state (registers, counters, memory) changes per
// instruction and is not copied here: `machine` counts the times the page
// published it (publishMachine(), at most once per drawn frame), and a view
// reads what it shows from the stepper and the emulator then.
import { Store } from "./store.js";

let kernels = [];
let image = null;      // { name, bytes: Uint8Array, hdr }
let initrd = null;     // { name, bytes: Uint8Array } passed to v86, or null
let emulator = null;
let emuRam = 0;          // RAM size the running emulator was started with
let emuReady = false;    // v86 finished loading (emulator-loaded)
let symbols = null;      // symbols.json of the current kernel, or null
let disasm = null;       // disasm.json of the current kernel, or null
let pageClass = [];      // per page: CSS colour token (region or section)
let grid = { cols: 32, cell: 14, pages: 0 };
let selectedPage = -1;
// The Machine tab first: it holds the VM's screen and run controls.
let tab = "machine";
let vmHasKeys = true;      // the VM screen was clicked last: Alt+Left/Right are its keys
let stepper = null;
let runState = "off";    // "off", "jit" (v86's own loop), "stepped" (our loop), "paused"
let counting = false;    // stepping from reset until every milestone is on screen
let countNote = "";
let startPaused = false; // ?paused=1: stop at the reset vector
let prefSpeed = "1000";  // speed after the entry: ?speed=, else the remembered choice, else 1k/s
let machine = 0;         // publishMachine() calls so far
// The page scan (scan.js), which the Memory tab's grid draws.
let usedPages = null;    // per page: 1 when the latest scan saw a non-zero byte
let changedPages = null; // per page: 1 when it differs between the last two scans
// v86.wasm with two internal functions exported for stepping (stepper.js).
// If the patch fails the page still boots, without the debugger.
let wasmPatchError = null;
// The count from reset (run.js) and Run to (runto.js).
let countStart = 0;      // realNow() when the count began
let countMs = 0;         // stepping time spent counting
let countSteps = 0;      // instructions stepped in that time
let runTo = null;        // Set of physical addresses "Run to" stops at, or null
let runToLabel = "";     // what they are ("0x1234", "init/main.c:1000")
let runToFrom = -1;      // where the Run to started (it steps past a target there once)
let runToMoved = false;  // an instruction has run since
let entryPhase = false;  // stepping at full stepping speed to the kernel entry, then slow
let refSaved = null;     // the "Still alive" milestone whose count is remembered
let jitLoopId = 0;       // v86's loop that runs (draws the screen), 0 for none
// The Debugger's drawing (debugui.js, symview.js).
let prevRegs = null;     // the registers as last drawn (changed ones are highlighted)
let flashes = 0;         // frames whose scan saw changed pages (the grid flashes them)
// The jump history (nav.js) and opening a function (symview.js).
let navBuilt = false;    // the history's first entry is made
let openFnWant = null;   // the function whose source opens when its DWARF arrives
// Compact tabs (Settings): the tab bar shows only the emoji (tabs.jsx).
let compactTabs = false;
// showTab calls so far (tabs.jsx): the views a tab draws on demand follow it
// (a tab shown again, e.g. the Page tab for another page, draws again).
let tabShows = 0;

const state = new Store({ kernels, image, initrd, emulator, emuRam, emuReady, symbols, disasm, pageClass, grid,
	selectedPage, tab, vmHasKeys, stepper, runState, counting, countNote, startPaused, prefSpeed, machine,
	usedPages, changedPages, wasmPatchError, countStart, countMs, countSteps, runTo, runToLabel, runToFrom,
	runToMoved, entryPhase, refSaved, jitLoopId, prevRegs, flashes, navBuilt, openFnWant, compactTabs, tabShows });

const setKernels = (v) => { kernels = v; state.set("kernels", v); };
const setImage = (v) => { image = v; state.set("image", v); };
const setInitrd = (v) => { initrd = v; state.set("initrd", v); };
const setEmulator = (v) => { emulator = v; state.set("emulator", v); };
const setEmuRam = (v) => { emuRam = v; state.set("emuRam", v); };
const setEmuReady = (v) => { emuReady = v; state.set("emuReady", v); };
const setSymbols = (v) => { symbols = v; state.set("symbols", v); };
const setDisasm = (v) => { disasm = v; state.set("disasm", v); };
const setPageClass = (v) => { pageClass = v; state.set("pageClass", v); };
const setGrid = (v) => { grid = v; state.set("grid", v); };
const setSelectedPage = (v) => { selectedPage = v; state.set("selectedPage", v); };
const setTab = (v) => { tab = v; state.set("tab", v); };
const setVmHasKeys = (v) => { vmHasKeys = v; state.set("vmHasKeys", v); };
const setStepper = (v) => { stepper = v; state.set("stepper", v); };
const setRunState = (v) => { runState = v; state.set("runState", v); };
const setCounting = (v) => { counting = v; state.set("counting", v); };
const setCountNote = (v) => { countNote = v; state.set("countNote", v); };
const setStartPaused = (v) => { startPaused = v; state.set("startPaused", v); };
const setPrefSpeed = (v) => { prefSpeed = v; state.set("prefSpeed", v); };
const setUsedPages = (v) => { usedPages = v; state.set("usedPages", v); };
const setChangedPages = (v) => { changedPages = v; state.set("changedPages", v); };
const setWasmPatchError = (v) => { wasmPatchError = v; state.set("wasmPatchError", v); };
const setCountStart = (v) => { countStart = v; state.set("countStart", v); };
const setCountMs = (v) => { countMs = v; state.set("countMs", v); }; // the tests too: MKB.countMs = ...
const setCountSteps = (v) => { countSteps = v; state.set("countSteps", v); };
const setRunTo = (v) => { runTo = v; state.set("runTo", v); };
const setRunToLabel = (v) => { runToLabel = v; state.set("runToLabel", v); };
const setRunToFrom = (v) => { runToFrom = v; state.set("runToFrom", v); };
const setRunToMoved = (v) => { runToMoved = v; state.set("runToMoved", v); };
const setEntryPhase = (v) => { entryPhase = v; state.set("entryPhase", v); };
const setRefSaved = (v) => { refSaved = v; state.set("refSaved", v); };
const setJitLoopId = (v) => { jitLoopId = v; state.set("jitLoopId", v); };
const setPrevRegs = (v) => { prevRegs = v; state.set("prevRegs", v); };
const setFlashes = (v) => { flashes = v; state.set("flashes", v); };
const setNavBuilt = (v) => { navBuilt = v; state.set("navBuilt", v); };
const setOpenFnWant = (v) => { openFnWant = v; state.set("openFnWant", v); };
const setCompactTabs = (v) => { compactTabs = v; state.set("compactTabs", v); };
const markTabShown = () => { tabShows++; state.set("tabShows", tabShows); };
// The machine's state was drawn anew (registers, counters, screen): views
// that show it may read it again.
const publishMachine = () => { machine++; state.set("machine", machine); };


// The page API: the functions (and a few shared objects) of the modules
// that a module below them in the import order calls, by concern. The
// module that has one provides it when it loads (provide({ name })), the
// caller calls it at run time (api.show.showTab("source")). A test's
// stand-in is provided the same way. An entry is undefined until provided.
const API_ENTRIES = {
	// The emulated machine: boot, load a kernel, CPU and clock, snapshots.
	machine: ["boot", "loadKernel", "useImage", "currentPhys", "clockMips", "flushScreen", "FLAG_BITS",
		"v86Restore", "snapKernelKey", "snapWorker"],
	// Guest memory and the Memory tab's map and grid.
	memory: ["drawMaps", "drawGrid", "pageAt", "pageLabel", "symbolsInPage", "memCellStyle", "MMAP"],
	// Symbols, instructions and debug info (DWARF: frames, functions).
	symbols: ["symbolAt", "symButton", "insnIndex", "insnStart", "unwindHere", "FRAME_METHODS", "kernelSymLookup",
		"dbgHas", "dbgFrames", "fnFactsText"],
	// C source: links, the Source panel, the editor, coverage, rebuild.
	source: ["srcLinkHtml", "srcLinkEl", "coverageFor", "syncSrcToggle", "kfetch", "genSrcPicked", "genImport",
		"genRestore", "edPanelShown", "covRun", "covRender", "rbSync", "hpSync"],
	// Memory events (the Events tab).
	events: ["MEV"],
	// Run, pause, step, Run to, and reverse execution (rewind).
	step: ["pause", "resume", "pauseAll", "afterStop", "runToTargets", "runToLine", "runToStopped", "speedReset",
		"revMark", "revJit", "revBranch", "REV", "REV_JIT_MS_PER_INSN", "REV_JIT_MS_PER_CALL"],
	// What the page shows: tabs, the page listing, jumps, the status line,
	// bookmarks, popovers.
	show: ["showTab", "selectPage", "inspect", "renderPage", "renderContext", "renderSymbols",
		"updateDebugUi", "navJump", "jumpTo", "openFunction", "writeHash", "listingHold", "listingKeep",
		"navListingAnchor", "setLive", "runBadgeOpen",
		"renderRunBadgePwa", "bmLineStar", "bmFnNames", "bmTip"],
};
const api = {};
const apiGroup = new Map();
for (const [g, names] of Object.entries(API_ENTRIES)) {
	api[g] = Object.seal(Object.fromEntries(names.map((name) => [name, undefined])));
	for (const name of names)
		apiGroup.set(name, g);
}
Object.freeze(api);

// Fills in the API's entries, by name.
function provide(o) {
	for (const [name, v] of Object.entries(o)) {
		const g = apiGroup.get(name);
		if (!g)
			throw new Error("page API: no entry " + name);
		api[g][name] = v;
	}
}

export { state, kernels, image, initrd, emulator, emuRam, emuReady, symbols, disasm, pageClass, grid, selectedPage,
	tab, vmHasKeys, stepper, runState, counting, countNote, startPaused, prefSpeed, machine, setKernels, setImage,
	setInitrd, setEmulator, setEmuRam, setEmuReady, setSymbols, setDisasm, setPageClass, setGrid, setSelectedPage,
	setTab, setVmHasKeys, setStepper, setRunState, setCounting, setCountNote, setStartPaused, setPrefSpeed,
	publishMachine, usedPages, changedPages, wasmPatchError, countStart, countMs, countSteps, runTo, runToLabel,
	runToFrom, runToMoved, entryPhase, refSaved, jitLoopId, prevRegs, flashes, navBuilt, openFnWant, setUsedPages,
	setChangedPages, setWasmPatchError, setCountStart, setCountMs, setCountSteps, setRunTo, setRunToLabel,
	setRunToFrom, setRunToMoved, setEntryPhase, setRefSaved, setJitLoopId, setPrevRegs, setFlashes, setNavBuilt,
	setOpenFnWant, compactTabs, setCompactTabs, markTabShown, api, provide };
