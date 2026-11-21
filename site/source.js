"use strict";

// C source in the listings (objdump -S style), the Debugger's source rows
// and the source panel with live line coverage. Which file:line (and
// inlined-at chain) an address belongs to comes from the kernel's DWARF
// (dbginfo.js); the text of those lines from one of:
//   shipped  kernels/<name>/src/ (site/gen-src.js in CI): index.json and
//            <path>.gz per file the DWARF names, fetched the first time a
//            line of it is shown
//   picked   a source folder or .tar the user picked (an imported vmlinux,
//            or a kernel without shipped source): read from the File the
//            first time a line of it is shown; for a vmlinux whose files
//            genimport.js made, it makes src/ from the pick and the kernel
//            reads that as shipped
// srcFile() answers synchronously from what is loaded and starts the load
// of the rest; dbgChanged() re-renders when it arrives. The text is
// srctext.js's, the links srclinks.js's, the switcher's lists srcswitch.js's.

import { $, escHtml } from "./util.js";
import { stepper, runState, api, provide } from "./page.js";
import { dwarfEachLine } from "./dwarftables.js";
import { dbgFrames, dbgFiles, dbgWhere, dbgChainText, dbgTableAt, DBG, dbgFunction, dbgOnChange } from "./dbginfo.js";
import { srcFile, srcText, srcStatementView, SRC, srcPickTar, srcPickFolder, srcStatus } from "./srctext.js";
import { srcLinkHtml, srcLinkEl, showSource, srcName, srcLocHtml, srcLinkAgain, srcLinkMark } from "./srclinks.js";
import { srcSw, srcSwOpen, srcSwClose, srcRemember, srcSwRender } from "./srcswitch.js";
import { SRCV, srcOpen, srcShow } from "./srcview.js";

let srcPanel = null;      // { pa, fn, path, line } shown in the side panel
const setSrcPanel = (p) => { srcPanel = p; }; // for other modules (an imported binding is read-only)

// Open the Source panel at path:line, highlighted and scrolled into view.
// Where pa's inline chain has that line (pa's own line, or the call site
// in a function it was inlined into), the panel shows that function with
// its executed lines; else the whole file (its lines with code from
// files.json); an imported vmlinux has no file list, so a function's
// declaration then opens in the function around pa.
function srcOpenAt(path, line, pa) {
	api.show.navJump(() => {
		const r = pa >= 0 ? dbgFrames(pa) : { state: "none" };
		const k = r.state === "ok" ? r.frames.findIndex((f) => f.file === path && f.line === line) : -1;
		if (k >= 0)
			openSource(pa, k);
		else if (pa >= 0 && r.state === "ok" && dbgFiles().state === "none") {
			openSource(pa);
			srcPanel.pick = srcPanel.scrollTo = line;
			renderSourcePanel();
		} else
			openSourceLine(path, line);
	});
}

// "func at file:line; inlined into ..." for pa as nodes, file:line a link.
function srcChainNodes(pa, sym) {
	const r = pa >= 0 ? dbgFrames(pa) : { state: "none", why: "" };
	if (r.state !== "ok")
		return [r.state === "loading" ? "reading DWARF…" : r.why];
	const out = [];
	r.frames.forEach((f, k) => {
		out.push((k ? "; inlined into " : "") + (f.func || sym || "?") + (f.file && f.line ? " at " : " (no line information)"));
		if (f.file && f.line)
			out.push(srcLinkEl(f.file, f.line, dbgWhere(f), pa));
	});
	return out;
}

// Why the text of frame f's line is not shown (title text), or "".
function srcWhyNot(f) {
	const s = srcFile(f.file);
	if (s.state === "loading")
		return "reading " + f.file + "…";
	if (s.state === "none")
		return s.why;
	return f.line > s.lines.length ? f.file + " has only " + s.lines.length + " lines" : "";
}

