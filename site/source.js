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

import { $, escHtml, hex } from "./util.js";
import { stepper, runState, symbols, api, provide } from "./page.js";
import { dwarfEachLine } from "./dwarftables.js";
import { dbgFrames, dbgFiles, dbgWhere, dbgChainText, dbgTableAt, DBG, dbgFunction, dbgLineCode,
	dbgOnChange } from "./dbginfo.js";
import { srcFile, srcText, srcStatementView, SRC, srcPickTar, srcPickFolder, srcStatus } from "./srctext.js";
import { srcLinkHtml, srcLinkEl, showSource, srcName, srcLocHtml, srcLocEl, srcLinkAgain,
	srcLinkMark } from "./srclinks.js";
import { srcSwClose, srcRemember, srcSw, srcSwRows, SRC_SW_GROUPS, srcFileCounts,
	srcSwMove } from "./srcswitch.js";

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
	const hold = $("src_panel").hidden ? api.show.listingHold() : null;
	$("src_panel").hidden = false;
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

function renderSourcePanel() {
	if (!srcPanel)
		return;
	const { path, line } = srcPanel;
	const body = $("src_body");
	let pl = panelLines();
	const file = srcFile(path);
	// A whole file without the file list (an imported vmlinux): its text,
	// no line marked.
	if (srcPanel.whole && pl.state === "none" && file.state === "ok")
		pl = { state: "ok", first: 1, last: Infinity, code: new Map(), why: pl.why };
	body.dataset.file = path;
	srcTitle(pl.state === "ok" ? ":" + pl.first + "-" + pl.last : "");
	srcOutline();
	if (pl.state !== "ok" || file.state !== "ok") {
		$("src_addrs").hidden = true;
		body.textContent = pl.state === "loading" || file.state === "loading" ? "reading " + (pl.state === "loading" ? "DWARF" : path) + "…" :
			pl.state === "none" ? pl.why : file.why;
		body.dataset.code = body.dataset.executed = 0;
		body.dataset.state = pl.state === "none" || file.state === "none" ? "none" : "loading";
		$("src_info").textContent = "";
		return;
	}
	const { code, run } = lineCoverage(pl);
	const sv = srcStatementView(file, pl.first, pl.last, code, run);
	srcTitle(":" + sv.first + "-" + sv.last);
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
	renderLineAddrs();
	// Opened at a line: scroll there once the file and the address list
	// above it are drawn (the list changes the body's height). The row's
	// offsetParent is the fixed panel, not the body: measure against the body.
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
	const note = (SRC.notes && SRC.notes[path] ? " " + path + ": " + SRC.notes[path] + "." : "") +
		(file.over ? " Patched: this is " + file.over + "." : "");
	body.dataset.over = file.over ? "1" : "";
	const cov = stepper ? api.source.coverageFor(stepper.kernelLo) : null;
	$("src_info").textContent = pl.why ? "No line is marked: " + pl.why + ". Source: " + (SRC.info || SRC.mode) + "." :
		run.size + " of " + code.size + " lines with code have executed (● ran, ○ not yet, ┆ more lines of the statement above or below, not counted)" +
		(!stepper ? "; boot a kernel to see coverage" : !cov.tint ? "; " + cov.why : cov.partial ? "; v86's JIT has run since the kernel entry and records nothing, so ○ lines may have run" : "") +
		". Recorded while stepping" + (srcPanel.whole ? ", every function of the file" : "") + "." +
		(was ? " ◌ ran in the build the coverage tree (Source tab) compares with, not here: " + wasN + " shown." : "") + " Source: " + (SRC.info || SRC.mode) + "." + note;
}

// ---- another file: breadcrumb, outline, file switcher ------------------------

