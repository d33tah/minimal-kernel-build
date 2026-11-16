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

const state = new Store({ kernels, image, initrd, emulator, emuRam, emuReady, symbols, disasm, pageClass, grid,
	selectedPage, tab, vmHasKeys, stepper, runState, counting, countNote, startPaused, prefSpeed, machine });

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
	machine: ["boot", "loadKernel", "useImage", "showHeader", "currentPhys", "clockMips", "flushScreen", "FLAG_BITS",
		"v86Restore", "snapKernelKey", "snapWorker"],
	// Guest memory and the Memory tab's map and grid.
	memory: ["ram", "drawMaps", "drawGrid", "pageAt", "pageLabel", "symbolsInPage", "memCellStyle", "MMAP"],
	// Symbols, instructions and debug info (DWARF: frames, functions).
	symbols: ["symbolAt", "symButton", "insnIndex", "insnStart", "unwindHere", "FRAME_METHODS", "kernelSymLookup",
		"dbgHas", "dbgFrames", "fnFactsText"],
	// C source: links, the Source panel, the editor, coverage, rebuild.
	source: ["srcLinkHtml", "srcLinkEl", "coverageFor", "syncSrcToggle", "kfetch", "genSrcPicked", "genImport",
		"genRestore", "edPanelShown", "covRun", "covRender", "rbSync"],
	// Memory events (the Events tab).
	events: ["MEV"],
	// Run, pause, step, Run to, and reverse execution (rewind).
	step: ["pause", "resume", "pauseAll", "afterStop", "runToTargets", "runToLine", "speedReset", "REF_KEY",
		"revMark", "revStepBack", "revJit", "revBranch", "REV", "REV_JIT_MS_PER_INSN", "REV_JIT_MS_PER_CALL"],
	// What the page shows: tabs, the page listing, jumps, the status line,
	// bookmarks, popovers.
	show: ["setStatus", "showTab", "selectPage", "inspect", "renderContext", "updateDebugUi", "navJump", "jumpTo",
		"openFunction", "writeHash", "listingHold", "listingKeep", "navListingAnchor", "setLive", "setCompactTabs",
		"compactDefault", "hideTip", "runBadgeOpen", "renderRunBadgePwa", "bmLineStar", "bmFnNames", "bmTip"],
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
	setInitrd, setEmulator, setEmuRam, setEmuReady, setSymbols, setDisasm, setPageClass, setGrid,
	setSelectedPage, setTab, setVmHasKeys, setStepper, setRunState, setCounting, setCountNote, setStartPaused,
	setPrefSpeed, publishMachine, api, provide };
