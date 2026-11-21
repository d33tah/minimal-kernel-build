"use strict";
// Bookmarks (K1): named places the user adds from many views and jumps
// back to. Uses the page's state and API (page.js: stepper, symbols, srcPanel, tab, ...) and
// S1's kernel identity (snapshots.js snapKernelKey).
//
// A bookmark: { id, name, kind, t, kernel, kernelKey, created, from }.
// kind and its target t:
//   code: { pa }           an instruction or address (Page listing, Code at EIP)
//   page: { page }         a 4 KiB page (Memory grid)
//   line: { path, line }   a source line (Source panel)
//   file: { path }         a whole source file (Source tab)
//   fn:   { pa, name }     a function (Calls box, call stack frame, Source tab)
//   pos:  { pa, eip, steps, icount, sym, snap, snapName }  the machine's position:
//         EIP's physical and virtual address, the stepper's count (the one
//         the Machine tab and snapshot names show), v86's instruction counter
//         (to find the snapshot of the same state) and the snapshot saved with
//         it, if any;
//         run: the run up to there (J4, replay.js), so Replay gets there again.
// kernelKey is S1's identity (SHA-256 of the bzImage and initrd): a
// bookmark is this kernel's when it matches, else another kernel's (listed
// apart with a switch). They are kept in localStorage (small JSON); without
// storage they last until the page is reloaded, and the list says so.
//
// Across builds (E8): build is the label of the build it was made on
// (buildmap.js; its kernel name less " (rebuilt here)" is the kernel the
// build was made from); a bookmark of another build of the
// same kernel (one rebuilt in the page, a hot patch, or the shipped one
// seen from a rebuild) is placed in this one by what stays: code and pos
// keep fn and off (the function holding the address and the offset into
// it) and src { path, line, fh, txt } (its C line, the version of that
// file's text and the line's text); line keeps fh and txt. Lines move
// through the diff of the two texts (bmPlace).

import { realNow } from "./clock.js";
import { hex, PAGE, num, $, escHtml } from "./util.js";
import { image, symbols, pageClass, stepper, emulator, runState, tab, selectedPage, grid, kernels, emuReady,
	emuRam, api, provide } from "./page.js";
import { jumpHit, setJumpHit, setJumpHitScroll } from "./jumps.js";
import { G } from "./graphdata.js";
import { graphNodeAt, selectedNode } from "./graphnav.js";
import { download, graphTick } from "./graph.js";
import { asmTargetName } from "./asmnames.js";
import { srcPanel, openSourceLine, openSourceFile, renderSourcePanel } from "./source.js";
import { kernelSymLookup } from "./livedis.js";
import { snapKernelName, snapshots } from "./snaprecord.js";
import { snapKernelKey, saveSnapshot, restoreSnapshot } from "./snapshots.js";
import { runNow, replayRun, runStatus } from "./replay.js";
import { setStatus } from "./header.js";
import { hideTip } from "./tips.js";
import { DBG, dbgFrames, dbgLineCode } from "./dbginfo.js";
import { srcFile } from "./srctext.js";
import { textKeep, buildFamily, recFamily, imageLabel, mapLine } from "./buildmap.js";

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
	bmChanged();
}

function bmValid(b) {
	return b && typeof b === "object" && typeof b.name === "string" && BM_KINDS[b.kind] && b.t && typeof b.t === "object";
}

// ---- kernel identity ----------------------------------------------------------

