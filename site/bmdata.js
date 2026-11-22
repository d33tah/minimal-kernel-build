"use strict";
// The bookmarks' data (bookmarks.js shows them): the list and its
// storage, the kernel a bookmark is for, where a bookmark of another build
// is in this one (E8), and the drafts the add dialog starts from (what is
// under the pointer, the machine's position). The bookmark's format is in
// bookmarks.js's header.

import { hex, PAGE, num } from "./util.js";
import { image, symbols, pageClass, stepper, emulator, runState, tab, selectedPage, api } from "./page.js";
import { jumpHit } from "./jumps.js";
import { graphNodeAt, selectedNode } from "./graphnav.js";
import { asmTargetName } from "./asmnames.js";
import { srcPanel } from "./source.js";
import { kernelSymLookup } from "./livedis.js";
import { snapKernelName, snapshots } from "./snaprecord.js";
import { snapKernelKey } from "./snapshots.js";
import { runNow } from "./replay.js";
import { DBG, dbgFrames, dbgLineCode } from "./dbginfo.js";
import { srcFile } from "./srctext.js";
import { textKeep, buildFamily, recFamily, mapLine } from "./buildmap.js";
import { srcOpen } from "./srcview.js";

const BM_KEY = "mkb-bookmarks";
const BM_LONG_MS = 600;   // a long press (touch) opens the add dialog
const BM_KINDS = { code: "instruction", page: "memory page", line: "source line", file: "source file", fn: "function", pos: "machine position" };

const BM = {
	list: [],
	stored: true,       // localStorage works
	why: "",            // why not
	q: "",              // the list's filter
	draft: null,        // the add dialog's bookmark
	pending: null,      // { b, until }: jump there once its kernel runs (a link, a switch)
	hover: null,        // the element under the mouse (the B key's target)
	longAt: -Infinity,  // realNow() of the long press that opened the dialog, until the next press
	marks: null,        // this kernel's targets: see bmMarks
	keyWait: null,      // the identity promise being waited for
	undo: null,         // the last deleted bookmark
	changed: null,      // after any change: bookmarks.js's bmChanged (the list, the marks)
};

// ---- storage ----------------------------------------------------------------

function bmLoad() {
	try {
		const j = JSON.parse(localStorage.getItem(BM_KEY) || "null");
		BM.list = j && Array.isArray(j.bookmarks) ? j.bookmarks.filter(bmValid) : [];
		BM.stored = true;
	} catch (e) {
		BM.stored = false;
		BM.why = "this browser keeps no data for the page (" + ((e && e.name) || e) + ")";
	}
}

function bmSave() {
	try {
		localStorage.setItem(BM_KEY, JSON.stringify({ format: "mkb-bookmarks", version: 1, bookmarks: BM.list }));
		BM.stored = true;
	} catch (e) {
		BM.stored = false;
		BM.why = "storing them failed (" + ((e && e.name) || e) + ")";
	}
	BM.changed();
}

function bmValid(b) {
	return b && typeof b === "object" && typeof b.name === "string" && BM_KINDS[b.kind] && b.t && typeof b.t === "object";
}

// ---- kernel identity ----------------------------------------------------------

// This kernel's identity, null while it is computed (BM.changed runs again then).
function bmKey() {
	const k = snapKernelKey();
	if (!k && image && image.snapKeyP && BM.keyWait !== image.snapKeyP) {
		BM.keyWait = image.snapKeyP;
		image.snapKeyP.then(() => BM.changed());
	}
	return k;
}

// "this": a bookmark of the loaded kernel; "build": of another build of
// it (E8: placed here by bmPlace); "other": of another kernel. While the
// identity is computed, the kernel's name decides.
function bmGroup(b) {
	const k = bmKey();
	if (k ? b.kernelKey === k : image && b.kernel === snapKernelName())
		return "this";
	return image && recFamily(b) === buildFamily() ? "build" : "other";
}

// ---- across builds (E8) ---------------------------------------------------------

