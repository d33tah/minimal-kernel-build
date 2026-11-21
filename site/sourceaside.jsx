// The Source panel (A1): the aside beside every tab (#src_panel) with a
// file's text (source.js), its title, file switcher and outline, the picked
// line's addresses, and the editor (srcedit.js) with its Rebuild and boot
// and hot patch buttons (rebuild.jsx, hotpatch.jsx). A component on
// srcview.js's SRCV, SWV and EDSTATE, mounted at load (shell.jsx: small,
// and the editor and the links of every view use it). The file's text, the
// editor's textarea with its line numbers, and the file's diff are written
// by source.js and srcedit.js through their refs (thousands of lines; the
// textarea is the browser's editor). The panel's own element (box) is shown
// and marked editing here. Element ids and data attributes are the ones the
// tests and the modules read.

import { memo, useLayoutEffect } from "react";
import { hex } from "./util.js";
import { stepper, symbols, api } from "./page.js";
import { useController, DomNodes } from "./ui.jsx";
import { SRCV, SWV, EDBAR, EDSTATE } from "./srcview.js";
import { srcPanel, closeSource, openSourceFile, openSourceLine, fileLines, srcBodyClick, srcSwPick } from "./source.js";
import { srcSw, srcSwOpen, srcSwClose, srcSwFilter, srcSwMove, srcFileCounts, SRC_SW_GROUPS } from "./srcswitch.js";
import { SRC } from "./srctext.js";
import { DBG, dbgFiles, dbgFrames, dbgLineCode, dbgChainText } from "./dbginfo.js";
import { ED, ED_TA, edMap, edCan, edToggle, edDiffToggle, edRevertPanel } from "./srcedit.js";
import { bmAsk, bmDraftHere } from "./bookmarks.js";
import { RebuildPanelButton } from "./rebuild.jsx";
import { HotPatchButton } from "./hotpatch.jsx";

const SRC_ADDRS_SHOWN = 40;

// The title: "fn, dir/dir/file.c:first-last" or "dir/dir/file.c (whole
// file)", each directory of the path a button that lists its files in the
// switcher, the file name one that opens the whole file.
function SrcTitle() {
	return <b id="src_title">{srcPanel ? <SrcTitleParts /> : null}</b>;
}

