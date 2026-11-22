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
//
// bmdata.js has the list, its storage, the identity, the placing and the
// drafts; this file shows them: the marks, the dialog, jumps, links, the menu's list.

import { realNow } from "./clock.js";
import { $, escHtml } from "./util.js";
import { image, stepper, emulator, runState, tab, selectedPage, grid, kernels, emuReady, emuRam, api, provide } from "./page.js";
import { setJumpHit, setJumpHitScroll } from "./jumps.js";
import { G } from "./graphdata.js";
import { download, graphTick } from "./graph.js";
import { srcPanel, openSourceLine, openSourceFile, renderSourcePanel } from "./source.js";
import { snapKernelName, snapshots } from "./snaprecord.js";
import { saveSnapshot, restoreSnapshot } from "./snapshots.js";
import { replayRun, runStatus } from "./replay.js";
import { hideTip } from "./tips.js";
import { DBG } from "./dbginfo.js";
import { buildFamily, recFamily, imageLabel } from "./buildmap.js";
import { srcOpen } from "./srcview.js";
import { setSettings, setStatus } from "./machtab.js";
import { BM_KINDS, BM, bmSave, bmValid, bmKey, bmGroup, bmPlace, bmTargetText } from "./bmdata.js";

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
	setSettings({ kernel: k.name });
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
	if (srcPanel && srcOpen())
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

BM.changed = bmChanged;
provide({ bmLineStar, bmFnNames, bmTip });

export { bmMarks, bmTip, bmStarEl, bmCodeStar, bmFnStar, bmFnNames, bmAddButton, bmRenderPages, bmAsk, bmDialogClose,
	bmDialogSave, bmPendingTick, bmFromHash, bmExport, bmImport, bmRender, bmChanged, bmMenuFit, bmListOpen };
