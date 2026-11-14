"use strict";
// Bookmarks (K1): named places the user adds from many views and jumps
// back to. Uses app.js's globals (stepper, symbols, srcPanel, tab, ...) and
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
	const k = typeof snapKernelKey === "function" ? snapKernelKey() : null;
	if (!k && image && image.snapKeyP && BM.keyWait !== image.snapKeyP) {
		BM.keyWait = image.snapKeyP;
		image.snapKeyP.then(() => bmChanged());
	}
	return k;
}

// "this": a bookmark of the loaded kernel; "other": of another one. While
// the identity is computed, the kernel's name decides.
function bmGroup(b) {
	const k = bmKey();
	if (k)
		return b.kernelKey === k ? "this" : "other";
	return image && b.kernel === snapKernelName() ? "this" : "other";
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
	return { kind: "code", t: { pa, sym: s }, name: s || hex(pa) };
}

function bmDraftPage(p) {
	const label = typeof pageLabel === "function" && pageClass.length ? pageLabel(p) : "";
	return { kind: "page", t: { page: p }, name: "page " + hex(p) + (label ? " " + label : "") };
}

function bmDraftFn(pa, name) {
	return { kind: "fn", t: { pa, name }, name };
}

function bmDraftLine(path, line) {
	return line ? { kind: "line", t: { path, line }, name: path.replace(/^.*\//, "") + ":" + line } : { kind: "file", t: { path }, name: path };
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
	const snap = typeof snapshots !== "undefined" ? snapshots.find((s) => s.meta && s.meta.icount === icount && key && s.kernelKey === key) : null;
	// The run up to here (J4): Replay boots again and replays it to here.
	const run = typeof runNow === "function" ? runNow() : null;
	const steps = stepper.steps;
	return { kind: "pos", t: { pa, eip, steps, icount, sym, snap: snap ? snap.id : null, snapName: snap ? snap.name : "", ...(run ? { run } : {}) },
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
		const p = pageAt({ clientX: x, clientY: y });
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
		const at = jumpHit >= 0 && Math.floor(jumpHit / PAGE) === selectedPage ? jumpHit : navListingAnchor();
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
	if (BM.marks && BM.marks.key === bmKey() + "|" + BM.list.length + "|" + BM.version)
		return BM.marks;
	const m = { key: bmKey() + "|" + BM.list.length + "|" + BM.version, code: new Map(), fn: new Map(), page: new Map(), line: new Map(), file: new Map() };
	const add = (map, k, b) => map.set(k, (map.get(k) || []).concat(b.name));
	for (const b of BM.list) {
		if (bmGroup(b) !== "this")
			continue;
		const t = b.t;
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
		memCellStyle(s, p, rows);
		box.append(s);
	}
}

// ---- the add dialog -------------------------------------------------------------

function bmAsk(draft) {
	if (!draft) {
		bmNote("Nothing to bookmark here: pick a line, a page, a box or a frame first.");
		return;
	}
	if (typeof hideTip === "function")
		hideTip();
	runBadgeOpen(false);
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
		kernel: snapKernelName(), kernelKey: bmKey(), created: Date.now() };
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
	if (bmGroup(b) !== "this") {
		bmSwitch(b);
		return;
	}
	runBadgeOpen(false);
	const t = b.t;
	switch (b.kind) {
	case "code":
	case "pos":
		navJump(() => {
			jumpHit = t.pa;
			jumpHitScroll = true;
			jumpTo(t.pa);
		});
		break;
	case "page":
		selectPage(t.page);
		break;
	case "line":
		openSourceLine(t.path, t.line);
		break;
	case "file":
		openSourceFile(t.path);
		break;
	case "fn":
		openFunction(t.pa, t.name, true);
		break;
	}
	bmNote("At “" + b.name + "” (" + bmTargetText(b) + ").");
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
	loadKernel(k).catch((e) => setStatus(String(e)));
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
	if (!key || (p.b.kernelKey !== key && p.b.kernel !== snapKernelName()))
		return;
	BM.pending = null;
	const b = BM.list.find((x) => x.id === p.b.id) || p.b;
	if (b.kernelKey !== key) {
		// The link's kernel by name, another build of it: go there anyway, say so.
		bmGoThere(b);
		bmNote("“" + b.name + "” was made with another build of " + b.kernel + ": the place may differ.");
		return;
	}
	bmGo(b);
}

function bmGoThere(b) {
	const was = b.kernelKey;
	b.kernelKey = bmKey();
	try {
		bmGo(b);
	} finally {
		b.kernelKey = was;
	}
}

// ---- links --------------------------------------------------------------------

const bmB64 = (s) => btoa(String.fromCharCode(...new TextEncoder().encode(s))).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
const bmUnB64 = (s) => new TextDecoder().decode(Uint8Array.from(atob(s.replace(/-/g, "+").replace(/_/g, "/")), (c) => c.charCodeAt(0)));

function bmLink(b) {
	const data = { n: b.name, k: b.kind, t: b.t, kn: b.kernel, kk: b.kernelKey };
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
			kernel: String(d.kn || ""), kernelKey: d.kk || null, created: Date.now(), from: "link" };
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
		if (!BM.undo)
			return;
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
	go.title = group === "this" ? "Go to " + bmTargetText(b) : "Switch to " + b.kernel + " and go to " + bmTargetText(b);
	go.addEventListener("click", () => bmGo(b));
	const what = document.createElement("span");
	what.className = "bmwhat";
	what.textContent = BM_KINDS[b.kind] + " · " + bmTargetText(b) + (group === "other" ? " · " + b.kernel : "") + (b.from === "link" ? " · from a link" : "");
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
			runBadgeOpen(false);
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
	const groups = { this: [], other: [] };
	for (const b of BM.list)
		groups[bmGroup(b)].push(b);
	let shown = 0;
	for (const g of ["this", "other"]) {
		if (!groups[g].length)
			continue;
		const h = document.createElement("li");
		h.className = "bmghead";
		h.textContent = g === "this" ? "This kernel (" + (snapKernelName() || "none") + ")" : "For another kernel";
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
	const mine = groups.this.length;
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
	if (typeof inspect === "function" && tab === "page" && selectedPage >= 0)
		inspect();
	if (tab === "debugger")
		renderContext();
	if (srcPanel && !$("src_panel").hidden)
		renderSourcePanel();
	if (tab === "source")
		covRender();
	if (typeof G !== "undefined") {
		G.dirty = true;
		if (tab === "calls")
			graphTick();
	}
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

// ---- init -------------------------------------------------------------------------

function initBookmarks() {
	bmLoad();
	// Every add action: a ☆ button, a right click or long press, the B key.
	$("bm_add_eip").addEventListener("click", () => bmAsk(bmDraftHere("eip")));
	$("bm_add_page").addEventListener("click", () => bmAsk(bmDraftHere("page")));
	$("bm_add_mem").addEventListener("click", () => bmAsk(bmDraftHere("mem")));
	$("bm_add_src").addEventListener("click", () => bmAsk(bmDraftHere("src")));
	$("bm_add_calls").addEventListener("click", () => bmAsk(bmDraftHere("calls")));
	$("bm_here").addEventListener("click", () => bmAsk(bmDraftPos()));
	$("bm_open").addEventListener("click", () => bmListOpen($("bm_box").hidden));
	$("bm_filter").addEventListener("input", () => {
		BM.q = $("bm_filter").value.trim();
		bmRender();
	});
	$("bm_export").addEventListener("click", bmExport);
	$("bm_import").addEventListener("click", () => $("bm_import_file").click());
	$("bm_import_file").addEventListener("change", () => {
		const f = $("bm_import_file").files[0];
		if (f)
			bmImport(f).finally(() => { $("bm_import_file").value = ""; });
	});
	$("bm_save").addEventListener("click", bmDialogSave);
	$("bm_cancel").addEventListener("click", bmDialogClose);
	// Before the menu's and the popovers' own key handlers (capture, added
	// first): Enter and Escape in the dialog and in a name being edited;
	// v86 takes every key on the window, so the list's buttons get their
	// Enter and Space here.
	document.addEventListener("keydown", (ev) => {
		const t = ev.target;
		if (!$("bm_dialog").hidden && (ev.key === "Escape" || (ev.key === "Enter" && t === $("bm_name")))) {
			ev.preventDefault();
			ev.stopImmediatePropagation();
			if (ev.key === "Enter")
				bmDialogSave();
			else
				bmDialogClose();
			return;
		}
		if (t && t.classList && t.classList.contains("bmedit") && (ev.key === "Enter" || ev.key === "Escape"))
			return; // its own handler
		const btn = t && t.closest ? t.closest("#bm_box button, #bm_dialog button, #bm_open") : null;
		if (btn && (ev.key === "Enter" || ev.key === " ")) {
			ev.preventDefault();
			ev.stopImmediatePropagation();
			btn.click();
			return;
		}
		// B: a bookmark for what is under the pointer, else what has the
		// focus, else the tab's place (as its ☆).
		if ((ev.key === "b" || ev.key === "B") && !ev.altKey && !ev.ctrlKey && !ev.metaKey && !ev.shiftKey && !vmHasKeys &&
			!(t && t.matches && t.matches("input, textarea, select")) && $("bm_dialog").hidden) {
			ev.preventDefault();
			ev.stopImmediatePropagation();
			const h = BM.hover;
			const d = (h && h.el.isConnected && bmDraftAt(h.el, h.x, h.y)) || bmDraftAt(document.activeElement, 0, 0) ||
				(srcPanel && !$("src_panel").hidden && $("src_panel").contains(document.activeElement) ? bmDraftHere("src") : bmDraftHere("tab"));
			bmAsk(d);
		}
	}, true);
	document.addEventListener("pointermove", (ev) => {
		if (ev.pointerType === "mouse")
			BM.hover = { el: ev.target, x: ev.clientX, y: ev.clientY };
	}, { passive: true });
	// A right click on something that can be bookmarked opens the dialog
	// (with Shift, the browser's own menu); so does a long press.
	document.addEventListener("contextmenu", (ev) => {
		if (realNow() - BM.longAt < 1500) {
			ev.preventDefault();
			return;
		}
		if (ev.shiftKey || !$("bm_dialog").hidden)
			return;
		const d = bmDraftAt(bmPressTarget(ev), ev.clientX, ev.clientY);
		if (d) {
			ev.preventDefault();
			bmAsk(d);
		}
	});
	let press = null;
	const unpress = () => {
		if (press)
			clearTimeout(press.timer);
		press = null;
	};
	document.addEventListener("pointerdown", (ev) => {
		unpress();
		// A new press: the events below that drop the end of the long press
		// (its mousedown and click) leave this one alone.
		BM.longAt = -Infinity;
		if (ev.pointerType === "mouse" || !$("bm_dialog").hidden)
			return;
		const x = ev.clientX, y = ev.clientY, el = bmPressTarget(ev);
		if (!bmDraftAt(el, x, y))
			return;
		press = { x, y, timer: setTimeout(() => {
			press = null;
			const d = bmDraftAt(el, x, y);
			if (d && $("bm_dialog").hidden) {
				BM.longAt = realNow();
				bmAsk(d);
			}
		}, BM_LONG_MS) };
	}, true);
	document.addEventListener("pointermove", (ev) => {
		if (press && Math.hypot(ev.clientX - press.x, ev.clientY - press.y) > 10)
			unpress();
	}, true);
	document.addEventListener("pointerup", unpress, true);
	document.addEventListener("pointercancel", unpress, true);
	// The press that opened the dialog: its release must not move the focus
	// out of the name field (mousedown) nor click (a page square would open
	// the Page tab).
	document.addEventListener("mousedown", (ev) => {
		if (realNow() - BM.longAt < 1500 && !(ev.target.closest && ev.target.closest("#bm_dialog")))
			ev.preventDefault();
	}, true);
	document.addEventListener("pointerup", () => {
		if (realNow() - BM.longAt < 1500 && !$("bm_dialog").hidden)
			setTimeout(() => $("bm_name").focus(), 0);
	}, true);
	// The click a long press may end with does nothing (a page square would open the Page tab).
	document.addEventListener("click", (ev) => {
		if (realNow() - BM.longAt < 1000 && !(ev.target.closest && ev.target.closest("#bm_dialog"))) {
			ev.preventDefault();
			ev.stopImmediatePropagation();
		}
	}, true);
	window.addEventListener("storage", (ev) => {
		if (ev.key === BM_KEY) {
			bmLoad();
			bmChanged();
		}
	});
	window.addEventListener("resize", bmMenuFit);
	setInterval(bmPendingTick, 300);
	bmRender();
}
