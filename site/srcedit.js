"use strict";

// Editing the kernel's source in the page (E2). The Source panel's Edit
// turns the file shown into a text editor: a plain <textarea> with a
// column of line numbers beside it (no editor library: CodeMirror 6 with C
// highlighting measured 519 KB minified, 167 KB gzip, about 60% more than
// all of the page's own scripts; the textarea gives undo, selection, IME and
// phone keyboards from the browser for nothing). Edits live in an overlay
// over the tree the kernel was built from: per kernel and path the text the
// edit started from (base) and the edited text, in IndexedDB, else in
// memory until a reload. A file whose text is the base again is no longer
// an edit. The Source tab lists the changed files with a diff and a revert
// each, downloads them as one patch for `git apply` (a/ and b/ paths
// relative to the kernel's tree, as the DWARF names them) and applies an
// uploaded patch: every file of it or none. The diff and the patch reader
// are edpatch.js's.
//
// The listings, the Debugger and the panel's own view keep showing the
// text the machine runs: the one the kernel was built from, or a hot
// patch's (hotpatch.jsx, E8); their line numbers and coverage belong to it. srcOverlay is the API for the in-browser build (E3, E4):
// paths() the changed files of the shown kernel, read(path) a file's text
// with the overlay applied, original(path) the text it was built from.

import { image, api, provide } from "./page.js";
import { dbgOnChange } from "./dbginfo.js";
import { srcFile, SRC } from "./srctext.js";
import { srcKernelKey } from "./srcswitch.js";
import { srcPanel, renderSourcePanel, openSourceFile } from "./source.js";
import { edFilePatch, edParse, edApplyHunks, edLines } from "./edpatch.js";
import { controller } from "./ui.jsx";
import { SRCV, EDBAR, EDSTATE, srcOpen } from "./srcview.js";

const ED_DB = "mkb-srcedit";
const ED_STORE = "edits";
const ED_SAVE_MS = 300;          // typing pause before the overlay is written
const ED_WAIT_MS = 60000;        // longest wait for a file's shipped text
const ED_ARM_MS = 5000;          // a revert's second press within this time

const ED = {
	all: new Map(),      // kernel -> Map(path -> { base, text }); base null: a new file, text null: deleted
	db: null,
	why: "",             // why there is no IndexedDB (the edits go with a reload)
	ready: null,
	on: false,           // the panel is in edit mode (it stays on across files until Edit again)
	cur: null,           // { kernel, path, base } of the file in the editor
	timer: 0,
	diffShown: false,    // the panel shows the file's diff instead of the text
	listDiff: null,      // path whose diff the Source tab shows
	armed: null,         // the revert waiting for its second press: "*", "panel" or "file:" + path
	armTimer: 0,
	barKey: "",          // what the Edit button and the editor bar show (edSync)
};

// What the Source tab's Your edits shows (A1, sourcepanel.jsx draws it from
// ED and the overlay): the status line (said counts the lines written).
const EDV = controller({ status: "", statusState: undefined, said: 0 });

const edKernel = () => srcKernelKey();

function edMap(kernel = edKernel()) {
	if (!ED.all.has(kernel))
		ED.all.set(kernel, new Map());
	return ED.all.get(kernel);
}

// ---- the store --------------------------------------------------------------

function edLoad() {
	return new Promise((resolve) => {
		let req;
		try {
			req = indexedDB.open(ED_DB, 1);
		} catch (e) {
			ED.why = "no IndexedDB here (" + e.message + ")";
			resolve();
			return;
		}
		req.onupgradeneeded = () => req.result.createObjectStore(ED_STORE, { keyPath: ["kernel", "path"] });
		req.onerror = () => {
			ED.why = "IndexedDB did not open (" + (req.error ? req.error.message : "error") + ")";
			resolve();
		};
		req.onsuccess = () => {
			ED.db = req.result;
			const get = ED.db.transaction(ED_STORE).objectStore(ED_STORE).getAll();
			get.onsuccess = () => {
				for (const r of get.result)
					edMap(r.kernel).set(r.path, { base: r.base, text: r.text });
				resolve();
			};
			get.onerror = () => {
				ED.why = "the stored edits did not load (" + (get.error ? get.error.message : "error") + ")";
				ED.db = null;
				resolve();
			};
		};
	});
}