// A C line for the listing when pa starts a new source location; `state`
// keeps the last one printed in this listing.
function srcLineHtml(pa, state) {
	if (!showSource())
		return null;
	const r = dbgFrames(pa);
	if (r.state === "loading") {
		if (state.key === "loading")
			return null;
		state.key = "loading";
		return '<span class="src dim" data-addr="' + pa + '">reading DWARF…</span>';
	}
	if (r.state !== "ok") {
		// Inside the image's code but no line here (padding, assembly): say
		// so once after code that had a line.
		if (state.key === null || state.key === "none")
			return null;
		state.key = "none";
		return '<span class="src nol" data-addr="' + pa + '" data-tip="' + escHtml(r.why) + '">(no line information)</span>';
	}
	const fr = r.frames;
	const f = fr[0];
	const key = fr.map((x) => x.func + "@" + x.file + ":" + x.line).join("<");
	if (key === state.key)
		return null;
	state.key = key;
	if (!f.file || !f.line)
		return '<span class="src nol" data-addr="' + pa + '" data-tip="' + escHtml(dbgChainText(fr)) + '">(no line information' +
			(f.func ? " in " + escHtml(f.func) : "") + ")</span>";
	const text = srcText(f.file, f.line);
	const why = text === null ? srcWhyNot(f) : "";
	const inl = fr.length > 1 ? " [" + escHtml(f.func || "?") + " inlined into " + escHtml(fr[fr.length - 1].func || "?") + "]" : "";
	return '<span class="src" data-addr="' + pa + '" data-tip="' + escHtml(dbgChainText(fr) + (why ? "\n" + why : "")) + '">' +
		srcLinkHtml(f.file, f.line, srcName(f), pa) + "  " + (text === null ? '<span class="dim">' + escHtml(f.file) + "</span>" : '<span class="srctext">' + escHtml(text.trim()) + "</span>") +
		inl + "</span>";
}

// "file.c:123 text" of the instruction at pa, or "".
function srcHere(pa) {
	const r = dbgFrames(pa);
	if (r.state !== "ok" || !r.frames[0].file || !r.frames[0].line)
		return "";
	const f = r.frames[0];
	const t = srcText(f.file, f.line);
	return srcName(f) + (t ? "  " + t.trim() : "");
}

// Open the side panel with the whole function around the C line of pa:
// frame k of its inline chain (0: the innermost, the line of pa itself;
// above: the call site in the function it was inlined into). A file
// without text opens too: the panel says why.
function openSource(pa, k = 0) {
	const r = dbgFrames(pa);
	if (r.state !== "ok")
		return;
	const loc = r.frames[k];
	if (!loc || !loc.file || !loc.line)
		return;
	api.show.navJump(() => {
		srcPanel = { pa, fn: loc.func, path: loc.file, line: loc.line, pick: loc.line, scrollTo: loc.line };
		srcShown();
	});
}

// After srcPanel changed: shown, drawn, remembered, in the hash.
function srcShown() {
	srcSwClose(false);
	const hold = srcOpen() ? null : api.show.listingHold();
	srcShow(true);
	renderSourcePanel();
	srcRemember(srcPanel.path);
	api.show.writeHash();
	api.source.edPanelShown();
	api.show.listingKeep(hold);
}

// Open the side panel with a whole file (the Coverage tab): every line of
// it with code, in any function, and which of them have executed.
function openSourceFile(path) {
	api.show.navJump(() => srcShowFile(path, 0));
}

// The whole file without a history entry (openSourceFile, openSourceLine,
// a reload's hash): at line, else at its first line that ran (or has code).
function srcShowFile(path, line) {
	srcPanel = { pa: -1, fn: null, path, line, whole: true, pick: line || undefined, scrollTo: line || "first" };
	srcShown();
}

// Open the side panel with a whole file at one line, picked (a function's
// declaration): its addresses are listed as for a tapped line.
function openSourceLine(path, line) {
	api.show.navJump(() => srcShowFile(path, line));
}

// A whole file's lines with code: the line rows of every shard that has
// code of it (files.json), each frame naming the file, like the Coverage
// tab counts them.
function fileLines(path) {
	const fl = dbgFiles();
	if (fl.state !== "ok")
		return fl;
	const F = fl.F, fi = F.fileIx.get(path);
	// A file without code (data only: "defined at" of a variable) is shown with none marked.
	if (fi === undefined)
		return { state: "ok", first: 1, last: Infinity, code: new Map(), fns: new Map(), incl: new Set() };
	const size = 2 ** F.shardBits;
	const tables = F.files[fi][2].map((s) => [s * size, (s + 1) * size, dbgTableAt(s * size)]);
	const wait = tables.find(([, , r]) => r.state !== "ok");
	if (wait)
		return wait[2];
	// The copies of its hot patched functions (E8) count with the file.
	if (DBG.patch && DBG.patch.t.strings.includes(path))
		tables.push([DBG.patch.lo, DBG.patch.hi, { t: DBG.patch.t }]);
	const code = new Map();
	const fns = new Map();   // function -> its first line in this file
	const incl = new Set();  // files of code inlined into this file's lines
	for (const [lo, hi, r] of tables)
		dwarfEachLine(r.t, lo, hi, (a, b, frames) => {
			let mine = -1;
			frames.forEach((f, k) => {
				if (f.file === path && f.line) {
					if (!code.has(f.line))
						code.set(f.line, []);
					code.get(f.line).push([a, b]);
					if (mine < 0)
						mine = k;
					if (f.func && !(fns.get(f.func) <= f.line))
						fns.set(f.func, f.line);
				}
			});
			for (let k = 0; k < mine; k++)
				if (frames[k].file && frames[k].file !== path)
					incl.add(frames[k].file);
		});
	return { state: "ok", first: 1, last: Infinity, code, fns, incl };
}