// The function holding pa and the offset into it (the kernel's own
// symbols, not a hot patch's copies), and pa's C line with the version of
// its file's text: what a bookmark of code keeps for another build.
function bmAnchor(pa) {
	const out = {};
	const s = symbols && pa >= 0 ? api.symbols.symbolAt(symbols.symbols.filter((x) => !x.patch), pa) : null;
	if (s && pa - s.paddr < Math.max(1, s.size))
		Object.assign(out, { fn: s.name, off: pa - s.paddr, fsize: s.size });
	const r = pa >= 0 ? dbgFrames(pa, true) : null;
	const f = r && r.state === "ok" ? r.frames[0] : null;
	if (f && f.file && f.line)
		out.src = { path: f.file, line: f.line, ...bmLineAnchor(f.file, f.line) };
	return out;
}

// The version of path's text shown (its hash) and line's text, when loaded.
function bmLineAnchor(path, line) {
	const f = srcFile(path);
	return f.state === "ok" ? { fh: textKeep(f.lines.join("\n")), txt: f.lines[line - 1] || "" } : {};
}

// The kernel's own symbol named name.
const bmSym = (name) => (symbols ? symbols.symbols.find((x) => x.name === name && !x.patch) : null);

// Line `line` of path as counted on the text fh (txt its text), in the text
// shown now: { line, how } (buildmap.js mapLine), or null while the text loads.
function bmMapLine(path, line, fh, txt) {
	const f = srcFile(path);
	if (f.state === "loading")
		return null;
	return f.state === "ok" ? mapLine(line, fh, txt, f.lines.join("\n")) : { line, how: "kept" };
}

const BM_HOW = { same: "the same line", moved: "moved by the edit", changed: "the edit changed that line: the next line it kept",
	text: "found by its text", kept: "the same number (its file's old text is not known here)" };

// Where bookmark b is in the kernel shown: { t, how, gone } with t of b's
// kind; for a bookmark of another build of it, placed by function name and
// offset and by file:line through the edit (how says by what, gone why
// not at all). Kept until the bookmarks, the DWARF or the kernel change.
function bmPlace(b) {
	// This build's: its addresses hold; a line moves when the panel shows
	// another text of its file (a hot patch's).
	const g = bmGroup(b);
	if (g === "other" || (g === "this" && (b.kind !== "line" || !b.t.fh)))
		return { t: b.t, how: "" };
	const key = BM.version + "|" + DBG.version + "|" + bmKey();
	const had = BM.placed && BM.placed.get(b);
	if (had && had.key === key)
		return had.p;
	const p = bmPlaceNow(b);
	if (!p.wait) {
		if (!BM.placed)
			BM.placed = new WeakMap();
		BM.placed.set(b, { key, p });
	}
	return p;
}

function bmPlaceNow(b) {
	const t = b.t;
	if (b.kind === "line") {
		const m = bmMapLine(t.path, t.line, t.fh, t.txt);
		return m ? { t: { ...t, line: m.line }, how: m.how === "same" ? "" : "at line " + m.line + " (line " + t.line + " there, " + BM_HOW[m.how] + ")" } :
			{ t, how: "reading " + t.path + "…", wait: true };
	}
	if (b.kind === "fn") {
		const s = bmSym(t.name);
		return s ? { t: { ...t, pa: s.paddr }, how: "by its name" + (s.paddr !== t.pa ? ", moved from " + hex(t.pa) : "") } :
			{ t, how: "", gone: "no " + t.name + "() in this build" };
	}
	if (b.kind === "code" || b.kind === "pos") {
		// Before E8 a position kept only "name+0xoff".
		const m = !t.fn && typeof t.sym === "string" ? /^([^+\s]+)(?:\+0x([0-9a-f]+))?$/.exec(t.sym) : null;
		const fn = t.fn || (m && m[1]), off = t.fn ? t.off : m ? parseInt(m[2] || "0", 16) : 0;
		const s = fn ? bmSym(fn) : null;
		if (!s)
			return { t, how: "", gone: fn ? "no " + fn + "() in this build" : "made before builds were tracked: no function to place it by" };
		// The function changed: its C line through the edit, the first
		// breakpoint address of that line inside it.
		if (t.src && t.fsize !== undefined && t.fsize !== s.size) {
			const ml = bmMapLine(t.src.path, t.src.line, t.src.fh, t.src.txt);
			const lc = ml ? dbgLineCode(t.src.path, ml.line) : null;
			const bp = lc && lc.state === "ok" ? lc.bps.find((a) => a >= s.paddr && a < s.paddr + s.size) : undefined;
			if (bp !== undefined)
				return { t: { ...t, pa: bp, sym: fn + (bp > s.paddr ? "+" + hex(bp - s.paddr) : "") },
					how: "by its line " + t.src.path + ":" + ml.line + " in " + fn + "() (" + BM_HOW[ml.how] + ")" };
			if (!ml || (lc && lc.state === "loading"))
				return { t: { ...t, pa: s.paddr + Math.min(off, s.size - 1) }, how: "reading the line table…", wait: true };
		}
		if (off >= Math.max(1, s.size))
			return { t, how: "", gone: fn + "() is shorter in this build than the offset " + hex(off) };
		return { t: { ...t, pa: s.paddr + off }, how: "by " + fn + "+" + hex(off) + (t.fsize !== undefined && t.fsize !== s.size ? " (" + fn + "() changed size)" : "") };
	}
	return { t, how: "" };
}