// Store one file's state: text equal to base drops the edit.
function edPut(kernel, path, base, text) {
	const m = edMap(kernel);
	const drop = base === text;
	if (drop)
		m.delete(path);
	else
		m.set(path, { base, text });
	if (!ED.db)
		return Promise.resolve();
	return new Promise((resolve, reject) => {
		const tx = ED.db.transaction(ED_STORE, "readwrite");
		const st = tx.objectStore(ED_STORE);
		if (drop)
			st.delete([kernel, path]);
		else
			st.put({ kernel, path, base, text });
		tx.oncomplete = () => resolve();
		tx.onerror = tx.onabort = () => reject(tx.error || new Error("IndexedDB write failed"));
	});
}

// ---- the text the kernel was built from ---------------------------------

// The text of path from source.js (shipped or picked tree), or null when
// there is none; waits while it loads.
function edShipped(path) {
	return new Promise((resolve, reject) => {
		const t0 = Date.now();
		const poll = () => {
			const f = srcFile(path, true);
			if (f.state === "ok")
				resolve(f.lines.join("\n"));
			else if (f.state === "none")
				resolve(null);
			else if (Date.now() - t0 > ED_WAIT_MS)
				reject(new Error(path + ": its text did not load"));
			else
				setTimeout(poll, 50);
		};
		poll();
	});
}

async function edRead(path) {
	await ED.ready;
	const e = edMap().get(path);
	return e ? e.text : edShipped(path);
}

async function edOriginal(path) {
	await ED.ready;
	const e = edMap().get(path);
	return e ? e.base : edShipped(path);
}

function edPatch(kernel = edKernel()) {
	const m = edMap(kernel);
	return [...m.keys()].sort().map((p) => edFilePatch(p, m.get(p).base, m.get(p).text).patch).join("");
}

// Apply a patch to the shown kernel's overlay: every file or none.
// Returns [{ path, base, text }] as stored.
async function edApply(text) {
	await ED.ready;
	const kernel = edKernel();
	const files = edParse(text);
	const done = [];
	for (const f of files) {
		if (!f.path)
			throw new Error("a file without a name");
		const e = edMap(kernel).get(f.path);
		const cur = e ? e.text : await edShipped(f.path);
		const base = e ? e.base : cur;
		if (f.from === null) {
			if (cur !== null)
				throw new Error(f.path + ": the patch makes it, but it is in the tree");
			done.push({ path: f.path, base, text: edApplyHunks(f.path, "", f.hunks) });
		} else {
			if (cur === null)
				throw new Error(f.path + ": not in this kernel's source" + (SRC.missing && SRC.missing[f.path] ? " (" + SRC.missing[f.path] + ")" : ""));
			const after = edApplyHunks(f.path, cur, f.hunks);
			if (f.to === null && after !== "")
				throw new Error(f.path + ": the patch deletes it, but its lines are not the whole file");
			done.push({ path: f.path, base, text: f.to === null ? null : after });
		}
	}
	for (const d of done)
		await edPut(kernel, d.path, d.base, d.text);
	edChanged();
	return done;
}

// ---- the editor in the Source panel ----------------------------------------

// The editor's textarea (sourceaside.jsx renders it; this module writes
// its text and listens to it, edWire).
const edTa = () => SRCV.taEl;

// After srcShown (another file in the panel) and closeSource (srcPanel null).
function edPanelShown() {
	if (ED.cur && (!srcPanel || srcPanel.path !== ED.cur.path))
		edLeaveFile();
	if (!srcPanel)
		ED.on = ED.diffShown = false;
	edSync();
	if (ED.on && srcPanel && !ED.cur)
		edEnterFile();
}

// What the panel can do with its file: { ok, why }.
function edCan() {
	if (!srcPanel)
		return { ok: false, why: "" };
	if (edMap().has(srcPanel.path))
		return { ok: true };
	const f = srcFile(srcPanel.path);
	return f.state === "ok" ? { ok: true } : f.state === "loading" ? { ok: false, why: "reading " + srcPanel.path + "…" } :
		{ ok: false, why: "No text to edit: " + f.why };
}