// The title: "fn, dir/dir/file.c:first-last" or "dir/dir/file.c (whole
// file)", each directory of the path a button that lists its files in the
// switcher, the file name one that opens the whole file. Drawn again only
// when it changes (the panel is drawn every second while stepping, and a
// focused button would lose the focus).
function srcTitle(range) {
	const { fn, path, whole } = srcPanel;
	const key = [whole, fn, path, range].join("\0");
	const t = $("src_title");
	if (t.dataset.key === key)
		return;
	t.dataset.key = key;
	t.textContent = "";
	if (!whole)
		t.append((fn || "?") + ", ");
	const parts = path.split("/");
	parts.forEach((p, k) => {
		const b = document.createElement("button");
		b.type = "button";
		b.className = "crumb";
		if (k < parts.length - 1) {
			const dir = parts.slice(0, k + 1).join("/");
			b.textContent = p + "/";
			b.dataset.dir = dir;
			b.title = "List the files in " + dir + "/";
			b.addEventListener("click", () => srcSwOpen(dir));
		} else if (!whole) {
			b.textContent = p;
			b.dataset.path = path;
			b.title = "Open the whole of " + path;
			b.addEventListener("click", () => openSourceFile(path));
		} else {
			t.append(p);
			return;
		}
		t.append(b);
	});
	t.append(whole ? " (whole file)" : range);
}

// The functions of the panel's file (from its line rows, each at its first
// line in the file, and every function files.json puts in it) and the
// files of code inlined into its lines (headers), each one pick away.
function srcFileFacts(path) {
	const c = srcPanel.facts;
	if (c && c.path === path && c.ver === DBG.version)
		return c.r;
	const r = fileLines(path);
	if (r.state === "ok")
		srcPanel.facts = { path, ver: DBG.version, r };
	return r;
}

function srcOutline() {
	const { path } = srcPanel;
	const fl = dbgFiles(), r = srcFileFacts(path);
	const key = [path, r.state, fl.state, DBG.version].join("\0");
	const sel = $("src_outline"), inc = $("src_incl");
	if (sel.dataset.key === key)
		return;
	sel.dataset.key = key;
	const fns = new Map(r.state === "ok" ? r.fns : []);
	const los = new Map();
	if (fl.state === "ok") {
		const k = fl.F.fileIx.get(path);
		for (const fn of fl.F.funcs)
			if (k !== undefined && fn[1] === k && !los.has(fn[0]))
				los.set(fn[0], fn[4]);
	}
	for (const name of los.keys())
		if (!fns.has(name))
			fns.set(name, 0);
	const list = [...fns].sort((x, y) => (x[1] || Infinity) - (y[1] || Infinity) || x[0].localeCompare(y[0]));
	const incl = r.state === "ok" ? [...r.incl].sort() : [];
	sel.textContent = inc.textContent = "";
	sel.append(new Option(list.length ? "ƒ " + list.length + " function" + (list.length === 1 ? "" : "s") + " in this file…" :
		r.state === "loading" ? "ƒ reading the functions…" : "ƒ no functions", ""));
	for (const [name, line] of list) {
		// A function without a line of its own here (only its address):
		// the Page tab at its code.
		const o = new Option(name + "()" + (line ? "  line " + line : "  (no line: opens its code)"), line ? String(line) : "@" + los.get(name));
		o.dataset.fn = name;
		sel.append(o);
	}
	sel.disabled = !list.length;
	inc.append(new Option(incl.length + " header" + (incl.length === 1 ? "" : "s") + " inlined here…", ""));
	for (const p of incl)
		inc.append(new Option(p, p));
	inc.hidden = !incl.length;
	$("src_nav").hidden = !list.length && !incl.length;
}

function srcSwOpen(dir = null) {
	srcSw.dir = dir;
	srcSw.q = "";
	srcSw.at = 0;
	$("src_q").value = "";
	$("src_switch").hidden = false;
	$("src_switch").dataset.dir = dir === null ? "" : dir;
	$("src_files").setAttribute("aria-expanded", "true");
	$("src_q").setAttribute("aria-expanded", "true");
	srcSwRender();
	$("src_q").focus({ preventScroll: true });
}

