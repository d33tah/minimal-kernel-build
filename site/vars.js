"use strict";

// Variables from the kernel's DWARF (dbginfo.js dbgTypes, dwarf.js
// DwarfTypes data): the Debugger's Variables panel (watch expressions
// pretty-printed from guest RAM, pointers open the Page tab at their
// target) and the Page listing's hex rows (the byte under the pointer or
// the tap named as variable + member path, e.g. init_task.comm[3]).
// Values are read from guest RAM when drawn: kernel virtual addresses are
// physical + the kernel's offset (0xC0000000 for i386 Linux). Reading and
// evaluating is varseval.js's, the call frames' locals varlocals.js's,
// operand names varops.js's, the tasks vartasks.js's; here the watches,
// the panels and the Page listing's bytes.

import { hex, $ } from "./util.js";
import { stepper, image, tab, runState, api } from "./page.js";
import { dwTypeName, dwWhere } from "./dwarflocals.js";
import { dbgTypes, dbgHas, DBG, dbgLocals, dbgFrames, dbgOnChange } from "./dbginfo.js";
import { varsByName, varsReady, varsNotStarted, VARS_NOT_STARTED, varsEval, varsRow, varsDeclNotes, setVarsCells,
	varsOffset, varsScalar, varsSection, varsNote, varDeclEl } from "./varseval.js";
import { varsTaskLayout, varsCurrentTask } from "./vartasks.js";
import { localsOpen, localsCtx, localsGroup } from "./varlocals.js";
import { brtTitle } from "./varops.js";

const WATCH_KEY = "mkb-watch:";
// Shown as one-tap suggestions when the kernel has them.
const WATCH_SUGGEST = ["init_task", "jiffies_64", "linux_banner", "system_state", "current_task", "init_uts_ns"];

let watchList = [];
let watchKernel = null;
const setWatchKernel = (k) => { watchKernel = k; }; // for other modules (an imported binding is read-only)

function watchSave() {
	try {
		localStorage.setItem(WATCH_KEY + watchKernel, JSON.stringify(watchList));
	} catch (e) {
		// no storage: the list lasts until the page is closed
	}
}

// The watch list of the current kernel (by name), remembered in this browser.
function watchLoad() {
	const k = image ? image.name : null;
	if (k === watchKernel)
		return;
	watchKernel = k;
	watchList = [];
	try {
		watchList = JSON.parse(localStorage.getItem(WATCH_KEY + k) || "[]");
	} catch (e) {
		watchList = [];
	}
}

function watchAdd(expr) {
	expr = String(expr || "").trim();
	if (!expr)
		return;
	watchLoad();
	if (!watchList.includes(expr)) {
		watchList.unshift(expr);
		watchSave();
	}
	renderWatches();
}

// The Debugger's Variables section.
function renderWatches() {
	const box = $("watches");
	if (!box || tab !== "debugger")
		return;
	watchLoad();
	const st = $("watch_status");
	const r = dbgTypes();
	box.dataset.state = r.state;
	$("watch_quick").textContent = "";
	if (r.state !== "ok") {
		st.textContent = r.why;
		box.textContent = "";
		return;
	}
	const T = r.T;
	const names = varsByName(T);
	st.textContent = T.vars.length + " variables and " + (T.types.length - (T.sub ? T.sub.size : 0)) + " types from the DWARF. " +
		(varsReady() ? "Values read from guest RAM " + (runState === "jit" ? "while it runs (Refresh, or pause)" : "now") + "." :
			"Boot the kernel to read values.") + (varsNotStarted() ? " " + VARS_NOT_STARTED : "");
	box.dataset.started = varsNotStarted() ? "no" : "";
	for (const n of WATCH_SUGGEST)
		if (names.has(n) && !watchList.includes(n)) {
			const b = document.createElement("button");
			b.type = "button";
			b.textContent = "+ " + n;
			b.title = "Watch " + n;
			b.addEventListener("click", () => watchAdd(n));
			$("watch_quick").append(b);
		}
	box.textContent = "";
	watchList.forEach((expr, i) => {
		const w = document.createElement("div");
		w.className = "watch";
		w.dataset.expr = expr;
		const x = document.createElement("button");
		x.type = "button";
		x.className = "wdel";
		x.textContent = "×";
		x.title = "Stop watching " + expr;
		x.setAttribute("aria-label", "stop watching " + expr);
		x.addEventListener("click", () => {
			watchList.splice(i, 1);
			watchSave();
			renderWatches();
		});
		const e = varsEval(T, expr);
		if (e.error) {
			const s = document.createElement("span");
			s.className = "werr";
			s.textContent = expr + ": " + e.error;
			w.dataset.state = "error";
			w.append(x, s);
		} else {
			w.dataset.state = "ok";
			w.dataset.addr = e.pa;
			w.append(x, varsRow(T, expr.trim(), e.type, e.pa, e.bits, expr + "/", 0));
			for (const n of e.notes) {
				const d = document.createElement("div");
				d.className = "note";
				d.textContent = n;
				w.append(d);
			}
			w.append(...varsDeclNotes(T, e.v, e.all));
		}
		box.append(w);
	});
	if (!watchList.length)
		box.textContent = "No watches: type a variable (init_task.comm, jiffies_64, ...) or tap a byte in the Page tab's hex rows.";
}