// The Edit button, the bar under the title and which view the panel shows
// (sourceaside.jsx draws them from ED and edCan); the bar's line.
function edSync() {
	const e = srcPanel ? edMap().get(srcPanel.path) : null;
	if (!ED.on && e) {
		const s = edStats(srcPanel.path);
		// A kernel rebuilt in the page (rebuild.js) was built from the edits.
		const built = image && image.edits && image.edits[srcPanel.path] === e.text;
		// Hot patched with this text (E8): the panel shows it (srctext.js SRC_OVER).
		const over = srcFile(srcPanel.path);
		const patched = over.over && over.lines.join("\n") === e.text;
		edStateSay(built ? "This kernel was rebuilt here from your text of this file (+" + s.add + " -" + s.del + "), shown here." :
			patched ? "The machine runs your text of this file (+" + s.add + " -" + s.del + "), hot patched in: shown here." :
				"You changed this file (+" + s.add + " -" + s.del + "): the panel shows the text the " + (over.over ? "machine runs" : "kernel was built from") + "; Edit shows yours.");
	} else if (ED.on && ED.cur)
		edState();
	// Drawn again only when what they show differs (a file opened, a save).
	const can = edCan();
	const key = [ED.on, !!ED.cur, ED.diffShown, ED.armed, !!e, can.ok, can.why].join("\0");
	if (key !== ED.barKey) {
		ED.barKey = key;
		EDBAR.changed();
	}
	if (ED.diffShown && srcPanel)
		edShowPanelDiff();
}

function edStateSay(text) {
	if (EDSTATE.text === text)
		return;
	EDSTATE.text = text;
	EDSTATE.changed();
}

async function edEnterFile() {
	const path = srcPanel.path, kernel = edKernel();
	const e = edMap(kernel).get(path);
	const base = e ? e.base : await edShipped(path).catch(() => null);
	if (!srcPanel || srcPanel.path !== path || !ED.on || ED.cur)
		return;
	if (!e && base === null) {
		ED.on = false;
		edSync();
		return;
	}
	ED.cur = { kernel, path, base };
	const ta = edTa();
	ta.value = e ? e.text || "" : base;
	ta.setAttribute("aria-label", "Text of " + path);
	edGutter(true);
	edSync();
	// At the line the panel was opened at or picked.
	const line = srcPanel.pick || srcPanel.line || 1;
	const lines = ta.value.split("\n");
	const pos = lines.slice(0, Math.max(0, line - 1)).reduce((n, l) => n + l.length + 1, 0);
	ta.focus({ preventScroll: true });
	ta.setSelectionRange(Math.min(pos, ta.value.length), Math.min(pos, ta.value.length));
	const lh = parseFloat(getComputedStyle(ta).lineHeight) || 15;
	ta.scrollTop = Math.max(0, (line - 1) * lh - ta.clientHeight / 3);
	edState();
}

// Out of the editor for this file: what was typed is stored first.
function edLeaveFile() {
	edFlush();
	ED.cur = null;
	ED.diffShown = false;
}

function edToggle() {
	if (ED.on) {
		edLeaveFile();
		ED.on = false;
		edSync();
		renderSourcePanel();
		SRCV.editEl.focus({ preventScroll: true });
		edChanged();
		return;
	}
	if (!edCan().ok)
		return;
	ED.on = true;
	edEnterFile();
}

// Line numbers: one per line of the text, redrawn when the count changes.
function edGutter(force) {
	const ta = edTa(), g = SRCV.gutterEl;
	let n = 1;
	for (let i = ta.value.indexOf("\n"); i >= 0; i = ta.value.indexOf("\n", i + 1))
		n++;
	if (force || Number(g.dataset.lines) !== n) {
		g.dataset.lines = n;
		let s = "";
		for (let i = 1; i <= n; i++)
			s += i + "\n";
		g.textContent = s;
	}
	g.scrollTop = ta.scrollTop;
}

function edCaret() {
	const ta = edTa(), at = ta.selectionStart;
	const before = ta.value.lastIndexOf("\n", at - 1);
	let line = 1;
	for (let i = ta.value.indexOf("\n"); i >= 0 && i < at; i = ta.value.indexOf("\n", i + 1))
		line++;
	return { line, col: at - before };
}

function edState(note) {
	if (!ED.cur)
		return;
	const { line, col } = edCaret();
	const e = edMap(ED.cur.kernel).get(ED.cur.path);
	const where = "line " + line + ", column " + col;
	const saved = ED.timer ? "saving…" : !e ? "the text the kernel was built from" :
		ED.db ? "saved in this browser" : "kept until a reload (" + ED.why + ")";
	edStateSay(ED.cur.path + ": " + (note || saved) + "; " + where);
}

function edInput() {
	edGutter(false);
	clearTimeout(ED.timer);
	ED.timer = setTimeout(edFlush, ED_SAVE_MS);
	edState();
}