function srcSwRender() {
	if ($("src_switch").hidden)
		return;
	const rows = srcSwRows();
	srcSw.rows = rows;
	srcSw.at = Math.min(srcSw.at, Math.max(0, rows.length - 1));
	const ul = $("src_list");
	ul.textContent = "";
	let group = null;
	rows.forEach((x, i) => {
		const head = srcSw.dir !== null ? (i === 0 ? "dir" : null) : x.group;
		if (head && head !== group) {
			group = head;
			const h = document.createElement("li");
			h.setAttribute("role", "presentation");
			h.className = "srcswhead";
			h.textContent = head === "dir" ? (srcSw.dir || "the source tree") + "/" : SRC_SW_GROUPS[head];
			ul.append(h);
		}
		const li = document.createElement("li");
		li.setAttribute("role", "option");
		li.id = "src_opt_" + i;
		li.className = "srcopt" + (i === srcSw.at ? " active" : "") + (x.path && srcPanel && x.path === srcPanel.path ? " current" : "");
		li.setAttribute("aria-selected", i === srcSw.at);
		li.dataset.i = i;
		li.dataset.group = x.group;
		if (x.path) {
			li.dataset.path = x.path;
			const c = srcFileCounts(x.info);
			li.dataset.code = c.code === null ? "" : c.code;
			li.dataset.executed = c.run === null ? "" : c.run;
			const k = x.path.lastIndexOf("/");
			const b = document.createElement("b");
			b.textContent = x.path.slice(k + 1);
			const dir = document.createElement("span");
			dir.className = "dim";
			dir.textContent = k >= 0 ? " " + x.path.slice(0, k + 1) : "";
			const n = document.createElement("span");
			n.className = "srcswn";
			const text = SRC.mode === "shipped" && SRC.have && !SRC.have.has(x.path) ? "; no text: " + (SRC.missing[x.path] || "not shipped") : "";
			n.textContent = c.code === null ? (x.info.k < 0 && dbgFiles().state === "ok" ? "no code" : "") + text :
				(c.run === null ? "" : c.run + " of ") + c.code + " line" + (c.code === 1 ? "" : "s") + (c.run === null ? " with code" : " ran") + text;
			li.title = x.path + (c.code === null ? "" : ": " + c.code + " lines with code" + (c.run === null ? (c.why ? "; " + c.why : "") : ", " + c.run + " of them have executed")) +
				(text ? text.replace(/^;/, ".") : "");
			li.append(b, dir, " ", n);
		} else {
			li.dataset.dir = x.dir;
			li.textContent = x.group === "up" ? "↑ " + (x.dir || "the source tree") + "/" : x.dir.slice(x.dir.lastIndexOf("/") + 1) + "/";
			li.title = "List the files in " + (x.dir || "the source tree") + "/";
		}
		ul.append(li);
	});
	if (srcSw.more || srcSw.none) {
		const m = document.createElement("li");
		m.setAttribute("role", "presentation");
		m.className = "note";
		m.textContent = srcSw.none ? (srcSw.q ? "No file matches." : "No file list: " + (dbgFiles().why || SRC.why || "no debug info")) :
			"… and " + srcSw.more.toLocaleString("en") + " more: type to narrow.";
		ul.append(m);
	}
	$("src_q").setAttribute("aria-activedescendant", rows.length ? "src_opt_" + srcSw.at : "");
	if (!rows.length)
		$("src_q").removeAttribute("aria-activedescendant");
}

function srcSwPick(i) {
	const x = srcSw.rows[i];
	if (!x)
		return;
	if (x.path) {
		srcSwClose(false);
		openSourceFile(x.path);
		$("src_body").focus({ preventScroll: true });
		return;
	}
	srcSwOpen(x.dir);
}

