"use strict";

// Loading a kernel: its bzImage, symbols.json and disasm.json.

import { kib, $, withV, httpWhy } from "./util.js";
import { image, setImage, setSymbols, setDisasm, initrd, setInitrd, provide } from "./page.js";
import { dbgUseShards } from "./dbginfo.js";
import { srcUseShipped } from "./source.js";
import { ramForInitrd, initrdNote, showImported, restoreImported, restoreCmdline, analyzeInitrd } from "./symimport.js";
import { renderSnapshots } from "./snapshots.js";
import { setStatus, ram, parseHeader, DEFAULT_RAM, showHeader, kernelEnd } from "./header.js";
import { drawMaps } from "./memview.js";
import { syncSrcToggle } from "./debugui.js";
import { boot } from "./boot.js";

function reportError(err) {
	setStatus("Emulator error: " + err + (image ? " (" + image.name + ", " + kib(ram()) + " RAM)" : ""));
}
window.addEventListener("error", (e) => {
	// Benign: a resize re-render changed the layout within the same frame.
	if (!/ResizeObserver loop/.test(e.message))
		reportError(e.message);
});
window.addEventListener("unhandledrejection", (e) => reportError(e.reason));

// Every kernel starts with 2 MB; the header check says when that is too little.
function useImage(name, buffer, memFromUrl, syms) {
	const bytes = new Uint8Array(buffer);
	setImage({ name, bytes, hdr: parseHeader(bytes) });
	setSymbols(syms || null);
	$("memory").value = memFromUrl || String(DEFAULT_RAM);
	// A kernel that needs more than the default (a distro kernel decompresses
	// to 16 MiB and beyond) gets the smallest listed size that fits it.
	if (!memFromUrl) {
		const need = kernelEnd(image.hdr);
		const opts = Array.from($("memory").options, (o) => Number(o.value)).sort((a, b) => a - b);
		const fit = opts.find((v) => v >= need);
		if (need > DEFAULT_RAM)
			$("memory").value = String(fit || opts[opts.length - 1]);
	}
	ramForInitrd(); // keeps a loaded initrd: v86 needs RAM above 64 MiB + its size
	initrdNote();
	showHeader();
	drawMaps();
	renderSnapshots(); // which snapshots are for this kernel
}

// symbols.json is optional: without it the grid shows regions only.
async function loadSymbols(entry) {
	if (!entry.symbols)
		return null;
	try {
		const resp = await fetch(withV(entry.symbols));
		if (!resp.ok)
			return null;
		const s = await resp.json();
		s.symbols.sort((a, b) => a.paddr - b.paddr || a.name.localeCompare(b.name));
		return s;
	} catch (e) {
		console.warn("symbols:", e);
		return null;
	}
}

async function loadDisasm(entry) {
	if (!entry.disasm)
		return null;
	try {
		const resp = await fetch(withV(entry.disasm));
		return resp.ok ? await resp.json() : null;
	} catch (e) {
		console.warn("disasm:", e);
		return null;
	}
}

async function loadKernel(entry, memFromUrl) {
	setStatus("Fetching " + entry.path + "...");
	// The bzImage, its disassembly and its symbols are fetched at once (each
	// one alone was a round trip on the way to the boot); they are used in
	// the same order as before.
	const respP = fetch(withV(entry.path)), disasmP = loadDisasm(entry), symbolsP = loadSymbols(entry);
	const resp = await respP;
	if (!resp.ok)
		throw new Error(entry.path + ": " + httpWhy(resp));
	const bytesP = resp.arrayBuffer();
	setDisasm(await disasmP);
	await srcUseShipped(entry);
	dbgUseShards(entry);
	syncSrcToggle();
	// kernels.json may give a kernel its own RAM size (meta.json "ram").
	const ramDefault = entry.ram && [...$("memory").options].some((o) => o.value === String(entry.ram)) ? String(entry.ram) : null;
	// kernels.json "initrd": the initrd that comes with this entry. One the
	// user loaded stays; one from another entry goes.
	if (entry.initrd && (!initrd || initrd.entry)) {
		setStatus("Fetching " + entry.initrd + "...");
		const r = await fetch(withV(entry.initrd));
		if (!r.ok)
			throw new Error(entry.initrd + ": " + httpWhy(r));
		const name = entry.initrd.split("/").pop();
		setInitrd({ name, bytes: new Uint8Array(await r.arrayBuffer()), entry: entry.name });
		initrd.info = await analyzeInitrd(initrd.bytes, name);
	} else if (!entry.initrd && initrd && initrd.entry)
		setInitrd(null);
	useImage(entry.name, await bytesP, memFromUrl || ramDefault, await symbolsP);
	showImported();
	await restoreImported();
	await restoreCmdline();
	await boot();
}

// The page API's entries of this module (page.js).
provide({ loadKernel, useImage });

export { loadKernel, useImage };
