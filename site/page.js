// What the page's modules share without importing each other in a cycle
// (R1 in the plan; R2 turns it into the page's state store and API).
//
// State: app.js's state the other modules read. An imported binding is
// read-only, so app.js and the modules that change a value call its setter.

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

const setKernels = (v) => { kernels = v; };
const setImage = (v) => { image = v; };
const setInitrd = (v) => { initrd = v; };
const setEmulator = (v) => { emulator = v; };
const setEmuRam = (v) => { emuRam = v; };
const setEmuReady = (v) => { emuReady = v; };
const setSymbols = (v) => { symbols = v; };
const setDisasm = (v) => { disasm = v; };
const setPageClass = (v) => { pageClass = v; };
const setGrid = (v) => { grid = v; };
const setSelectedPage = (v) => { selectedPage = v; };
const setTab = (v) => { tab = v; };
const setVmHasKeys = (v) => { vmHasKeys = v; };
const setStepper = (v) => { stepper = v; };
const setRunState = (v) => { runState = v; };
const setCounting = (v) => { counting = v; };
const setCountNote = (v) => { countNote = v; };
const setStartPaused = (v) => { startPaused = v; };
const setPrefSpeed = (v) => { prefSpeed = v; };


// Functions (and a few constants) of a module that a module below it uses:
// the module above provides them when it loads (provideApp in app.js before
// init() runs, the others at their end), and the module below calls them
// through these bindings at run time. A module that replaces one later
// (a test's stand-in) provides it again.
// From livedis.js.
let kernelSymLookup;
const provideLivedis = (o) => { ({ kernelSymLookup } = o); };
// From dbginfo.js.
let dbgHas, dbgFrames;
const provideDbginfo = (o) => { ({ dbgHas, dbgFrames } = o); };
// From source.js.
let srcLinkHtml, srcLinkEl;
const provideSource = (o) => { ({ srcLinkHtml, srcLinkEl } = o); };
// From app.js.
let navJump, jumpTo, listingHold, writeHash, listingKeep, coverageFor, unwindHere, insnStart, runToLine,
	symButton, symbolAt, inspect, renderContext, selectPage, showTab, speedReset, updateDebugUi, insnIndex,
	syncSrcToggle, boot, loadKernel, setStatus, ram, showHeader, drawMaps, useImage, currentPhys, FRAME_METHODS,
	drawGrid, openFunction, REF_KEY, setCompactTabs, compactDefault, clockMips, pause, resume, pauseAll,
	flushScreen, afterStop, runToTargets, FLAG_BITS, pageLabel, symbolsInPage, setLive, pageAt, navListingAnchor,
	memCellStyle, hideTip, runBadgeOpen, renderRunBadgePwa;
const provideApp = (o) => { ({ navJump, jumpTo, listingHold, writeHash, listingKeep, coverageFor, unwindHere,
	insnStart, runToLine, symButton, symbolAt, inspect, renderContext, selectPage, showTab, speedReset,
	updateDebugUi, insnIndex, syncSrcToggle, boot, loadKernel, setStatus, ram, showHeader, drawMaps, useImage,
	currentPhys, FRAME_METHODS, drawGrid, openFunction, REF_KEY, setCompactTabs, compactDefault, clockMips,
	pause, resume, pauseAll, flushScreen, afterStop, runToTargets, FLAG_BITS, pageLabel, symbolsInPage, setLive,
	pageAt, navListingAnchor, memCellStyle, hideTip, runBadgeOpen, renderRunBadgePwa } = o); };
// From reverse.js.
let revMark, revStepBack, revJit, REV, REV_JIT_MS_PER_INSN, REV_JIT_MS_PER_CALL, revBranch;
const provideReverse = (o) => { ({ revMark, revStepBack, revJit, REV, REV_JIT_MS_PER_INSN,
	REV_JIT_MS_PER_CALL, revBranch } = o); };
// From memevents.js.
let MEV;
const provideMemevents = (o) => { ({ MEV } = o); };
// From genimport.js.
let kfetch, genSrcPicked, genImport, genRestore;
const provideGenimport = (o) => { ({ kfetch, genSrcPicked, genImport, genRestore } = o); };
// From fnfacts.js.
let fnFactsText;
const provideFnfacts = (o) => { ({ fnFactsText } = o); };
// From srcedit.js.
let edPanelShown;
const provideSrcedit = (o) => { ({ edPanelShown } = o); };
// From bookmarks.js.
let bmLineStar, bmFnNames, bmTip;
const provideBookmarks = (o) => { ({ bmLineStar, bmFnNames, bmTip } = o); };
// From coverage.js.
let covRun, covRender;
const provideCoverage = (o) => { ({ covRun, covRender } = o); };
// From memmap.js.
let MMAP;
const provideMemmap = (o) => { ({ MMAP } = o); };
// From cpustate.js.
let v86Restore;
const provideCpustate = (o) => { ({ v86Restore } = o); };
// From rebuild.js.
let rbSync;
const provideRebuild = (o) => { ({ rbSync } = o); };
// From snapshots.js.
let snapKernelKey, snapWorker;
const provideSnapshots = (o) => { ({ snapKernelKey, snapWorker } = o); };

export { kernels, image, initrd, emulator, emuRam, emuReady, symbols, disasm, pageClass, grid, selectedPage,
	tab, vmHasKeys, stepper, runState, counting, countNote, startPaused, prefSpeed, setKernels, setImage,
	setInitrd, setEmulator, setEmuRam, setEmuReady, setSymbols, setDisasm, setPageClass, setGrid,
	setSelectedPage, setTab, setVmHasKeys, setStepper, setRunState, setCounting, setCountNote, setStartPaused,
	setPrefSpeed, kernelSymLookup, provideLivedis, dbgHas, dbgFrames, provideDbginfo, srcLinkHtml, srcLinkEl,
	provideSource, navJump, jumpTo, listingHold, writeHash, listingKeep, coverageFor, unwindHere, insnStart,
	runToLine, symButton, symbolAt, inspect, renderContext, selectPage, showTab, speedReset, updateDebugUi,
	insnIndex, syncSrcToggle, boot, loadKernel, setStatus, ram, showHeader, drawMaps, useImage, currentPhys,
	FRAME_METHODS, drawGrid, openFunction, REF_KEY, setCompactTabs, compactDefault, clockMips, pause, resume,
	pauseAll, flushScreen, afterStop, runToTargets, FLAG_BITS, pageLabel, symbolsInPage, setLive, pageAt,
	navListingAnchor, memCellStyle, hideTip, runBadgeOpen, renderRunBadgePwa, provideApp, revMark, revStepBack,
	revJit, REV, REV_JIT_MS_PER_INSN, REV_JIT_MS_PER_CALL, revBranch, provideReverse, MEV, provideMemevents,
	kfetch, genSrcPicked, genImport, genRestore, provideGenimport, fnFactsText, provideFnfacts, edPanelShown,
	provideSrcedit, bmLineStar, bmFnNames, bmTip, provideBookmarks, covRun, covRender, provideCoverage, MMAP,
	provideMemmap, v86Restore, provideCpustate, rbSync, provideRebuild, snapKernelKey, snapWorker,
	provideSnapshots };
