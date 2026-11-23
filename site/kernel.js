"use strict";

// Loading a kernel: its bzImage, symbols.json and disasm.json.

import { kib, withV, httpWhy } from "./util.js";
import { image, setImage, setSymbols, setDisasm, initrd, setInitrd, kernels, api, provide } from "./page.js";
import { dbgUseShards, dbgFresh } from "./dbginfo.js";
import { srcUseShipped, srcFresh } from "./srctext.js";
import { setSettings, ramHas, ramSizes, setStatus } from "./machtab.js";
import { ramForInitrd, initrdNote, showImported, restoreImported, restoreCmdline, analyzeInitrd } from "./symimport.js";
import { renderSnapshots } from "./snapshots.js";
import { ram, parseHeader, DEFAULT_RAM, showHeader, kernelEnd } from "./header.js";
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
	let mem = memFromUrl || String(DEFAULT_RAM);
	// A kernel that needs more than the default (a distro kernel decompresses
	// to 16 MiB and beyond) gets the smallest listed size that fits it.
	if (!memFromUrl) {
		const need = kernelEnd(image.hdr);
		const opts = ramSizes().sort((a, b) => a - b);
		const fit = opts.find((v) => v >= need);
		if (need > DEFAULT_RAM)
			mem = String(fit || opts[opts.length - 1]);
	}
	setSettings({ ram: mem });
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

// The latest kernel asked for: a load overtaken by another pick stops at
// its next step (A3b: hello's late files replaced the picked kernel's).
let loadGen = 0;

async function loadKernel(entry, memFromUrl) {
	const gen = ++loadGen;
	const gone = () => gen !== loadGen;
	api.source.srcKernelSwitch(entry.name);
	setStatus("Fetching " + entry.path + "...");
	// The bzImage, its disassembly and its symbols are fetched at once (each
	// one alone was a round trip on the way to the boot); they are used in
	// the same order as before.
	const respP = fetch(withV(entry.path)), disasmP = loadDisasm(entry), symbolsP = loadSymbols(entry);
	const resp = await respP;
	if (gone())
		return;
	if (!resp.ok)
		throw new Error(entry.path + ": " + httpWhy(resp));
	const bytesP = resp.arrayBuffer();
	const dis = await disasmP;
	if (gone())
		return;
	setDisasm(dis);
	await srcUseShipped(entry);
	if (gone())
		return;
	dbgUseShards(entry);
	syncSrcToggle();
	// kernels.json may give a kernel its own RAM size (meta.json "ram").
	const ramDefault = entry.ram && ramHas(entry.ram) ? String(entry.ram) : null;
	// kernels.json "initrd": the initrd that comes with this entry. One the
	// user loaded stays; one from another entry goes.
	if (entry.initrd && (!initrd || initrd.entry)) {
		setStatus("Fetching " + entry.initrd + "...");
		const r = await fetch(withV(entry.initrd));
		if (gone())
			return;
		if (!r.ok)
			throw new Error(entry.initrd + ": " + httpWhy(r));
		const name = entry.initrd.split("/").pop();
		const bytes = new Uint8Array(await r.arrayBuffer());
		if (gone())
			return;
		const info = await analyzeInitrd(bytes, name);
		if (gone())
			return;
		setInitrd({ name, bytes, entry: entry.name, info });
	} else if (!entry.initrd && initrd && initrd.entry)
		setInitrd(null);
	const kernelBytes = await bytesP, syms = await symbolsP;
	if (gone())
		return;
	useImage(entry.name, kernelBytes, memFromUrl || ramDefault, syms);
	showImported();
	await restoreImported();
	if (gone())
		return;
	await restoreCmdline();
	if (gone())
		return;
	await boot();
}

// The source and DWARF shards kernels.json lists for entry, for the shown
// image (an unchanged rebuild of a kit without debug sections is the
// shipped kernel's bytes: rebuild.jsx, plan E6).
async function shippedDebugFor(entry) {
	await srcUseShipped(entry);
	dbgUseShards(entry);
	syncSrcToggle();
}

// The Machine tab's Kernel menu: that kernel, loaded and booted.
function chooseKernel(name) {
	setSettings({ kernel: name });
	const k = kernels.find((x) => x.name === name);
	if (k)
		loadKernel(k).catch((e) => setStatus(String(e)));
}

// The RAM menu: the header check and the memory map again (Boot uses it).
function chooseRam(v) {
	setSettings({ ram: v });
	showHeader();
	drawMaps();
}

// From disk: a bzImage file, booted (without disasm.json, C source or
// DWARF, until its vmlinux is imported).
async function bootFromDisk(f) {
	const gen = ++loadGen;
	api.source.srcKernelSwitch(f.name);
	setDisasm(null);
	srcFresh(null, "no C source for a bzImage from disk");
	dbgFresh(null, "no debug info for a bzImage from disk: import its unstripped vmlinux for file:line");
	syncSrcToggle();
	const bytes = await f.arrayBuffer();
	if (gen !== loadGen)
		return;
	useImage(f.name, bytes);
	showImported();
	await restoreImported();
	if (gen !== loadGen)
		return;
	await restoreCmdline();
	if (gen !== loadGen)
		return;
	await boot();
}

// The page API's entries of this module (page.js).
provide({ loadKernel, useImage });

export { loadKernel, useImage, loadSymbols, loadDisasm, shippedDebugFor, chooseKernel, chooseRam, bootFromDisk };