// This kernel's identity, null while it is computed (bmChanged runs again then).
function bmKey() {
	const k = snapKernelKey();
	if (!k && image && image.snapKeyP && BM.keyWait !== image.snapKeyP) {
		BM.keyWait = image.snapKeyP;
		image.snapKeyP.then(() => bmChanged());
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
	if (where === "src" && srcPanel && !$("src_panel").hidden)
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

// ---- marks --------------------------------------------------------------------

// This kernel's bookmarks by target: code and pos by address, fn by its
// start, page by number, line by "path:line", file by path.
function bmMarks() {
	// Another build's bookmarks are placed through the DWARF and the texts
	// (dbgChanged also follows a text loaded).
	const key = bmKey() + "|" + BM.list.length + "|" + BM.version + "|" + DBG.version + "|" + buildFamily();
	if (BM.marks && BM.marks.key === key)
		return BM.marks;
	const m = { key, code: new Map(), fn: new Map(), page: new Map(), line: new Map(), file: new Map() };
	const add = (map, k, b) => map.set(k, (map.get(k) || []).concat(b.name));
	for (const b of BM.list) {
		if (bmGroup(b) === "other")
			continue;
		// Another build's: where bmPlace puts it here (E8).
		const p = bmPlace(b);
		if (p.gone)
			continue;
		const t = p.t;
		if (b.kind === "code" || b.kind === "pos")
			add(m.code, t.pa, b);
		else if (b.kind === "fn")
			add(m.fn, t.pa, b);
		else if (b.kind === "page")
			add(m.page, t.page, b);
		else if (b.kind === "line")
			add(m.line, t.path + ":" + t.line, b);
		else if (b.kind === "file")
			add(m.file, t.path, b);
	}
	BM.marks = m;
	return m;
}

const bmTip = (names) => "Bookmark" + (names.length > 1 ? "s " : " ") + names.map((n) => "“" + n + "”").join(", ") +
	". The menu (bottom right) lists the bookmarks; pick one there to come back here.";

function bmStarHtml(names) {
	return names ? ' <span class="bmstar tipmark" tabindex="0" role="button" aria-label="' + escHtml(bmTip(names)) + '" data-tip="' +
		escHtml(bmTip(names)) + '">★</span>' : "";
}

function bmStarEl(names) {
	const s = document.createElement("span");
	s.className = "bmstar tipmark";
	s.tabIndex = 0;
	s.setAttribute("role", "button");
	s.setAttribute("aria-label", bmTip(names));
	s.dataset.tip = bmTip(names);
	s.textContent = "★";
	return s;
}

// The ★ of an instruction line (code at pa) or a symbol header (a function at pa).
const bmCodeStar = (pa) => (BM.list.length ? bmStarHtml(bmMarks().code.get(pa)) : "");
const bmFnStar = (pa) => (BM.list.length ? bmStarHtml(bmMarks().fn.get(pa)) : "");
const bmLineStar = (path, line) => (BM.list.length ? bmStarHtml(bmMarks().line.get(path + ":" + line)) : "");
// For the Calls tab: the names of the bookmarks of the function at pa, or undefined.
const bmFnNames = (pa) => (BM.list.length ? bmMarks().fn.get(pa) : undefined);

// A small ☆ button that adds a bookmark for draft() (a function: the target
// may change before the click).
function bmAddButton(draft, label) {
	const b = document.createElement("button");
	b.type = "button";
	b.className = "bmadd";
	b.textContent = "☆";
	b.title = label;
	b.setAttribute("aria-label", label);
	b.addEventListener("click", (ev) => {
		ev.stopPropagation();
		bmAsk(draft());
	});
	return b;
}

// The ★ of bookmarked pages over the Memory grid, positioned like M1's EIP box.
function bmRenderPages() {
	const box = $("pages_box");
	if (!box)
		return;
	for (const e of box.querySelectorAll(".bmpage"))
		e.remove();
	if (!BM.list.length || !grid.pages)
		return;
	const rows = Math.ceil(grid.pages / grid.cols);
	for (const [p, names] of bmMarks().page) {
		if (p >= grid.pages)
			continue;
		const s = bmStarEl(names);
		s.classList.add("bmpage");
		s.dataset.page = p;
		api.memory.memCellStyle(s, p, rows);
		box.append(s);
	}
}

// ---- the add dialog -------------------------------------------------------------

function bmAsk(draft) {
	if (!draft) {
		bmNote("Nothing to bookmark here: pick a line, a page, a box or a frame first.");
		return;
	}
	hideTip();
	api.show.runBadgeOpen(false);
	BM.draft = draft;
	const d = $("bm_dialog");
	d.dataset.kind = draft.kind;
	$("bm_what").textContent = BM_KINDS[draft.kind] + ": " + bmTargetText(draft);
	$("bm_name").value = draft.name;
	$("bm_snap_l").hidden = draft.kind !== "pos" || !!draft.t.snap || !emulator;
	$("bm_snap").checked = false;
	d.hidden = false;
	$("bm_name").focus();
	$("bm_name").select();
}

function bmDialogClose() {
	$("bm_dialog").hidden = true;
	BM.draft = null;
}

async function bmDialogSave() {
	const d = BM.draft;
	if (!d)
		return;
	const name = $("bm_name").value.trim() || d.name;
	const withSnap = d.kind === "pos" && !$("bm_snap_l").hidden && $("bm_snap").checked;
	bmDialogClose();
	const b = { id: "b" + Date.now().toString(36) + Math.floor(Math.random() * 36 ** 4).toString(36), name, kind: d.kind, t: { ...d.t },
		kernel: snapKernelName(), kernelKey: bmKey(), build: imageLabel(), created: Date.now() };
	BM.list.push(b);
	bmSave();
	bmNote("Added “" + name + "”.");
	if (!b.kernelKey && image && image.snapKeyP) {
		b.kernelKey = await image.snapKeyP;
		bmSave();
	}
	if (withSnap) {
		const before = snapshots.length ? snapshots[snapshots.length - 1] : null;
		await saveSnapshot();
		const s = snapshots.length ? snapshots[snapshots.length - 1] : null;
		if (s && s !== before) {
			b.t.snap = s.id;
			b.t.snapName = s.name;
			bmSave();
		}
	}
}

// ---- jumps --------------------------------------------------------------------

// Go to b's place: one history entry (Back returns). Another kernel's
// bookmark loads that kernel first and goes there when it runs.
function bmGo(b) {
	if (bmGroup(b) === "other") {
		bmSwitch(b);
		return;
	}
	// Another build's bookmark: where it is in this build (E8).
	const p = bmPlace(b);
	if (p.gone) {
		bmNote("“" + b.name + "” (" + bmTargetText(b) + ") is not in this build: " + p.gone + ".");
		return;
	}
	api.show.runBadgeOpen(false);
	const t = p.t;
	switch (b.kind) {
	case "code":
	case "pos":
		api.show.navJump(() => {
			setJumpHit(t.pa);
			setJumpHitScroll(true);
			api.show.jumpTo(t.pa);
		});
		break;
	case "page":
		api.show.selectPage(t.page);
		break;
	case "line":
		openSourceLine(t.path, t.line);
		break;
	case "file":
		openSourceFile(t.path);
		break;
	case "fn":
		api.show.openFunction(t.pa, t.name, true);
		break;
	}
	bmNote("At “" + b.name + "” (" + bmTargetText({ ...b, t }) + ")" + (p.how ? ", made on " + (b.build || "another build of " + recFamily(b)) +
		": placed " + p.how : "") + ".");
}

// Load the kernel b is for, then go to b.
function bmSwitch(b) {
	const k = kernels.find((x) => x.name === b.kernel);
	if (!k) {
		bmNote("“" + b.kernel + "” is not one of the listed kernels: load that bzImage from disk (Machine tab) to use “" + b.name + "”.");
		return;
	}
	BM.pending = { b, until: realNow() + 120000 };
	$("kernel").value = k.name;
	bmNote("Switching to " + k.name + " for “" + b.name + "”...");
	api.machine.loadKernel(k).catch((e) => {
		setStatus(String(e));
		if (BM.pending && BM.pending.b === b)
			BM.pending = null;
		bmNote("Switching to " + k.name + " for “" + b.name + "” failed: " + ((e && e.message) || e) + ".");
	});
}

// A pending jump (a link, a switch) once its kernel runs and is known.
function bmPendingTick() {
	const p = BM.pending;
	if (!p)
		return;
	if (realNow() > p.until) {
		BM.pending = null;
		return;
	}
	if (!emulator || !emuReady || !emuRam || !image || !stepper || runState === "off")
		return;
	const key = bmKey();
	if (!key || (p.b.kernelKey !== key && recFamily(p.b) !== buildFamily()))
		return;
	BM.pending = null;
	const b = BM.list.find((x) => x.id === p.b.id) || p.b;
	if (b.kernelKey !== key) {
		// Another build of the link's kernel: placed by name and line (E8), said so.
		bmGo(b);
		const pl = bmPlace(b);
		bmNote("“" + b.name + "” was made with another build of " + recFamily(b) + ": " +
			(pl.gone ? "it is not in this one (" + pl.gone + ")." : "placed " + (pl.how || "at the same place") + " (" + bmTargetText({ ...b, t: pl.t }) + ")."));
		return;
	}
	bmGo(b);
}

// ---- links --------------------------------------------------------------------

const bmB64 = (s) => btoa(String.fromCharCode(...new TextEncoder().encode(s))).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
const bmUnB64 = (s) => new TextDecoder().decode(Uint8Array.from(atob(s.replace(/-/g, "+").replace(/_/g, "/")), (c) => c.charCodeAt(0)));

function bmLink(b) {
	const data = { n: b.name, k: b.kind, t: b.t, kn: b.kernel, kk: b.kernelKey, bd: b.build };
	return location.origin + location.pathname + "?kernel=" + encodeURIComponent(b.kernel.split(" + ")[0]) + "#bm=" + bmB64(JSON.stringify(data));
}

// A link's #bm=...: add the bookmark (once) and go there when its kernel
// runs. The hash becomes the Machine tab's. True when the hash was one.
function bmFromHash() {
	if (!location.hash.startsWith("#bm="))
		return false;
	let b = null;
	try {
		const d = JSON.parse(bmUnB64(location.hash.slice(4)));
		b = { id: "b" + Date.now().toString(36) + Math.floor(Math.random() * 36 ** 4).toString(36), name: String(d.n), kind: d.k, t: d.t,
			kernel: String(d.kn || ""), kernelKey: d.kk || null, ...(d.bd ? { build: String(d.bd) } : {}),
			created: Date.now(), from: "link" };
	} catch (e) {
		b = null;
	}
	history.replaceState(history.state, "", "#machine");
	if (!bmValid(b)) {
		bmNote("The link's bookmark could not be read.");
		return true;
	}
	const same = BM.list.find((x) => x.kind === b.kind && JSON.stringify(x.t) === JSON.stringify(b.t) && x.kernelKey === b.kernelKey);
	if (same)
		b = same;
	else {
		BM.list.push(b);
		bmSave();
	}
	BM.pending = { b, until: realNow() + 300000 };
	bmNote("Opened “" + b.name + "” from a link" + (same ? "" : "; it is in your bookmarks now") + ".");
	return true;
}

// ---- export, import -------------------------------------------------------------

function bmExport() {
	download("mkb-bookmarks.json", JSON.stringify({ format: "mkb-bookmarks", version: 1, exported: new Date().toISOString(), bookmarks: BM.list }, null, 1),
		"application/json");
	bmNote("Exported " + BM.list.length + " bookmarks to mkb-bookmarks.json.");
}

async function bmImport(file) {
	let j = null;
	try {
		j = JSON.parse(await file.text());
	} catch (e) {
		bmNote(file.name + " is not a bookmarks file: " + ((e && e.message) || e) + ".");
		return;
	}
	const list = j && Array.isArray(j.bookmarks) ? j.bookmarks : Array.isArray(j) ? j : null;
	if (!list) {
		bmNote(file.name + " has no bookmarks (an export of this page has a \"bookmarks\" list).");
		return;
	}
	let added = 0, had = 0, bad = 0;
	for (const b of list) {
		if (!bmValid(b)) {
			bad++;
			continue;
		}
		if (BM.list.some((x) => x.id === b.id || (x.kind === b.kind && JSON.stringify(x.t) === JSON.stringify(b.t) && x.kernelKey === b.kernelKey && x.name === b.name))) {
			had++;
			continue;
		}
		BM.list.push({ ...b, id: b.id || "b" + Date.now().toString(36) + added });
		added++;
	}
	bmSave();
	bmNote("Imported " + added + " bookmarks from " + file.name + (had ? ", " + had + " were here already" : "") + (bad ? ", " + bad + " unreadable" : "") + ".");
}

// ---- the list (in the menu toggle's menu) ---------------------------------------

function bmNote(text) {
	const el = $("bm_status");
	if (!el)
		return;
	el.textContent = text;
	BM.undo = null;
}

function bmDelete(b) {
	const i = BM.list.indexOf(b);
	if (i < 0)
		return;
	BM.list.splice(i, 1);
	bmSave();
	bmNote("Deleted “" + b.name + "”. ");
	BM.undo = { b, i };
	const u = document.createElement("button");
	u.type = "button";
	u.textContent = "Undo";
	u.addEventListener("click", () => {
		BM.list.splice(Math.min(BM.undo.i, BM.list.length), 0, BM.undo.b);
		bmNote("“" + BM.undo.b.name + "” is back.");
		bmSave();
	});
	$("bm_status").append(u);
}

function bmEditName(b, row) {
	const go = row.querySelector(".bmgo");
	const inp = document.createElement("input");
	inp.className = "bmedit";
	inp.value = b.name;
	inp.setAttribute("aria-label", "New name for “" + b.name + "”");
	let done = false;
	const finish = (save) => {
		if (done)
			return;
		done = true;
		const v = inp.value.trim();
		if (save && v && v !== b.name) {
			b.name = v;
			bmSave();
		} else
			bmRender();
		const again = $("bm_list").querySelector('.bmrow[data-id="' + b.id + '"] .bmgo');
		if (again)
			again.focus();
	};
	inp.addEventListener("keydown", (ev) => {
		if (ev.key === "Enter" || ev.key === "Escape") {
			ev.preventDefault();
			ev.stopPropagation();
			finish(ev.key === "Enter");
		}
	});
	inp.addEventListener("blur", () => finish(true));
	go.replaceWith(inp);
	inp.focus();
	inp.select();
}

function bmRowEl(b, group) {
	const li = document.createElement("li");
	li.className = "bmrow";
	Object.assign(li.dataset, { id: b.id, kind: b.kind, group });
	const go = document.createElement("button");
	go.type = "button";
	go.className = "bmgo";
	go.textContent = b.name;
	// Another build's: where it is here, and on which build it was made (E8).
	const pl = group === "build" ? bmPlace(b) : null;
	const here = pl && !pl.gone ? bmTargetText({ ...b, t: pl.t }) : bmTargetText(b);
	go.title = group === "other" ? "Switch to " + b.kernel + " and go to " + bmTargetText(b) : "Go to " + here;
	go.addEventListener("click", () => bmGo(b));
	const what = document.createElement("span");
	what.className = "bmwhat";
	what.textContent = BM_KINDS[b.kind] + " · " + here + (group === "other" ? " · " + b.kernel : "") +
		(pl ? " · made on " + (b.build || "another build of " + recFamily(b)) + (pl.gone ? ", not in this build: " + pl.gone : pl.how ? ", placed " + pl.how : "") : "") +
		(b.from === "link" ? " · from a link" : "");
	if (pl)
		li.dataset.placed = pl.gone ? "gone" : bmTargetText({ ...b, t: pl.t });
	const acts = document.createElement("span");
	acts.className = "bmacts";
	if (group === "other") {
		const sw = document.createElement("button");
		sw.type = "button";
		sw.className = "bmswitch";
		sw.textContent = "Switch to " + b.kernel;
		sw.title = "Load " + b.kernel + " (the Machine tab's kernel) and go to this bookmark";
		sw.addEventListener("click", () => bmSwitch(b));
		acts.append(sw);
	}
	if (b.kind === "pos" && b.t.snap && group === "this") {
		const s = snapshots.find((x) => x.id === b.t.snap);
		if (s) {
			const r = document.createElement("button");
			r.type = "button";
			r.className = "bmrestore";
			r.textContent = "Restore";
			r.title = "Restore the snapshot “" + s.name + "” (Snapshots tab): the machine goes back to this position";
			r.addEventListener("click", () => restoreSnapshot(s));
			acts.append(r);
		}
	}
	if (b.kind === "pos" && b.t.run) {
		const p = document.createElement("button");
		p.type = "button";
		p.className = "bmreplay";
		p.textContent = "Replay";
		p.title = "Boot " + b.t.run.kernel + " again and replay the run that got here (the same keys at the same instruction counts), to " +
			b.t.run.end.at.toLocaleString("en") + " instructions";
		p.addEventListener("click", () => {
			api.show.runBadgeOpen(false);
			replayRun(b.t.run).catch((e) => runStatus("Replay failed: " + e, "error"));
		});
		acts.append(p);
	}
	const link = document.createElement("a");
	link.className = "bmlink";
	link.href = bmLink(b);
	link.textContent = "🔗";
	link.title = "A link that opens this place (copy it to share; it adds the bookmark for whoever opens it)";
	link.setAttribute("aria-label", "Link to “" + b.name + "”");
	link.addEventListener("click", (ev) => {
		// The link itself is for copying; a click goes there in this page.
		ev.preventDefault();
		if (navigator.clipboard)
			navigator.clipboard.writeText(link.href).then(() => bmNote("Copied the link to “" + b.name + "”."), () => bmNote("Copy the link from its context menu."));
	});
	const ren = document.createElement("button");
	ren.type = "button";
	ren.className = "bmren";
	ren.textContent = "✎";
	ren.title = "Rename (Enter saves, Escape cancels)";
	ren.setAttribute("aria-label", "Rename “" + b.name + "”");
	ren.addEventListener("click", () => bmEditName(b, li));
	const del = document.createElement("button");
	del.type = "button";
	del.className = "bmdel";
	del.textContent = "✕";
	del.title = "Delete";
	del.setAttribute("aria-label", "Delete “" + b.name + "”");
	del.addEventListener("click", () => bmDelete(b));
	acts.append(link, ren, del);
	li.append(go, what, acts);
	return li;
}

function bmRender() {
	const ul = $("bm_list");
	if (!ul)
		return;
	const q = BM.q.toLowerCase();
	ul.textContent = "";
	const groups = { this: [], build: [], other: [] };
	for (const b of BM.list)
		groups[bmGroup(b)].push(b);
	let shown = 0;
	for (const g of ["this", "build", "other"]) {
		if (!groups[g].length)
			continue;
		const h = document.createElement("li");
		h.className = "bmghead";
		h.textContent = g === "this" ? "This kernel (" + (snapKernelName() || "none") + ")" :
			g === "build" ? "Other builds of " + buildFamily() + " (placed in this one by function and line)" : "For another kernel";
		ul.append(h);
		let n = 0;
		for (const b of groups[g]) {
			const li = bmRowEl(b, g);
			li.hidden = !!q && !b.name.toLowerCase().includes(q);
			if (!li.hidden)
				n++;
			ul.append(li);
		}
		h.hidden = !n;
		shown += n;
	}
	if (!shown) {
		const e = document.createElement("li");
		e.className = "bmempty";
		e.textContent = BM.list.length ? "No bookmark matches “" + BM.q + "”." :
			"No bookmarks yet. Add one with a ☆ button, a right click (a long press on a touch screen) on a line, page, box or frame, or the B key.";
		ul.append(e);
	}
	ul.dataset.count = BM.list.length;
	const mine = groups.this.length + groups.build.length;
	$("bm_count").textContent = mine ? "(" + mine + (groups.other.length ? " + " + groups.other.length : "") + ")" : groups.other.length ? "(0 + " + groups.other.length + ")" : "";
	$("bm_store").textContent = BM.stored ? "" : "Not kept after a reload: " + BM.why + ".";
	$("bm_store").hidden = BM.stored;
}

// After any change: the list, the marks in every view.
function bmChanged() {
	BM.version = (BM.version || 0) + 1;
	BM.marks = null;
	bmRender();
	bmRenderPages();
	if (tab === "page" && selectedPage >= 0)
		api.show.inspect();
	if (tab === "debugger")
		api.show.renderContext();
	if (srcPanel && !$("src_panel").hidden)
		renderSourcePanel();
	if (tab === "source")
		api.source.covRender();
	G.dirty = true;
	if (tab === "calls")
		graphTick();
}

// The menu grows upward from the toggle: it may take the room above it.
function bmMenuFit() {
	const m = $("runbadge_menu");
	if (m.hidden)
		return;
	const top = $("runbadge_toggle").getBoundingClientRect().top;
	m.style.maxHeight = Math.max(160, top - 14) + "px";
}

function bmListOpen(open) {
	$("bm_box").hidden = !open;
	$("bm_open").setAttribute("aria-expanded", open ? "true" : "false");
	if (open)
		bmRender();
	bmMenuFit();
}

provide({ bmLineStar, bmFnNames, bmTip });

export { BM_KEY, BM_LONG_MS, BM, bmLoad, bmDraftFn, bmDraftLine, bmDraftPos, bmDraftAt, bmPressTarget, bmDraftHere, bmPlace, bmGroup,
	bmMarks, bmTip, bmStarEl, bmCodeStar, bmFnStar, bmFnNames, bmAddButton, bmRenderPages, bmAsk, bmDialogClose,
	bmDialogSave, bmPendingTick, bmFromHash, bmExport, bmImport, bmRender, bmChanged, bmMenuFit, bmListOpen };