function initSourceFiles() {
	$("src_files").addEventListener("click", () => $("src_switch").hidden ? srcSwOpen() : srcSwClose());
	$("src_q").addEventListener("input", () => {
		srcSw.q = $("src_q").value.trim();
		srcSw.at = 0;
		srcSwRender();
	});
	$("src_q").addEventListener("keydown", (ev) => {
		const page = Math.max(1, Math.floor($("src_list").clientHeight / 24));
		const to = { ArrowDown: srcSw.at + 1, ArrowUp: srcSw.at - 1, PageDown: srcSw.at + page, PageUp: srcSw.at - page }[ev.key];
		if (to !== undefined)
			srcSwMove(to);
		else if (ev.key === "Enter")
			srcSwPick(srcSw.at);
		else if (ev.key === "Escape")
			srcSwClose();
		else
			return;
		ev.preventDefault();
		ev.stopPropagation();
	});
	$("src_list").addEventListener("click", (ev) => {
		const li = ev.target.closest(".srcopt");
		if (li)
			srcSwPick(Number(li.dataset.i));
	});
	$("src_outline").addEventListener("change", () => {
		const v = $("src_outline").value;
		$("src_outline").value = "";
		if (!srcPanel || !v)
			return;
		if (v[0] === "@")
			api.show.jumpTo(Number(v.slice(1)));
		else
			openSourceLine(srcPanel.path, Number(v));
	});
	$("src_incl").addEventListener("change", () => {
		const v = $("src_incl").value;
		$("src_incl").value = "";
		if (v)
			openSourceFile(v);
	});
	// v86 takes every key on the window for the VM while the Machine tab
	// shows its screen (preventDefault): the panel's buttons get Enter and
	// Space here, before it.
	$("src_panel").addEventListener("keydown", (ev) => {
		if ((ev.key === "Enter" || ev.key === " ") && ev.target.tagName === "BUTTON") {
			ev.preventDefault();
			ev.stopPropagation();
			ev.target.click();
		}
	});
}

// The panel's picked line (a click on a line, or the line a listing's C
// line opened): every address of its code (each inlined copy and each
// inlined call made on it, dbgLineCode) with a jump to it, and "Run to
// file:line". A line that continues a statement shows its first line's.
const SRC_ADDRS_SHOWN = 40;