// The panel's lines from the DWARF of the function around srcPanel.pa:
// first/last are the lines of srcPanel.fn in its file (directly, not
// through code inlined into it), widened by two lines; code has every
// line of that span with code (also as the call site of inlined code) and
// the address ranges of each. Kept per DWARF version.
function panelLines() {
	if (srcPanel.lines && srcPanel.dbgVersion === DBG.version)
		return srcPanel.lines;
	if (srcPanel.whole) {
		const fl = fileLines(srcPanel.path);
		if (fl.state === "ok") {
			srcPanel.lines = fl;
			srcPanel.dbgVersion = DBG.version;
		}
		return fl;
	}
	const fn = dbgFunction(srcPanel.pa);
	if (fn.state !== "ok")
		return fn;
	const { path } = srcPanel;
	let lo = srcPanel.line, hi = srcPanel.line;
	for (const [, , frames] of fn.rows)
		for (const f of frames)
			if (f.func === srcPanel.fn && f.file === path && f.line) {
				lo = Math.min(lo, f.line);
				hi = Math.max(hi, f.line);
			}
	const first = Math.max(1, lo - 2), last = hi;
	const code = new Map(); // line -> [[a, b], ...]
	for (const [a, b, frames] of fn.rows)
		for (const f of frames)
			if (f.file === path && f.line >= first && f.line <= last) {
				if (!code.has(f.line))
					code.set(f.line, []);
				code.get(f.line).push([a, b]);
			}
	srcPanel.lines = { state: "ok", first, last, code, lo: fn.lo, hi: fn.hi };
	srcPanel.dbgVersion = DBG.version;
	return srcPanel.lines;
}

// Lines that have code, and those whose code has run (an instruction
// started in one of its address ranges while stepping).
function lineCoverage(pl) {
	const run = new Set();
	if (stepper)
		for (const [l, spans] of pl.code)
			if (spans.some(([a, b]) => stepper.countIn(a, b) > 0))
				run.add(l);
	return { code: new Set(pl.code.keys()), run };
}

