"use strict";

// The page's entry (index.html loads it): init() wires the page's controls
// to the modules that do the work, reads the URL's options and loads the
// first kernel. What the page does is in the modules it imports (README.md,
// "Scripts: ES modules, no globals"); what they share is page.js's state
// store and API.

// Its own exports, for window.MKB (debug.js).
import * as app from "./app.js";

import { PAGE, $, withV } from "./util.js";
import { setStartPaused, setPrefSpeed, kernels, tab, selectedPage, setKernels } from "./page.js";
import { exposeMKB } from "./debug.js";
import { initJumps } from "./jumpview.js";
import { renderGraphInfo } from "./graph.js";
import { initGraph } from "./graphinit.js";
import { dbgOnChange } from "./dbginfo.js";
import { initSource } from "./source.js";
import { initSrcEdit } from "./srcedit.js";
import { initCoverage } from "./coverage.js";
import { initImport, setCmdlineFromUrl, showImported } from "./symimport.js";
import { MEV } from "./memevents.js";
import { MMAP } from "./memmap.js";
import { QUESTS } from "./quests.jsx";
import { setupVars } from "./vars.js";
import { initSnapshots } from "./snapshots.js";
import { initReplay } from "./replay.js";
import { initReverse } from "./reverse.js";
import { initBookmarks } from "./bminit.js";
import { initRebuild } from "./rebuild.jsx";
import { initHotPatch } from "./hotpatch.jsx";
import { setStatus } from "./machtab.js";
import { setAutoscan, scan } from "./scan.js";
import { initLegend, drawMaps, pageAt, showHover } from "./memview.js";
import { selectPage } from "./listing.js";
import { watchPanicText, watchJitEnd, sampleSpeed, SPEED_SAMPLE_MS, loadVersion, updateDebugUi, initRunBadge,
	SPEED_KEY, MIPS_KEY, renderContext, COUNT_KEY, syncSrcToggle } from "./debugui.js";
import { initCompactTabs, readHash } from "./tabs.jsx";
import { initNav } from "./nav.js";
import { listingStale } from "./symview.js";
import { onVisibility } from "./run.js";
import { prefetchWasm } from "./boot.js";
import { initTips } from "./tips.js";
import { loadKernel } from "./kernel.js";
import { SPEEDS, CLOCKS, setSettings, addRam } from "./machtab.js";
import { mountShell } from "./shell.jsx";

async function init() {
	// The kernel list and v86.wasm are fetched while the page sets itself up.
	prefetchWasm();
	const kernelsJson = fetch(withV("kernels.json")).then((r) => r.json());
	// The React components first (shell.jsx): their controls exist from here on.
	mountShell();
	const params = new URLSearchParams(location.search);
	MEV.panel();
	MMAP.init(); // memmap.js
	QUESTS.init(); // quests.js
	// ?mem=BYTES may name any multiple of 4 KiB, for probing the minimum.
	let memParam = null;
	const m = Number(params.get("mem"));
	if (Number.isInteger(m) && m > 0 && m % PAGE === 0) {
		memParam = String(m);
		addRam(memParam);
	}
	setStartPaused(params.get("paused") === "1");
	const options = SPEEDS.map(([v]) => v);
	let stored = null;
	try {
		stored = localStorage.getItem(SPEED_KEY);
	} catch (e) {
		stored = null;
	}
	setPrefSpeed([params.get("speed"), stored, "1000"].find((v) => options.includes(v)));
	const clocks = CLOCKS.map(([v]) => v);
	let storedMips = null;
	try {
		storedMips = localStorage.getItem(MIPS_KEY);
	} catch (e) {
		storedMips = null;
	}
	setSettings({ clock: [params.get("mips"), storedMips, "10"].find((v) => clocks.includes(v)) });
	initTips();
	initLegend();
	drawMaps();
	initReverse();
	initGraph();
	initImport();
	setInterval(watchPanicText, 250);
	setInterval(watchJitEnd, 250);
	setInterval(sampleSpeed, SPEED_SAMPLE_MS);
	document.addEventListener("visibilitychange", onVisibility);
	document.addEventListener("resume", onVisibility);
	loadVersion();
	if (params.has("cmdline"))
		setCmdlineFromUrl(params.get("cmdline"));
	let storedCount = null;
	try {
		storedCount = localStorage.getItem(COUNT_KEY);
	} catch (e) {
		storedCount = null;
	}
	if (params.get("count") === "0" || (!params.has("count") && storedCount === "0"))
		setSettings({ count: false });
	initSource();
	initSrcEdit();
	initRebuild();
	initHotPatch();
	initJumps();
	initCoverage();
	setupVars();
	dbgOnChange(() => {
		syncSrcToggle();
		showImported();
		if (tab === "debugger")
			renderContext();
		if (tab === "page")
			listingStale();
		renderGraphInfo();
	});
	updateDebugUi();
	initCompactTabs();
	initBookmarks(); // before the menu: its keys in the dialog first
	initRunBadge();
	window.addEventListener("hashchange", readHash);
	readHash();
	initNav();
	$("scan").addEventListener("click", () => scan());
	$("autoscan").addEventListener("change", setAutoscan);
	initSnapshots();
	initReplay();
	const canvas = $("pages");
	canvas.addEventListener("click", (ev) => {
		const p = pageAt(ev);
		if (p >= 0)
			selectPage(p);
	});
	canvas.addEventListener("mousemove", (ev) => showHover(pageAt(ev)));
	canvas.addEventListener("mouseleave", () => showHover(selectedPage));

	try {
		setKernels((await kernelsJson).kernels);
	} catch (e) {
		setStatus("No kernels.json (" + e + "). Load a bzImage from disk.");
		return;
	}
	const wanted = kernels.find((k) => k.name === params.get("kernel")) || kernels[0];
	setSettings({ kernels: kernels.map((k) => [k.name, k.label || k.name]), kernel: wanted ? wanted.name : "" });
	if (!wanted) {
		setStatus("kernels.json lists no kernels. Load a bzImage from disk.");
		return;
	}
	await loadKernel(wanted, memParam);
}

// window.MKB, the page's one global (debug.js).
exposeMKB(app);

init().catch((e) => setStatus(String(e)));