function edFlush() {
	if (!ED.timer || !ED.cur)
		return;
	clearTimeout(ED.timer);
	ED.timer = 0;
	const { kernel, path, base } = ED.cur;
	edPut(kernel, path, base, edTa().value).then(() => {
		edState();
		edChanged();
	}, (e) => edState("not saved: " + e.message));
}

function edKeys(ev) {
	if (ev.key === "Tab" && !ev.shiftKey && !ev.altKey && !ev.ctrlKey && !ev.metaKey) {
		ev.preventDefault();
		ev.stopPropagation();
		const ta = edTa();
		if (!document.execCommand("insertText", false, "\t")) // keeps the browser's undo
			ta.setRangeText("\t", ta.selectionStart, ta.selectionEnd, "end");
		edInput();
	} else if (ev.key === "Escape") {
		ev.preventDefault();
		ev.stopPropagation();
		SRCV.editEl.focus();
	} else if ((ev.ctrlKey || ev.metaKey) && (ev.key === "s" || ev.key === "S")) {
		ev.preventDefault();
		edFlush();
	}
}

// ---- diff views, revert -----------------------------------------------------

function edStats(path) {
	const e = edMap().get(path);
	if (!e)
		return { add: 0, del: 0 };
	if (e.statsOf !== e.text) {
		const r = edFilePatch(path, e.base, e.text);
		e.stats = { add: r.add, del: r.del };
		e.statsOf = e.text;
	}
	return e.stats;
}

// A patch's line's colour (its class).
const edLineClass = (l) => l.startsWith("@@") ? "edh" : /^(diff |--- |\+\+\+ |new file|deleted file)/.test(l) ? "edf" :
	l[0] === "+" ? "eda" : l[0] === "-" ? "edd" : "";

// A patch as coloured lines in pre.
function edDiffInto(pre, patch) {
	pre.textContent = "";
	for (const l of edLines(patch)) {
		const s = document.createElement("span");
		s.className = edLineClass(l);
		s.textContent = l;
		pre.append(s);
	}
}

function edShowPanelDiff() {
	const e = srcPanel && edMap().get(srcPanel.path);
	if (ED.cur)
		edFlush();
	const text = ED.cur ? edTa().value : e && e.text;
	const base = ED.cur ? ED.cur.base : e && e.base;
	const p = srcPanel ? edFilePatch(srcPanel.path, base, text).patch : "";
	edDiffInto(SRCV.pdiffEl, p || "No change: the text is the one the kernel was built from.\n");
}

function edDiffToggle() {
	ED.diffShown = !ED.diffShown;
	if (ED.diffShown && !ED.on) {
		ED.on = true;
		edEnterFile();
	}
	edSync();
	if (!ED.diffShown && ED.cur)
		edTa().focus({ preventScroll: true });
}

// A revert acts on its second press within ED_ARM_MS. Which one is armed is
// kept here, not on the button: the list is drawn again when the DWARF or
// the source arrives, which replaces its buttons.
function edArmed(key, fn) {
	if (ED.armed !== key) {
		clearTimeout(ED.armTimer);
		ED.armed = key;
		ED.armTimer = setTimeout(() => {
			ED.armed = null;
			edArmLabels();
		}, ED_ARM_MS);
		edArmLabels();
		return;
	}
	clearTimeout(ED.armTimer);
	ED.armed = null;
	edArmLabels();
	fn();
}

function edArmLabels() {
	ED.barKey = "";
	EDBAR.changed();
	EDV.changed();
}

// The overlay in memory and the views change at once; the store after.
function edRevert(paths) {
	const kernel = edKernel(), writes = [];
	for (const p of paths) {
		const e = edMap(kernel).get(p);
		if (e)
			writes.push(edPut(kernel, p, e.base, e.base));
		if (ED.cur && ED.cur.path === p && ED.cur.kernel === kernel) {
			clearTimeout(ED.timer);
			ED.timer = 0;
			if (e && e.base === null) { // a file the patch made: gone
				ED.cur = null;
				ED.on = ED.diffShown = false;
			} else {
				edTa().value = ED.cur.base;
				edGutter(true);
			}
		}
	}
	if (ED.listDiff && paths.includes(ED.listDiff))
		ED.listDiff = null;
	edStatus("Reverted " + paths.join(", ") + ".", "reverted");
	edChanged();
	if (srcPanel && srcOpen())
		renderSourcePanel();
	return Promise.all(writes).catch((e) => edStatus("Reverted here, but not in the store: " + e.message, "reverted"));
}