// The panel's text, drawn into its <pre> (SRCV.bodyEl: thousands of lines
// of HTML, written as the Page listing is); the title, the outline, the
// picked line's addresses and the line under the text are sourceaside.jsx's.
function renderSourcePanel() {
	if (!srcPanel)
		return;
	const { path, line } = srcPanel;
	const body = SRCV.bodyEl;
	let pl = panelLines();
	const file = srcFile(path);
	// A whole file without the file list (an imported vmlinux): its text,
	// no line marked.
	if (srcPanel.whole && pl.state === "none" && file.state === "ok")
		pl = { state: "ok", first: 1, last: Infinity, code: new Map(), why: pl.why };
	body.dataset.file = path;
	SRCV.shown = srcPanel;
	SRCV.range = pl.state === "ok" ? ":" + pl.first + "-" + pl.last : "";
	if (pl.state !== "ok" || file.state !== "ok") {
		srcPanel.cont = null;
		srcPanel.ready = false;
		body.textContent = pl.state === "loading" || file.state === "loading" ? "reading " + (pl.state === "loading" ? "DWARF" : path) + "…" :
			pl.state === "none" ? pl.why : file.why;
		body.dataset.code = body.dataset.executed = 0;
		body.dataset.state = pl.state === "none" || file.state === "none" ? "none" : "loading";
		SRCV.info = "";
		SRCV.changed();
		return;
	}
	const { code, run } = lineCoverage(pl);
	const sv = srcStatementView(file, pl.first, pl.last, code, run);
	SRCV.range = ":" + sv.first + "-" + sv.last;
	// The coverage tree compares with an earlier build (E8): ◌ a line that
	// ran there (moved to this text) and not here.
	const was = api.source.covWas(path);
	let wasN = 0;
	const out = [];
	for (let l = sv.first; l <= sv.last; l++) {
		const c = sv.cont.get(l);
		const ran = run.has(l) || (c && c.ran);
		const gone = !!was && was.has(l) && !run.has(l) && !c;
		wasN += gone;
		const cls = (ran ? "exec" : code.has(l) || c ? "code" : "") + (c ? " cont" : "") + (gone ? " was" : "");
		const mark = c ? "┆" : run.has(l) ? "●" : gone ? "◌" : code.has(l) ? "○" : " ";
		const title = c ? ' title="' + escHtml("Line " + l + " is part of the statement at lines " + c.start + "–" + c.end + ", which " +
			(c.ran ? "ran" : "has not run") + ". Not counted: only lines with code of their own in the line table are counted, here, in the Coverage tab and in the lcov export.") + '"' :
			gone ? ' title="' + escHtml("Line " + l + " ran in the build the coverage tree (Source tab) compares with, and not (yet) in this one.") + '"' : "";
		out.push('<span class="' + cls + (l === line ? " here" : "") + (l === srcPanel.pick ? " pick" : "") + '" data-line="' + l + '"' + title + ">" + srcLocHtml(String(l).padStart(5, " ")) + " " + mark + " " +
			'<span class="srctext">' + escHtml(file.lines[l - 1]) + "</span>" + api.show.bmLineStar(path, l) + "</span>");
	}
	body.innerHTML = out.join("\n");
	srcPanel.cont = sv.cont;
	srcPanel.ready = true;
	const note = (SRC.notes && SRC.notes[path] ? " " + path + ": " + SRC.notes[path] + "." : "") +
		(file.over ? " Patched: this is " + file.over + "." : "");
	const cov = stepper ? api.source.coverageFor(stepper.kernelLo) : null;
	SRCV.info = pl.why ? "No line is marked: " + pl.why + ". Source: " + (SRC.info || SRC.mode) + "." :
		run.size + " of " + code.size + " lines with code have executed (● ran, ○ not yet, ┆ more lines of the statement above or below, not counted)" +
		(!stepper ? "; boot a kernel to see coverage" : !cov.tint ? "; " + cov.why : cov.partial ? "; v86's JIT has run since the kernel entry and records nothing, so ○ lines may have run" : "") +
		". Recorded while stepping" + (srcPanel.whole ? ", every function of the file" : "") + "." +
		(was ? " ◌ ran in the build the coverage tree (Source tab) compares with, not here: " + wasN + " shown." : "") + " Source: " + (SRC.info || SRC.mode) + "." + note;
	// The title, the outline and the picked line's addresses (above the
	// text: they change its height) are drawn before the scroll.
	SRCV.changed();
	// Opened at a line: scroll there once the file and the address list
	// above it are drawn. The row's offsetParent is the fixed panel, not
	// the body: measure against the body.
	if (srcPanel.scrollPx !== undefined) {
		body.scrollTop = srcPanel.scrollPx; // Back to this view: where it was left
		delete srcPanel.scrollPx;
		delete srcPanel.scrollTo;
	} else if (srcPanel.scrollTo) {
		const row = srcPanel.scrollTo === "first" ? body.querySelector(".exec") || body.querySelector(".code") :
			body.querySelector('[data-line="' + srcPanel.scrollTo + '"]');
		if (row)
			body.scrollTop += row.getBoundingClientRect().top - body.getBoundingClientRect().top - body.clientHeight / 3;
		else if (srcPanel.scrollTo === "first")
			body.scrollTop = 0;
		delete srcPanel.scrollTo;
	}
	body.dataset.state = "ok";
	body.dataset.code = code.size;
	body.dataset.executed = run.size;
	body.dataset.was = was ? wasN : "";
	body.dataset.over = file.over ? "1" : "";
}

// The Debugger's "source" rows: the C line at EIP and, for inlined code,
// each function it was inlined into with the line of the call.
function renderSourceRows(box, phys, sym) {
	const row = document.createElement("div");
	row.id = "cur_src";
	const b = document.createElement("b");
	b.textContent = "source";
	row.append(b, " ");
	const r = phys >= 0 ? dbgFrames(phys) : { state: "none", why: "EIP is not mapped" };
	row.dataset.state = r.state;
	row.dataset.frames = JSON.stringify(r.state === "ok" ? r.frames : []);
	row.dataset.src = srcHere(phys);
	if (r.state === "loading")
		row.append("reading DWARF…");
	else if (r.state === "none")
		row.append(r.why);
	else {
		r.frames.forEach((f, k) => {
			const line = document.createElement("div");
			line.className = "frame" + (k ? " inl" : "");
			const t = f.file && f.line ? srcText(f.file, f.line) : null;
			const fn = f.func || sym || "?";
			const loc = f.file && f.line ? srcLinkEl(f.file, f.line, dbgWhere(f), phys) : null;
			if (loc && t === null)
				loc.title += "\n" + srcWhyNot(f);
			if (k)
				line.append("inlined into " + fn, ...(loc ? [" at ", loc] : [" (no line information)"]));
			else
				line.append(loc || "no line information", " in " + fn + (r.frames.length > 1 ? " (inlined)" : ""));
			if (t !== null) {
				const s = document.createElement("span");
				s.className = "srctext mono";
				s.textContent = " " + t.trim();
				line.append(s);
			}
			row.append(line);
		});
	}
	box.append(row);
}