function SrcTitleParts() {
	const { fn, path, whole } = srcPanel;
	const parts = path.split("/");
	return (
		<>
			{whole ? null : (fn || "?") + ", "}
			{parts.map((p, k) => {
				if (k < parts.length - 1) {
					const dir = parts.slice(0, k + 1).join("/");
					return <button key={k} type="button" className="crumb" data-dir={dir} title={"List the files in " + dir + "/"}
						onClick={() => srcSwOpen(dir)}>{p + "/"}</button>;
				}
				return whole ? p : <button key={k} type="button" className="crumb" data-path={path} title={"Open the whole of " + path}
					onClick={() => openSourceFile(path)}>{p}</button>;
			})}
			{whole ? " (whole file)" : SRCV.range}
		</>
	);
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

// Kept per file, its facts' state and the DWARF version (the panel is drawn
// every second while stepping, and again for each file opened; the
// functions are looked up in every one of the kernel's).
let outlineLast = null;

function srcOutlineView() {
	const { path } = srcPanel;
	const fl = dbgFiles(), r = srcFileFacts(path);
	const key = [path, r.state, fl.state, DBG.version].join("\0");
	if (outlineLast && outlineLast.key === key)
		return outlineLast;
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
	outlineLast = { key, list, los, state: r.state, incl: r.state === "ok" ? [...r.incl].sort() : [] };
	return outlineLast;
}

// A function of the outline: its line, or (no line of its own here) the
// Page tab at its code; a header: the whole of it.
function outlinePick(v) {
	if (!srcPanel || !v)
		return;
	if (v[0] === "@")
		api.show.jumpTo(Number(v.slice(1)));
	else
		openSourceLine(srcPanel.path, Number(v));
}

function SrcNav() {
	const o = srcPanel ? srcOutlineView() : { list: [], los: new Map(), state: "none", incl: [] };
	const { list, incl } = o;
	return (
		<div id="src_nav" className="srcnav" hidden={!list.length && !incl.length}>
			<select id="src_outline" aria-label="Go to a function of this file" value="" disabled={!list.length}
				onChange={(e) => outlinePick(e.target.value)}>
				<option value="">{list.length ? "ƒ " + list.length + " function" + (list.length === 1 ? "" : "s") + " in this file…" :
					o.state === "loading" ? "ƒ reading the functions…" : "ƒ no functions"}</option>
				{list.map(([name, line]) => (
					<option key={name} value={line ? String(line) : "@" + o.los.get(name)} data-fn={name}>
						{name + "()" + (line ? "  line " + line : "  (no line: opens its code)")}</option>
				))}
			</select>
			{" "}
			<select id="src_incl" aria-label="Open a header whose code is inlined into this file" value="" hidden={!incl.length}
				onChange={(e) => { if (e.target.value) openSourceFile(e.target.value); }}>
				<option value="">{incl.length + " header" + (incl.length === 1 ? "" : "s") + " inlined here…"}</option>
				{incl.map((p) => <option key={p} value={p}>{p}</option>)}
			</select>
		</div>
	);
}

// One row of the switcher: a file with its counts, or a directory.
const SwRow = memo(function SwRow({ x, i, active, current }) {
	if (!x.path)
		return <li role="option" id={"src_opt_" + i} className={"srcopt" + (active ? " active" : "")} aria-selected={active} data-i={i}
			data-group={x.group} data-dir={x.dir} title={"List the files in " + (x.dir || "the source tree") + "/"}>
			{x.group === "up" ? "↑ " + (x.dir || "the source tree") + "/" : x.dir.slice(x.dir.lastIndexOf("/") + 1) + "/"}</li>;
	const c = srcFileCounts(x.info);
	const k = x.path.lastIndexOf("/");
	const text = SRC.mode === "shipped" && SRC.have && !SRC.have.has(x.path) ? "; no text: " + (SRC.missing[x.path] || "not shipped") : "";
	return (
		<li role="option" id={"src_opt_" + i} className={"srcopt" + (active ? " active" : "") + (current ? " current" : "")} aria-selected={active}
			data-i={i} data-group={x.group} data-path={x.path} data-code={c.code === null ? "" : c.code} data-executed={c.run === null ? "" : c.run}
			title={x.path + (c.code === null ? "" : ": " + c.code + " lines with code" + (c.run === null ? (c.why ? "; " + c.why : "") : ", " + c.run + " of them have executed")) +
				(text ? text.replace(/^;/, ".") : "")}>
			<b>{x.path.slice(k + 1)}</b>
			<span className="dim">{k >= 0 ? " " + x.path.slice(0, k + 1) : ""}</span>
			{" "}
			<span className="srcswn">{c.code === null ? (x.info.k < 0 && dbgFiles().state === "ok" ? "no code" : "") + text :
				(c.run === null ? "" : c.run + " of ") + c.code + " line" + (c.code === 1 ? "" : "s") + (c.run === null ? " with code" : " ran") + text}</span>
		</li>
	);
});

// The filter's keys: arrows and pages move, Enter opens, Escape closes.
function swKey(ev) {
	const page = Math.max(1, Math.floor(SWV.listEl.clientHeight / 24));
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
}

const qMount = (el) => { SWV.qEl = el; };
const listMount = (el) => { SWV.listEl = el; };

// The file switcher under the title (#src_switch): with the filter empty,
// the call stack's files, the recent ones, then every file; with a filter,
// the best matches; after a breadcrumb, one directory's files.
const SrcSwitch = memo(function SrcSwitch() {
	useController(SWV);
	const open = SWV.open, rows = open ? srcSw.rows : [];
	const items = [];
	let group = null;
	rows.forEach((x, i) => {
		const head = srcSw.dir !== null ? (i === 0 ? "dir" : null) : x.group;
		if (head && head !== group) {
			group = head;
			items.push(<li key={"h" + i} role="presentation" className="srcswhead">{head === "dir" ? (srcSw.dir || "the source tree") + "/" : SRC_SW_GROUPS[head]}</li>);
		}
		items.push(<SwRow key={i} x={x} i={i} active={i === srcSw.at} current={!!x.path && !!srcPanel && x.path === srcPanel.path} />);
	});
	if (open && (srcSw.more || srcSw.none))
		items.push(<li key="note" role="presentation" className="note">{srcSw.none ? (srcSw.q ? "No file matches." : "No file list: " + (dbgFiles().why || SRC.why || "no debug info")) :
			"… and " + srcSw.more.toLocaleString("en") + " more: type to narrow."}</li>);
	return (
		<div id="src_switch" className="srcswitch" hidden={!open} data-dir={open && srcSw.dir !== null ? srcSw.dir : ""}>
			<input id="src_q" ref={qMount} type="search" role="combobox" aria-autocomplete="list" aria-expanded={open} aria-controls="src_list"
				aria-activedescendant={rows.length ? "src_opt_" + srcSw.at : undefined} aria-label="Filter the source files by name or path"
				placeholder="file name or path, e.g. printk" autoComplete="off" spellCheck="false"
				onChange={(e) => srcSwFilter(e.target.value)} onKeyDown={swKey} />
			{" "}
			<ul id="src_list" ref={listMount} role="listbox" aria-label="Source files" onClick={(ev) => {
				const li = ev.target.closest(".srcopt");
				if (li)
					srcSwPick(Number(li.dataset.i));
			}}>{items}</ul>
		</div>
	);
});

// The picked line (a click on a line, or the line a listing's C line
// opened): every address of its code (each inlined copy and each inlined
// call made on it, dbgLineCode) with a jump to it, and "Run to file:line".
// A line that continues a statement shows its first line's.
function SrcAddrs() {
	const pick = srcPanel && srcPanel.ready && srcPanel.pick;
	const v = pick ? srcAddrsView(pick) : null;
	return <div id="src_addrs" className="note srcaddrs" hidden={!v} {...(v ? v.data : {})}>{v ? v.body : null}</div>;
}

function srcAddrsView(pick) {
	const c = srcPanel.cont && srcPanel.cont.get(pick);
	const line = c ? c.start : pick;
	const r = dbgLineCode(srcPanel.path, line);
	const where = (r.path || srcPanel.path) + ":" + line;
	const lead = [<span key="loc" className="srcloc">{where}</span>, c ? " (line " + pick + " continues this statement): " : ": "];
	const data = { "data-state": r.state, "data-line": line, "data-path": r.path || srcPanel.path };
	if (r.state !== "ok")
		return { data: { ...data, "data-bps": "[]", "data-rows": "[]", "data-calls": "[]" },
			body: <div>{lead}{r.state === "loading" ? r.why || "reading DWARF…" : r.why}</div> };
	const outer = (pa) => {
		const f = dbgFrames(pa, true);
		return f.state === "ok" ? f.frames : null;
	};
	const fns = new Set(r.rows.map(([a]) => { const f = outer(a); return f ? f[f.length - 1].func : "?"; }));
	const all = [...r.rows.map((x) => ["row", x]), ...r.calls.map((x) => ["call", x])].sort((x, y) => x[1][0] - y[1][0]);
	const item = (a, b, text, title) => {
		const s = symbols && api.symbols.symbolAt(symbols.symbols, a);
		return (
			<li key={a + ":" + b + ":" + text} title={title}>
				<button type="button" className="symlink" title="Show this code in the Page tab" data-addr={a} onClick={() => api.show.jumpTo(a)}>{hex(a)}</button>
				{" " + (s ? s.name + (a > s.paddr ? "+0x" + (a - s.paddr).toString(16) : "") : "") + " (" + (b - a) + " byte" + (b - a === 1 ? "" : "s") + ")" + text}
			</li>
		);
	};
	return {
		data: { ...data, "data-bps": JSON.stringify(r.bps), "data-rows": JSON.stringify(r.rows.map(([a, b]) => [a, b])),
			"data-calls": JSON.stringify(r.calls.map(([a, b, name]) => [a, b, name])) },
		body: (
			<>
				<div>
					{lead}
					{r.rows.length + " range" + (r.rows.length === 1 ? "" : "s") + " of its own code" +
						(fns.size > 1 ? " in " + fns.size + " functions (inlined copies)" : "") +
						(r.calls.length ? ", " + r.calls.length + " range" + (r.calls.length === 1 ? "" : "s") + " of code inlined by a call on it" : "") + ". "}
					<button id="src_runto" type="button" disabled={!stepper} onClick={() => api.step.runToLine(where)}
						title={!stepper ? "Boot a kernel first" : "Run until the kernel reaches the first of this line's " + r.bps.length +
							" breakpoint address" + (r.bps.length === 1 ? "" : "es") + " (the is_stmt rows of the line table, and where each inlined call starts)" +
							(r.noStmt ? ". This line table has no is_stmt flags: every range's start counts" : "")}>{"Run to " + where}</button>
				</div>
				<ul>
					{all.slice(0, SRC_ADDRS_SHOWN).map(([kind, x]) => {
						if (kind === "row") {
							const [a, b, bp] = x;
							const f = outer(a);
							const into = f && f.length > 1 ? "; inlined into " + (f[f.length - 1].func || "?") : "";
							return item(a, b, into + (bp.length ? "; breakpoint" + (bp.length > 1 ? "s at " + bp.map(hex).join(", ") : bp[0] === a ? " here" : " at " + hex(bp[0])) :
								"; no is_stmt row here"), "Code of " + where + (f ? "\n" + dbgChainText(f) : ""));
						}
						const [a, b, name] = x;
						return item(a, b, "; " + (name || "?") + "() inlined by the call on this line; breakpoint at its start", "Code of " + (name || "?") + ", inlined at " + where);
					})}
					{all.length > SRC_ADDRS_SHOWN ? <li>{"… and " + (all.length - SRC_ADDRS_SHOWN) + " more (Run to sets all " + r.bps.length + " breakpoints)"}</li> : null}
				</ul>
			</>
		),
	};
}

// The editor bar's line: where the caret is, saved or not (drawn at every key).
const EdStateLine = memo(function EdStateLine() {
	useController(EDSTATE);
	return <span id="ed_state" className="note" role="status">{EDSTATE.text}</span>;
});

const filesMount = (el) => { SRCV.filesEl = el; };
// The editor's textarea, srcedit.js's own node (the same array at every render).
const TA_NODES = [ED_TA];

// Files: opens and closes the switcher (drawn with it, on SWV).
const FilesButton = memo(function FilesButton() {
	useController(SWV);
	return (
		<button id="src_files" ref={filesMount} type="button" aria-expanded={SWV.open} aria-controls="src_switch"
			onClick={() => (SWV.open ? srcSwClose() : srcSwOpen())}
			title="Open another file: the call stack's, the recent ones, or any file of the kernel's debug info and source, filtered by name or path (arrows and Enter, Escape closes). A directory of the path in the title lists its files.">
			<span aria-hidden="true">📂</span>
			{" Files"}
		</button>
	);
});
const editMount = (el) => { SRCV.editEl = el; };
const bodyMount = (el) => { SRCV.bodyEl = el; };
const gutterMount = (el) => { SRCV.gutterEl = el; };
const pdiffMount = (el) => { SRCV.pdiffEl = el; };

// Edit: the file shown into the editor and back.
const EditButton = memo(function EditButton() {
	useController(EDBAR);
	const can = edCan();
	return (
		<button id="src_edit" ref={editMount} type="button" aria-pressed={ED.on} disabled={!ED.on && !can.ok} onClick={edToggle}
			title={ED.on ? "Stop editing: back to the text the kernel was built from, with its coverage" :
				can.ok ? "Edit this file. Your edits are kept in this browser (Source tab: the changed files, a patch). " +
					"Tab inserts a tab, Escape leaves the text, Ctrl+S saves at once." : can.why}>
			<span aria-hidden="true">✎</span>
			{" Edit"}
		</button>
	);
});

// The editor bar (a changed file, or editing: the line, Diff, Revert, the
// Rebuild and hot patch buttons), and the panel's marks while editing
// (which view it shows: style.css).
const EdBar = memo(function EdBar({ box }) {
	useController(EDBAR);
	const editing = ED.on && !!ED.cur;
	const e = srcPanel ? edMap().get(srcPanel.path) : null;
	useLayoutEffect(() => {
		box.classList.toggle("editing", editing);
		box.classList.toggle("diffing", editing && ED.diffShown);
	});
	return (
		<div id="ed_bar" className="edbar" hidden={!editing && !e}>
			<EdStateLine />
			{" "}
			<span className="edbtns">
				<button id="ed_diffbtn" type="button" aria-pressed={ED.diffShown} disabled={!e} onClick={edDiffToggle}
					title="This file's changes as a diff, in place of its text (again: back to the text)">
					Diff
				</button>
				{" "}
				<button id="ed_revert" type="button" disabled={!e} onClick={edRevertPanel}
					title="Throw away your edits of this file (press twice)">{ED.armed === "panel" ? "Revert? Press again" : "Revert"}</button>
				{" "}
				<span id="rb_go_slot" className="rbslot"><RebuildPanelButton /></span>
				{" "}
				<span id="hp_go_slot" className="rbslot"><HotPatchButton /></span>
			</span>
		</div>
	);
});

// The panel's element (box, the aside of index.html): shown, and its keys:
// v86 takes every key on the window for the VM while the Machine tab shows
// its screen (preventDefault), so the panel's buttons get Enter and Space
// here, before it; Escape closes the panel.
function useBox(box) {
	useLayoutEffect(() => {
		box.toggleAttribute("hidden", !SRCV.open);
	});
	useLayoutEffect(() => {
		const keys = (ev) => {
			if (ev.defaultPrevented)
				return;
			if ((ev.key === "Enter" || ev.key === " ") && ev.target.tagName === "BUTTON") {
				ev.preventDefault();
				ev.stopPropagation();
				ev.target.click();
			} else if (ev.key === "Escape")
				closeSource();
		};
		box.addEventListener("keydown", keys);
		return () => box.removeEventListener("keydown", keys);
	}, [box]);
}

function SourceAside({ box }) {
	useController(SRCV);
	useBox(box);
	return (
		<>
			<div className="scan srchead">
				<SrcTitle />
				{" "}
				<span className="srcbtns">
					<button id="bm_add_src" type="button" className="bmadd" aria-label="Bookmark the picked line" onClick={() => bmAsk(bmDraftHere("src"))}
						title="Bookmark the picked line (tap a line to pick it), else the line opened, or the whole file; or right-click (long-press) a line, or press B">
						{"☆"}
					</button>
					{" "}
					<FilesButton />
					{" "}
					<EditButton />
					{" "}
					<button id="src_close" type="button" onClick={closeSource}>Close</button>
				</span>
			</div>
			{" "}
			<SrcSwitch />
			{" "}
			<SrcNav />
			{" "}
			<EdBar box={box} />
			{" "}
			<div id="ed_area" className="edarea">
				<pre id="ed_gutter" ref={gutterMount} className="edgutter" aria-hidden="true" />
				<DomNodes nodes={TA_NODES} />
			</div>
			{" "}
			<pre id="ed_pdiff" ref={pdiffMount} className="eddiff edpanel" />
			{" "}
			<p id="src_info" className="note">{srcPanel ? SRCV.info : ""}</p>
			{" "}
			<SrcAddrs />
			{" "}
			<pre id="src_body" ref={bodyMount} className="srcview" tabIndex="0" onClick={srcBodyClick} />
		</>
	);
}

export { SourceAside };