// Names for the input's suggestions: those starting with what is typed.
function watchSuggest() {
	const r = dbgTypes();
	const dl = $("watch_names");
	dl.textContent = "";
	const q = $("watch_expr").value.trim();
	if (r.state !== "ok" || q.length < 2 || /[.\-[]/.test(q))
		return;
	let n = 0;
	for (const name of varsByName(r.T).keys())
		if (name.startsWith(q)) {
			const o = document.createElement("option");
			o.value = name;
			dl.append(o);
			if (++n >= 40)
				break;
		}
}

// The Debugger's "Parameters and locals" section for the unwound frames
// uw (unwindHere), or the reason there are none.
function renderLocals(uw) {
	const box = $("locals"), st = $("locals_status");
	setVarsCells([]);
	box.textContent = "";
	if (!uw) {
		st.textContent = runState === "jit" ? "Pause to see the parameters and locals of the call frames." : "";
		return;
	}
	if (!dbgHas()) {
		st.textContent = "No parameters or locals: " + DBG.why + ".";
		return;
	}
	const frames = uw.frames.filter((f) => !f.user);
	st.textContent = frames.length ? "From the DWARF at each frame's EIP (the call instruction for callers), read from its registers and stack" +
		(uw.state !== "ok" ? "; frames below the last unwound one have no registers, so their variables cannot be read" : "") + "." :
		"No call frames unwound here" + (uw.why ? " (" + uw.why + ")" : "") + ".";
	frames.forEach((f, n) => {
		const det = document.createElement("details");
		det.className = "lframe";
		det.dataset.n = n;
		det.dataset.pc = f.lookup;
		const name = stepper ? stepper.symName[stepper.symbolOf(api.symbols.insnStart(f.lookup))] || "" : "";
		const k = String(n);
		const sum = document.createElement("summary");
		const m = api.symbols.FRAME_METHODS[f.method] || ["?"];
		sum.append("#" + n + " " + (name || hex(f.lookup)) + " ");
		const tag = document.createElement("span");
		tag.className = "fmeth";
		tag.textContent = m[0];
		if (m[1])
			tag.title = "How this frame was found: " + m[1];
		sum.append(tag);
		det.append(sum);
		const fill = () => {
			if (det.dataset.filled)
				return;
			const r = dbgLocals(f.lookup);
			det.dataset.state = r.state;
			if (r.state !== "ok") {
				const d = document.createElement("div");
				d.className = "note";
				d.textContent = r.why;
				det.append(d);
				return; // filled again when the data arrives (dbgOnChange re-renders)
			}
			det.dataset.filled = "1";
			if (!r.frames.length) {
				const d = document.createElement("div");
				d.className = "note";
				d.textContent = "no DWARF function here (assembly, or code without debug info)";
				det.append(d);
				return;
			}
			const lines = dbgFrames(f.lookup);
			const ctx = localsCtx(f, r.frames[r.frames.length - 1].fb);
			r.frames.forEach((lf, i) => {
				const fr = lines.state === "ok" ? lines.frames[i] : null;
				det.append(localsGroup(r.T, lf, i, f, ctx, "L" + n + (name || f.lookup), fr && fr.func === lf.func && fr.file && fr.line ? fr : null));
			});
		};
		const ok = k + ":" + name;
		det.open = localsOpen.has(ok) ? localsOpen.get(ok) : n === 0;
		if (det.open)
			fill();
		det.addEventListener("toggle", () => {
			localsOpen.set(ok, det.open);
			if (det.open)
				fill();
		});
		box.append(det);
	});
}

// The Debugger's "task" row: the running task (current_task).
function renderTaskRow(box) {
	const d = document.createElement("div");
	d.id = "cur_task";
	const b = document.createElement("b");
	b.textContent = "task";
	d.append(b, " ");
	const r = dbgHas() ? dbgTypes() : { state: "none", why: "this kernel has no DWARF, so no task_struct layout" };
	const L = r.state === "ok" ? varsTaskLayout(r.T) : { why: r.why };
	const c = L.why ? L : varsNotStarted() ? { why: VARS_NOT_STARTED } : varsCurrentTask(r.T);
	d.dataset.state = c.why ? (r.state === "loading" ? "loading" : "none") : "ok";
	if (c.why) {
		const s = document.createElement("span");
		s.className = "dim";
		s.textContent = "not known: " + c.why;
		d.append(s);
		d.title = "The running task (comm and pid of the task_struct current_task points at, with the member offsets from the DWARF) " +
			"is shown here when the kernel has tasks and its DWARF describes current_task and task_struct.";
	} else {
		const n = document.createElement("span");
		n.className = "mono";
		n.textContent = c.comm;
		const w = document.createElement("button");
		w.type = "button";
		w.textContent = "Watch";
		w.title = "Watch *current_task: the whole task_struct in Variables";
		w.addEventListener("click", () => watchAdd("*current_task"));
		d.append(n, " (pid " + c.pid + "), task_struct at " + hex(c.va) + " ", w);
		d.title = "The running task: comm and pid of the task_struct that " + c.how + " points at, read from guest RAM " +
			"with the member offsets from the DWARF.";
		Object.assign(d.dataset, { comm: c.comm, pid: c.pid, va: c.va });
	}
	box.append(d);
}

// ---- the Page listing: which variable a byte belongs to ---------------------

// Physical address of the byte under a pointer event in a hex row, or -1.
function listingByte(ev) {
	const line = ev.target.closest && ev.target.closest("#listing .hex");
	if (!line)
		return -1;
	const rect = line.getBoundingClientRect();
	const text = line.textContent;
	const col = Math.floor((ev.clientX - rect.left) / (rect.width / text.length));
	const per = Number(line.dataset.per), row = Number(line.dataset.row);
	const lo = Number(line.dataset.addr), end = Number(line.dataset.end);
	let i = -1;
	for (let k = 0; k < per; k++) {
		const c = 9 + k * 3 + (per === 16 && k >= 8 ? 1 : 0);
		if (col >= c && col < c + 2)
			i = k;
	}
	const asc = 9 + per * 3 - 1 + (per === 16 ? 1 : 0) + 2;
	if (i < 0 && col >= asc && col < asc + per)
		i = col - asc;
	const b = row + i;
	return i >= 0 && b >= lo && b < end ? b : -1;
}

// "init_task.comm[3]: char = 115 's' in init_task (struct task_struct, ...)".
function varInfoAt(pa) {
	const r = dbgTypes();
	if (r.state !== "ok")
		return { state: r.state, text: hex(pa) + ": " + r.why };
	const T = r.T;
	const w = dwWhere(T, pa);
	if (!w)
		return { state: "none", text: hex(pa) + " (" + hex(pa + varsOffset()) + "): no variable of the DWARF here (heap, stack, " +
			"page tables, code, or data without debug info)" };
	const v = w.var;
	const sc = w.note.startsWith("bit") || w.note === "padding" ? null : varsScalar(T, w.type, w.leafAddr, null);
	let text = hex(pa + varsOffset()) + " " + w.path + (w.offset ? " +" + w.offset : "") +
		(w.note ? " (" + w.note + ")" : "") + ": " + dwTypeName(T, w.type) + (sc && sc.text ? " = " + sc.text : "");
	if (w.unions.length)
		text += "; the same bytes are also " + w.unions.join(", ") + " (union)";
	text += ", in " + v[0] + ": " + dwTypeName(T, v[2]) + ", " + v[3] + " bytes, " + varsSection(T, v) +
		varsNote(T, v).map((n) => "; " + n).join("");
	if (v[5] & 2)
		text += " (this is the template; the Variables panel shows CPU 0's copy)";
	const after = varsNotStarted() ? ". " + VARS_NOT_STARTED : "";
	return { state: "ok", text: text + "; defined at ", after, v, T, path: w.path, note: w.note };
}

let varPinned = -1;

function showVarInfo(pa, pinned) {
	const el = $("var_info");
	el.textContent = "";
	el.dataset.state = "";
	if (pa < 0)
		return;
	const r = varInfoAt(pa);
	el.dataset.state = r.state;
	el.dataset.path = r.path || "";
	el.dataset.addr = pa;
	el.append(r.text);
	if (r.v)
		el.append(varDeclEl(r.T, r.v), r.after);
	if (pinned && r.state === "ok") {
		const b = document.createElement("button");
		b.type = "button";
		b.id = "var_watch";
		b.textContent = "Watch " + r.path;
		b.addEventListener("click", () => {
			watchAdd(r.path);
			api.show.navJump(() => api.show.showTab("debugger"));
		});
		el.append(" ", b);
	}
}

// The DWARF type of a data symbol for its label line in the listing ("" if
// unknown or not loaded); starts the load of the types.
function varTypeOf(sym) {
	if (/^\.text|^\.init\.text|^\.head\.text/.test(sym.section || "") || !dbgHas())
		return "";
	const r = dbgTypes();
	if (r.state !== "ok")
		return "";
	const v = (varsByName(r.T).get(sym.name) || []).find((x) => x[1] === sym.paddr);
	return v ? dwTypeName(r.T, v[2]) : "";
}

function setupVars() {
	// Branch target names: the tooltip when hovered (again once the DWARF
	// it needs is in), the target when clicked (before the line's own
	// click handler).
	let brtHover = null;
	document.addEventListener("mouseover", (ev) => {
		const b = ev.target.closest ? ev.target.closest(".brt") : null;
		brtHover = b;
		if (b)
			b.title = brtTitle(b);
	});
	dbgOnChange(() => {
		if (brtHover && brtHover.isConnected)
			brtHover.title = brtTitle(brtHover);
	});
	document.addEventListener("click", (ev) => {
		const b = ev.target.closest ? ev.target.closest(".brt") : null;
		if (b) {
			ev.stopPropagation();
			api.show.jumpTo(Number(b.dataset.pa));
		}
	}, true);
	const listing = $("listing");
	listing.addEventListener("mousemove", (ev) => {
		if (varPinned >= 0)
			return;
		showVarInfo(listingByte(ev), false);
	});
	listing.addEventListener("mouseleave", () => {
		if (varPinned < 0)
			showVarInfo(-1);
	});
	listing.addEventListener("click", (ev) => {
		const b = listingByte(ev);
		if (b < 0) {
			if (ev.target.closest && ev.target.closest("#listing .hex")) {
				varPinned = -1;
				showVarInfo(-1);
			}
			return;
		}
		varPinned = b === varPinned ? -1 : b;
		showVarInfo(b, varPinned >= 0);
	});
	$("watch_add").addEventListener("click", () => {
		watchAdd($("watch_expr").value);
		$("watch_expr").value = "";
	});
	$("watch_expr").addEventListener("keydown", (ev) => {
		if (ev.key === "Enter")
			$("watch_add").click();
	});
	$("watch_expr").addEventListener("input", watchSuggest);
	$("watch_refresh").addEventListener("click", renderWatches);
	dbgOnChange(() => {
		renderWatches();
		if (varPinned >= 0 || $("var_info").dataset.addr)
			showVarInfo(Number($("var_info").dataset.addr), varPinned >= 0);
	});
}

export { WATCH_KEY, watchList, watchKernel, setWatchKernel, watchLoad, renderWatches, renderLocals, renderTaskRow,
	varTypeOf, setupVars };