// The .srclink that opened the panel from the keyboard: Escape or Close
// gives it the focus back.
let srcOpener = null;

function closeSource() {
	srcSwClose(false);
	const hold = api.show.listingHold();
	srcShow(false);
	srcPanel = null;
	api.source.edPanelShown();
	api.show.writeHash();
	api.show.listingKeep(hold);
	const a = srcLinkAgain(srcOpener);
	if (a)
		a.focus();
	srcOpener = null;
}

// The switcher's row i: a file opens, a directory lists its files.
function srcSwPick(i) {
	const x = srcSw.rows[i];
	if (!x)
		return;
	if (x.path) {
		srcSwClose(false);
		openSourceFile(x.path);
		SRCV.bodyEl.focus({ preventScroll: true });
		return;
	}
	srcSwOpen(x.dir);
}

// A click on a line of the text picks it: its addresses are listed above.
function srcBodyClick(ev) {
	const row = ev.target.closest("[data-line]");
	if (!row || !srcPanel || window.getSelection().toString())
		return;
	srcPanel.pick = Number(row.dataset.line);
	for (const x of SRCV.bodyEl.querySelectorAll(".pick"))
		x.classList.remove("pick");
	row.classList.add("pick");
	SRCV.changed();
	api.show.writeHash();
	row.scrollIntoView({ block: "nearest" }); // the address list above can push it out of view
}

// A click (capture) on a listing's C line (Page listing, Code at EIP)
// opens its function in the Source panel.
function srcLineClick(ev) {
	const line = ev.target.closest(".src");
	if (line && !line.classList.contains("nol")) {
		ev.stopPropagation();
		openSource(Number(line.dataset.addr));
	}
}

function initSource() {
	// Every .srclink (srcLinkEl, srcLinkHtml) in any view; before the
	// listings' own handlers. A keyboard click (detail 0) moves the focus
	// into the panel.
	document.addEventListener("click", (ev) => {
		const a = ev.target.closest ? ev.target.closest(".srclink") : null;
		if (!a)
			return;
		ev.stopPropagation();
		ev.preventDefault();
		srcOpenAt(a.dataset.file, Number(a.dataset.line), Number(a.dataset.pa));
		srcOpener = ev.detail === 0 ? a : null;
		if (srcOpener) {
			srcLinkMark(a);
			SRCV.bodyEl.focus({ preventScroll: true });
		}
	}, true);
	// v86 takes every key on the window for the VM (preventDefault), which
	// stops a button's own Enter and Space: click the link here first.
	document.addEventListener("keydown", (ev) => {
		const a = ev.target.closest ? ev.target.closest(".srclink") : null;
		if (a && (ev.key === "Enter" || ev.key === " ")) {
			ev.preventDefault();
			ev.stopPropagation();
			a.click();
		}
	}, true);
	$("src_tar").addEventListener("change", () => {
		const f = $("src_tar").files[0];
		if (f)
			srcPickTar(f);
	});
	$("src_dir_btn").addEventListener("click", () => $("src_dir").click());
	$("src_dir").addEventListener("change", () => {
		if ($("src_dir").files.length)
			srcPickFolder($("src_dir").files);
	});
	dbgOnChange(() => {
		srcStatus();
		if (srcPanel && srcOpen()) {
			renderSourcePanel();
			srcSwRender();
		}
	});
	setInterval(() => {
		if (srcPanel && srcOpen() && (runState === "stepped"))
			renderSourcePanel();
	}, 1000);
}

provide({ srcLinkHtml, srcLinkEl });

export { srcPanel, setSrcPanel, srcChainNodes, srcBodyClick, srcSwPick, srcLineHtml, openSource, openSourceFile, srcShowFile,
	openSourceLine, fileLines, renderSourcePanel, renderSourceRows, closeSource, initSource, srcLineClick };
