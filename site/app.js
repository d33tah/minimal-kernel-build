"use strict";

// The page's entry (index.html loads it): init() wires the page's controls
// to the modules that do the work, reads the URL's options and loads the
// first kernel. What the page does is in the modules it imports (README.md,
// "Scripts: ES modules, no globals"); what they share is page.js's state
// store and API.

// Its own exports, for window.MKB (debug.js).
import * as app from "./app.js";

import { PAGE, $, kib, withV } from "./util.js";
import { setStartPaused, setPrefSpeed, kernels, tab, selectedPage, setDisasm, setKernels } from "./page.js";
import { exposeMKB } from "./debug.js";
import { initJumps } from "./jumps.js";
import { renderGraphInfo } from "./graph.js";
import { initGraph } from "./graphinit.js";
import { dbgOnChange, dbgFresh } from "./dbginfo.js";
import { srcFresh } from "./srctext.js";
import { initSource } from "./source.js";
import { initSrcEdit } from "./srcedit.js";
import { initCoverage } from "./coverage.js";
import { initImport, initInitrd, setCmdlineFromUrl, showImported, restoreImported, restoreCmdline } from "./symimport.js";
import { liveDecode, loadIced } from "./livedis.js";
import { MEV } from "./memevents.js";
import { MMAP } from "./memmap.js";
import { QUESTS } from "./quests.jsx";
import { setupVars } from "./vars.js";
import { initSnapshots } from "./snapshots.js";
import { initReplay } from "./replay.js";
import { initReverse } from "./reverse.js";
import { initBookmarks } from "./bookmarks.js";
import { initRebuild } from "./rebuild.jsx";
import { showHeader, setStatus } from "./header.js";
import { setAutoscan, scan } from "./scan.js";
import { initLegend, drawMaps, gridRam, pageAt, showHover } from "./memview.js";
import { inspect, setLive, selectPage } from "./listing.js";
import { watchPanicText, watchJitEnd, sampleSpeed, SPEED_SAMPLE_MS, loadVersion, updateDebugUi, initRunBadge,
	SPEED_KEY, MIPS_KEY, renderContext, COUNT_KEY, syncSrcToggle } from "./debugui.js";
import { initCompactTabs, readHash } from "./tabs.jsx";
import { initNav } from "./nav.js";
import { listingStale } from "./symview.js";
import { onVisibility } from "./run.js";
import { initDebugger } from "./runto.js";
import { boot, prefetchWasm } from "./boot.js";
import { initTips } from "./tips.js";
import { loadKernel, useImage } from "./kernel.js";
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
		if (![...$("memory").options].some((o) => o.value === memParam))
			$("memory").add(new Option(kib(m), memParam));
	}
	setStartPaused(params.get("paused") === "1");
	const options = [...$("dbg_speed").options].map((o) => o.value);
	let stored = null;
	try {
		stored = localStorage.getItem(SPEED_KEY);
	} catch (e) {
		stored = null;
	}
	setPrefSpeed([params.get("speed"), stored, "1000"].find((v) => options.includes(v)));
	const clocks = [...$("clock_mips").options].map((o) => o.value);
	let storedMips = null;
	try {
		storedMips = localStorage.getItem(MIPS_KEY);
	} catch (e) {
		storedMips = null;
	}
	$("clock_mips").value = [params.get("mips"), storedMips, "10"].find((v) => clocks.includes(v));
	initTips();
	initLegend();
	drawMaps();
	initDebugger();
	initReverse();
	initGraph();
	initImport();
	$("live_decode").addEventListener("change", () => { if (liveDecode()) loadIced(); inspect(); renderContext(); });
	initInitrd();
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
		$("count").checked = false;
	initSource();
	initSrcEdit();
	initRebuild();
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
		if (typeof renderGraphInfo === "function")
			renderGraphInfo();
	});
	updateDebugUi();
	initCompactTabs();
	initBookmarks(); // before the menu: its keys in the dialog first
	initRunBadge();
	window.addEventListener("hashchange", readHash);
	readHash();
	initNav();
	$("page_prev").addEventListener("click", () => selectedPage > 0 && selectPage(selectedPage - 1));
	$("page_next").addEventListener("click", () => selectedPage >= 0 && selectedPage + 1 < gridRam() / PAGE && selectPage(selectedPage + 1));
	$("reread").addEventListener("click", inspect);
	$("memory").addEventListener("change", () => { showHeader(); drawMaps(); });
	$("boot").addEventListener("click", boot);
	$("scan").addEventListener("click", () => scan());
	$("autoscan").addEventListener("change", setAutoscan);
	$("live").addEventListener("change", setLive);
	// Bytes per row and columns follow the listing's width.
	let dumpWidth = 0;
	new ResizeObserver(() => {
		const w = $("listing").clientWidth;
		if (w !== dumpWidth) {
			dumpWidth = w;
			requestAnimationFrame(inspect);
		}
	}).observe($("listing"));
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
	$("kernel").addEventListener("change", () => {
		const k = kernels.find((x) => x.name === $("kernel").value);
		if (k)
			loadKernel(k).catch((e) => setStatus(String(e)));
	});
	$("file").addEventListener("change", async () => {
		const f = $("file").files[0];
		if (!f)
			return;
		setDisasm(null); // a file from disk comes without disasm.json, C source or DWARF
		srcFresh(null, "no C source for a bzImage from disk");
		dbgFresh(null, "no debug info for a bzImage from disk: import its unstripped vmlinux for file:line");
		syncSrcToggle();
		useImage(f.name, await f.arrayBuffer());
		showImported();
		await restoreImported();
		await restoreCmdline();
		await boot();
	});

	try {
		setKernels((await kernelsJson).kernels);
	} catch (e) {
		setStatus("No kernels.json (" + e + "). Load a bzImage from disk.");
		return;
	}
	for (const k of kernels)
		$("kernel").add(new Option(k.label || k.name, k.name));
	const wanted = kernels.find((k) => k.name === params.get("kernel")) || kernels[0];
	if (!wanted) {
		setStatus("kernels.json lists no kernels. Load a bzImage from disk.");
		return;
	}
	$("kernel").value = wanted.name;
	await loadKernel(wanted, memParam);
}

// window.MKB, the page's one global (debug.js).
exposeMKB(app);

init().catch((e) => setStatus(String(e)));