function renderLineAddrs() {
	const box = $("src_addrs");
	const pick = srcPanel && srcPanel.pick;
	box.hidden = !pick;
	if (!pick)
		return;
	const c = srcPanel.cont && srcPanel.cont.get(pick);
	const line = c ? c.start : pick;
	const r = dbgLineCode(srcPanel.path, line);
	const key = [srcPanel.path, pick, r.state, DBG.version, !!stepper].join("\0");
	if (box.dataset.key === key)
		return;
	box.dataset.key = key;
	box.textContent = "";
	box.dataset.state = r.state;
	box.dataset.line = line;
	box.dataset.path = r.path || srcPanel.path;
	const where = (r.path || srcPanel.path) + ":" + line;
	const head = document.createElement("div");
	head.append(srcLocEl(where), c ? " (line " + pick + " continues this statement): " : ": ");
	box.append(head);
	if (r.state !== "ok") {
		head.append(r.state === "loading" ? r.why || "reading DWARF…" : r.why);
		box.dataset.bps = box.dataset.rows = box.dataset.calls = "[]";
		return;
	}
	box.dataset.bps = JSON.stringify(r.bps);
	box.dataset.rows = JSON.stringify(r.rows.map(([a, b]) => [a, b]));
	box.dataset.calls = JSON.stringify(r.calls.map(([a, b, name]) => [a, b, name]));
	const outer = (pa) => {
		const f = dbgFrames(pa, true);
		return f.state === "ok" ? f.frames : null;
	};
	const fns = new Set(r.rows.map(([a]) => { const f = outer(a); return f ? f[f.length - 1].func : "?"; }));
	head.append(r.rows.length + " range" + (r.rows.length === 1 ? "" : "s") + " of its own code" +
		(fns.size > 1 ? " in " + fns.size + " functions (inlined copies)" : "") +
		(r.calls.length ? ", " + r.calls.length + " range" + (r.calls.length === 1 ? "" : "s") + " of code inlined by a call on it" : "") + ". ");
	const run = document.createElement("button");
	run.type = "button";
	run.id = "src_runto";
	run.textContent = "Run to " + where;
	run.disabled = !stepper;
	run.title = !stepper ? "Boot a kernel first" : "Run until the kernel reaches the first of this line's " + r.bps.length +
		" breakpoint address" + (r.bps.length === 1 ? "" : "es") + " (the is_stmt rows of the line table, and where each inlined call starts)" +
		(r.noStmt ? ". This line table has no is_stmt flags: every range's start counts" : "");
	run.addEventListener("click", () => api.step.runToLine(where));
	head.append(run);
	const list = document.createElement("ul");
	const item = (a, b, text, title) => {
		const li = document.createElement("li");
		const btn = api.symbols.symButton(hex(a), a, "Show this code in the Page tab");
		btn.dataset.addr = a;
		const s = symbols && api.symbols.symbolAt(symbols.symbols, a);
		li.append(btn, " " + (s ? s.name + (a > s.paddr ? "+0x" + (a - s.paddr).toString(16) : "") : "") + " (" + (b - a) + " byte" + (b - a === 1 ? "" : "s") + ")" + text);
		li.title = title;
		list.append(li);
	};
	const all = [...r.rows.map((x) => ["row", x]), ...r.calls.map((x) => ["call", x])].sort((x, y) => x[1][0] - y[1][0]);
	for (const [kind, x] of all.slice(0, SRC_ADDRS_SHOWN)) {
		if (kind === "row") {
			const [a, b, bp] = x;
			const f = outer(a);
			const into = f && f.length > 1 ? "; inlined into " + (f[f.length - 1].func || "?") : "";
			item(a, b, into + (bp.length ? "; breakpoint" + (bp.length > 1 ? "s at " + bp.map(hex).join(", ") : bp[0] === a ? " here" : " at " + hex(bp[0])) :
				"; no is_stmt row here"), "Code of " + where + (f ? "\n" + dbgChainText(f) : ""));
		} else {
			const [a, b, name] = x;
			item(a, b, "; " + (name || "?") + "() inlined by the call on this line; breakpoint at its start", "Code of " + (name || "?") + ", inlined at " + where);
		}
	}
	if (all.length > SRC_ADDRS_SHOWN) {
		const li = document.createElement("li");
		li.textContent = "… and " + (all.length - SRC_ADDRS_SHOWN) + " more (Run to sets all " + r.bps.length + " breakpoints)";
		list.append(li);
	}
	box.append(list);
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
	$("src_panel").hidden = true;
	srcPanel = null;
	api.source.edPanelShown();
	api.show.writeHash();
	api.show.listingKeep(hold);
	const a = srcLinkAgain(srcOpener);
	if (a)
		a.focus();
	srcOpener = null;
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
			$("src_body").focus({ preventScroll: true });
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
	$("src_panel").addEventListener("keydown", (ev) => {
		if (ev.key === "Escape")
			closeSource();
	});
	initSourceFiles();
	$("src_close").addEventListener("click", closeSource);
	$("src_body").addEventListener("click", (ev) => {
		const row = ev.target.closest("[data-line]");
		if (!row || !srcPanel || window.getSelection().toString())
			return;
		srcPanel.pick = Number(row.dataset.line);
		for (const x of $("src_body").querySelectorAll(".pick"))
			x.classList.remove("pick");
		row.classList.add("pick");
		renderLineAddrs();
		api.show.writeHash();
		row.scrollIntoView({ block: "nearest" }); // the address list above can push it out of view
	});
	for (const id of ["listing", "context"])
		$(id).addEventListener("click", (ev) => {
			const line = ev.target.closest(".src");
			if (line && !line.classList.contains("nol")) {
				ev.stopPropagation();
				openSource(Number(line.dataset.addr));
			}
		}, true);
	$("show_src").addEventListener("change", () => {
		api.show.inspect();
		api.show.renderContext();
	});
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
		if (srcPanel && !$("src_panel").hidden) {
			renderSourcePanel();
			srcSwRender();
		}
	});
	setInterval(() => {
		if (srcPanel && !$("src_panel").hidden && (runState === "stepped"))
			renderSourcePanel();
	}, 1000);
}

provide({ srcLinkHtml, srcLinkEl });

export { srcPanel, setSrcPanel, srcChainNodes, srcLineHtml, openSource, openSourceFile, srcShowFile,
	openSourceLine, fileLines, renderSourcePanel, renderSourceRows, closeSource, initSource };