// ---- what a bookmark points at -----------------------------------------------

const bmSymName = (pa) => (symbols ? asmTargetName(pa, kernelSymLookup()) : "");

function bmTargetText(b) {
	const t = b.t;
	switch (b.kind) {
	case "code": {
		// The name it had when added: another kernel's symbols are not loaded.
		const s = t.sym !== undefined ? t.sym : bmSymName(t.pa);
		return (s ? s + ", " : "") + hex(t.pa);
	}
	case "page":
		return "page " + hex(t.page) + " (" + hex(t.page * PAGE) + ")";
	case "line":
		return t.path + ":" + t.line;
	case "file":
		return t.path + " (whole file)";
	case "fn":
		return t.name + "() at " + hex(t.pa);
	case "pos":
		return "EIP " + (t.sym || hex(t.eip >>> 0)) + " after " + num(typeof t.steps === "number" ? t.steps : t.icount) + " instructions" + (t.snapName ? ", snapshot " + t.snapName : "");
	}
	return "";
}

function bmDraftCode(pa) {
	const s = bmSymName(pa);
	return { kind: "code", t: { pa, sym: s, ...bmAnchor(pa) }, name: s || hex(pa) };
}

function bmDraftPage(p) {
	const label = pageClass.length ? api.memory.pageLabel(p) : "";
	return { kind: "page", t: { page: p }, name: "page " + hex(p) + (label ? " " + label : "") };
}

function bmDraftFn(pa, name) {
	return { kind: "fn", t: { pa, name }, name };
}