// ---- the Source tab: the changed files ----------------------------------

function edStatus(text, state) {
	EDV.status = text;
	EDV.statusState = state;
	EDV.said++;
	EDV.changed();
}

function edChanged() {
	edRender();
	api.source.rbSync(); // rebuild.js: the count of edits it would build with
	api.source.hpSync(); // hotpatch.jsx: whether the edit can be patched in
	if (srcPanel && srcOpen())
		edSync();
}

// The Source tab's list: drawn from the overlay (sourcepanel.jsx).
const edRender = () => EDV.changed();

// A list row's Open: the file in the panel's editor.
function edOpen(path) {
	ED.on = true;
	openSourceFile(path);
}

function edListDiff(path) {
	ED.listDiff = ED.listDiff === path ? null : path;
	edRender();
}

function edDownload() {
	edFlush();
	const kernel = edKernel(), text = edPatch(kernel);
	if (!text)
		return;
	const a = document.createElement("a");
	a.href = URL.createObjectURL(new Blob([text], { type: "text/x-diff" }));
	a.download = kernel.replace(/[^\w.-]+/g, "_") + "-edits.patch";
	a.click();
	setTimeout(() => URL.revokeObjectURL(a.href), 10000);
	const n = edMap(kernel).size;
	edStatus("Downloaded " + a.download + ": " + n + " file" + (n === 1 ? "" : "s") + "; in the kernel's tree: git apply " + a.download, "downloaded");
}

async function edUpload(ev) {
	const inp = ev.target, f = inp.files[0];
	if (!f)
		return;
	inp.value = "";
	edFlush();
	try {
		const done = await edApply(await f.text());
		if (ED.cur && done.some((d) => d.path === ED.cur.path)) {
			const d = done.find((x) => x.path === ED.cur.path);
			if (d.text === null)
				edPanelShown();
			else {
				edTa().value = d.text;
				edGutter(true);
			}
		}
		edStatus("Applied " + f.name + ": " + done.map((d) => d.path + (d.base === null ? " (new)" : d.text === null ? " (deleted)" : "")).join(", ") + ".", "applied");
		if (srcPanel && srcOpen())
			renderSourcePanel();
	} catch (e) {
		edStatus("Not applied, no file changed: " + e.message, "refused");
	}
}

// ---- the API for the build (E3, E4) ------------------------------------------

const srcOverlay = {
	ready: null,
	// The changed files of the shown kernel (deleted ones too), sorted.
	paths: () => [...edMap().keys()].sort(),
	// A file's text with the overlay applied (null: deleted or not in the tree).
	read: edRead,
	// Its text in the tree the kernel was built from (null: a new file).
	original: edOriginal,
	// Set a file's text (null deletes it); equal to the original drops the edit.
	async write(path, text) {
		const base = await edOriginal(path);
		await edPut(edKernel(), path, base, text);
		edChanged();
	},
	revert: (path) => edRevert([path]),
	// Every edit as one git patch (a/ b/ paths in the kernel's tree).
	patch: () => edPatch(),
	apply: edApply,
};

// The editor's listeners (the component's effect, once it has the
// textarea): native ones, since a script's input event does not reach
// React's root.
function edWire(ta) {
	ta.addEventListener("input", edInput);
	ta.addEventListener("keydown", edKeys);
	ta.addEventListener("scroll", () => { SRCV.gutterEl.scrollTop = ta.scrollTop; });
	ta.addEventListener("blur", edFlush);
	for (const t of ["click", "keyup", "select"])
		ta.addEventListener(t, () => edState());
}

// The panel's Revert (press twice).
const edRevertPanel = () => edArmed("panel", () => srcPanel && edRevert([srcPanel.path]));

function initSrcEdit() {
	ED.ready = srcOverlay.ready = edLoad().then(() => edChanged());
	window.addEventListener("pagehide", edFlush);
	dbgOnChange(() => {
		edRender();
		if (srcPanel && srcOpen()) {
			edSync();
			if (ED.on && !ED.cur)
				edEnterFile();
		}
	});
}

provide({ edPanelShown });

export { ED, EDV, edMap, edCan, edToggle, edDiffToggle, edRevertPanel, edWire, edLineClass, edStats, edArmed, edRevert, edOpen, edListDiff, edDownload, edUpload, edState, edFlush, srcOverlay,
	initSrcEdit };