function bmDraftLine(path, line) {
	return line ? { kind: "line", t: { path, line, ...bmLineAnchor(path, line) }, name: path.replace(/^.*\//, "") + ":" + line } :
		{ kind: "file", t: { path }, name: path };
}

// The machine's position: EIP and the stepper's count, the one the Machine
// tab shows (X2; bookmarks from before X2 have only v86's counter, icount,
// and show that). A snapshot of this kernel taken at the same v86 counter is
// tied to it.
function bmDraftPos() {
	if (!stepper || !emulator || runState === "off")
		return null;
	const c = emulator.v86.cpu;
	const pa = stepper.nextPhys(), eip = stepper.regs().eip >>> 0, icount = c.instruction_counter[0] >>> 0;
	const i = pa >= 0 ? stepper.symbolOf(pa) : -1;
	const sym = i >= 0 ? stepper.symName[i] + (pa > stepper.symStart[i] ? "+" + hex(pa - stepper.symStart[i]) : "") : "";
	const key = bmKey();
	const snap = snapshots.find((s) => s.meta && s.meta.icount === icount && key && s.kernelKey === key);
	// The run up to here (J4): Replay boots again and replays it to here.
	const run = runNow();
	const steps = stepper.steps;
	return { kind: "pos", t: { pa, eip, steps, icount, sym, snap: snap ? snap.id : null, snapName: snap ? snap.name : "", ...(run ? { run } : {}), ...bmAnchor(pa) },
		name: snapKernelName() + " @" + steps.toLocaleString("en") + (sym ? " " + sym : " " + hex(eip)) };
}

// A function's start and name for code at pa (a call stack frame).
function bmFnOf(pa) {
	if (!stepper || pa < 0)
		return null;
	const i = stepper.symbolOf(pa);
	return i >= 0 ? { pa: stepper.symStart[i], name: stepper.symName[i] } : null;
}

// The bookmark for what is under element t (at client point x, y), or null.
function bmDraftAt(t, x, y) {
	if (!t || !t.closest)
		return null;
	let e = t.closest("#listing [data-addr].ins, #context [data-addr].ins, #listing .hex[data-addr]");
	if (e)
		return bmDraftCode(Number(e.dataset.addr));
	e = t.closest("#listing .lbl[data-addr]");
	if (e) {
		const pa = Number(e.dataset.addr), f = bmFnOf(pa);
		return f && f.pa === pa && f.name === e.dataset.sym ? bmDraftFn(pa, f.name) : bmDraftCode(pa);
	}
	e = t.closest("#src_body [data-line]");
	if (e && srcPanel)
		return bmDraftLine(srcPanel.path, Number(e.dataset.line));
	e = t.closest("#cov_tree .covrow[data-kind]");
	if (e)
		return e.dataset.kind === "file" ? bmDraftLine(e.dataset.path, 0) :
			e.dataset.kind === "func" && e.dataset.pa ? bmDraftFn(Number(e.dataset.pa), e.dataset.fn) : null;
	e = t.closest("#call_stack .cframe[data-fnpa]");
	if (e)
		return bmDraftFn(Number(e.dataset.fnpa), e.dataset.fn);
	if (t.closest("#pages")) {
		const p = api.memory.pageAt({ clientX: x, clientY: y });
		return p >= 0 ? bmDraftPage(p) : null;
	}
	if (t.closest("#graph_canvas")) {
		const node = graphNodeAt(x, y);
		return node >= 0 ? bmDraftFn(stepper.symStart[node], stepper.symName[node]) : null;
	}
	return null;
}

// The element a press is on: Chromium may give a touch to an ancestor
// (a long press on a function's label line in the Page tab reached #listing
// as its target, CI run 43509415990), so the element under the point when
// the target itself is nothing to bookmark.
function bmPressTarget(ev) {
	const t = ev.target;
	if (bmDraftAt(t, ev.clientX, ev.clientY))
		return t;
	const under = document.elementFromPoint(ev.clientX, ev.clientY);
	return under && t && t.contains && t.contains(under) ? under : t;
}

// What the tab's ☆ (and the B key with nothing under the pointer) adds.
function bmDraftHere(where) {
	if (where === "src" && srcPanel && srcOpen())
		return bmDraftLine(srcPanel.path, srcPanel.whole && !srcPanel.pick ? 0 : srcPanel.pick || srcPanel.line);
	if (where === "eip" || (where === "tab" && tab === "debugger")) {
		const pa = stepper && runState !== "off" && runState !== "jit" ? stepper.nextPhys() : -1;
		return pa >= 0 ? bmDraftCode(pa) : null;
	}
	if (where === "page" || (where === "tab" && tab === "page")) {
		if (selectedPage < 0)
			return null;
		const at = jumpHit >= 0 && Math.floor(jumpHit / PAGE) === selectedPage ? jumpHit : api.show.navListingAnchor();
		return at >= 0 ? bmDraftCode(at) : bmDraftPage(selectedPage);
	}
	if (where === "mem" || (where === "tab" && tab === "memory")) {
		const pa = stepper && runState !== "off" ? stepper.nextPhys() : -1;
		const p = selectedPage >= 0 ? selectedPage : pa >= 0 ? Math.floor(pa / PAGE) : -1;
		return p >= 0 ? bmDraftPage(p) : null;
	}
	if (where === "calls" || (where === "tab" && tab === "calls")) {
		const node = selectedNode();
		return node >= 0 && node < stepper.symName.length ? bmDraftFn(stepper.symStart[node], stepper.symName[node]) : null;
	}
	return bmDraftPos();
}

export { BM_KEY, BM_LONG_MS, BM_KINDS, BM, bmLoad, bmSave, bmValid, bmKey, bmGroup, bmPlace, bmTargetText, bmDraftFn,
	bmDraftLine, bmDraftPos, bmDraftAt, bmPressTarget, bmDraftHere };
