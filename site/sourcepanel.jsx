// The Source tab (A1): the coverage tree (coverage.js), Your edits
// (srcedit.js) with the Rebuild and boot box (RebuildPanel, rebuild.jsx)
// and the hot patch box (HotPatchPanel, hotpatch.jsx). A component on
// coverage.js's COVV (the tree's rows and lines as covRender computed them,
// the lcov line) and srcedit.js's EDV (Your edits, drawn from the overlay),
// mounted when the tab is first shown (shell.jsx). A function's facts under
// its row are the nodes fnfacts.js builds (DomNodes). Element ids and data
// attributes are the ones the tests and the modules read.

import { memo, useRef } from "react";
import { useController, DomNodes, Tip } from "./ui.jsx";
import { COV, COVV, covToggle, covFilter, covSortBy, covCompare, covExport, covPct } from "./coverage.js";
import { ED, EDV, edMap, edStats, edArmed, edRevert, edOpen, edListDiff, edDownload, edUpload, edLineClass,
	srcOverlay } from "./srcedit.js";
import { edFilePatch, edLines } from "./edpatch.js";
import { bmAsk, bmTip } from "./bookmarks.js";
import { RebuildPanel } from "./rebuild.jsx";
import { HotPatchPanel } from "./hotpatch.jsx";

const COV_TIP = "All the kernel's code, by directory, file and function, from its DWARF: for the kernels built here a -g build of the same tree whose code is byte-identical to the kernel that boots, for Debian its -dbg package. Lines run: of the lines with code (the line table's rows; a line of inlined code and the line of its call both count), those where an instruction started while the page stepped the CPU. Instructions run: of the function's instructions (llvm-objdump of the same ELF, in CI), the addresses where an instruction started while stepping. Only what the page steps is recorded, the same record as the tint in the Page listing: code v86's JIT ran (the fast-forward to the kernel entry, full speed) is not. Tap a directory to open it, a file for the whole file in the Source panel with its executed lines (the other lines of a statement written over several lines, e.g. an asm statement, are marked ┆ with the statement's status and are not counted), a function to open it in the Page tab. The totals come with the kernel; which lines ran is read from the line tables of the code that ran, fetched when needed. An imported vmlinux has no tree: its DWARF is only read around the addresses shown.";
const LCOV_TIP = "Download an lcov tracefile (genhtml, IDE coverage plugins): every file's lines with code, DA:line,1 for a line that has executed, 0 for one that has not (ran or not, not how often). For a big kernel this first reads every shard of its line table (Debian: about 5 MB).";
const EDITS_TIP = "Edit any file of this kernel's source: open it in the Source panel (a file below, a C line in a listing, or Files) and press Edit. The edits are kept in this browser per kernel and file (IndexedDB), over the tree the kernel was built from; the listings and the coverage keep showing that tree. Download patch: every changed file as one patch, which `git apply` takes in the kernel's tree (the paths are the DWARF's, relative to the tree). Upload a patch: a unified diff (git diff, diff -u) applied to your edits, every file of it or none; it may make new files or delete them.";
const CMP_TIP = "What ran in an earlier build of this kernel (one rebuilt in this page, or the shipped one), next to what runs in this one: each file and function shows how many of its lines ran there, + the lines that ran only here, − those that ran only there. The earlier build's lines are moved to this build's text through the diff of each edited file (lines below an edit move with it); a line the edit changed counts as not the same line. The Source panel marks ◌ the lines that ran there and not (yet) here. Kept until the page is reloaded.";

// The filter, the sort and the lcov export.
const CovControls = memo(function CovControls() {
	useController(COVV);
	const v = COVV.view;
	return (
		<div className="scan">
			<input id="cov_filter" type="search" placeholder="filter" aria-label="filter files and functions" defaultValue={COV.q}
				onChange={(e) => covFilter(e.target.value)} />
			<Tip text={COV_TIP} />
			{" "}
			<label>
				{"sort "}
				<select id="cov_sort" value={COV.sort} onChange={(e) => covSortBy(e.target.value)}>
					<option value="path">by name</option>
					<option value="run">lines run (%)</option>
					<option value="notrun">lines not run</option>
					<option value="lines">lines with code</option>
					<option value="ins">instructions run (%)</option>
				</select>
			</label>
			{" "}
			<button id="cov_export" type="button" disabled={!v || v.state !== "ok"} onClick={covExport}>lcov</button>
			<Tip text={LCOV_TIP} />
			{" "}
			<span id="cov_export_status" className="note">{COVV.exportStatus}</span>
		</div>
	);
});

// One file's changes as coloured lines.
function EdDiff({ path, e }) {
	return edLines(edFilePatch(path, e.base, e.text).patch).map((l, i) => <span key={i} className={edLineClass(l)}>{l}</span>);
}

// Your edits: the changed files of the shown kernel, each with its diff and
// a revert; the patch download and upload; the Rebuild and hot patch boxes.
const EdBox = memo(function EdBox() {
	useController(EDV);
	const up = useRef(null);
	const m = edMap(), paths = [...m.keys()].sort();
	const where = ED.db ? "kept in this browser" : "kept until a reload: " + (ED.why || "IndexedDB not open yet");
	const diff = ED.listDiff && m.has(ED.listDiff) ? ED.listDiff : null;
	return (
		<div id="ed_box" className="edbox">
			<div className="scan">
				<b>Your edits:</b>
				{" "}
				<span id="ed_count" className="note">{paths.length ? paths.length + " changed file" + (paths.length === 1 ? "" : "s") + ", " + where :
					"none for this kernel (Edit in the Source panel, or upload a patch)" + (ED.db ? "" : "; " + where)}</span>
			</div>
			{" "}
			<div className="scan">
				<button id="ed_download" type="button" disabled={!paths.length} onClick={edDownload}>Download patch</button>
				{" "}
				<button id="ed_upload_btn" type="button" onClick={() => up.current.click()}>Upload a patch</button>
				<input id="ed_upload" ref={up} type="file" accept=".patch,.diff,text/x-diff,text/x-patch,text/plain" hidden onChange={edUpload} />
				{" "}
				<button id="ed_revert_all" type="button" disabled={!paths.length}
					onClick={() => edArmed("*", () => edRevert(srcOverlay.paths()))}>{ED.armed === "*" ? "Revert all? Press again" : "Revert all"}</button>
				<Tip text={EDITS_TIP} />
			</div>
			{" "}
			<p id="ed_status" key={EDV.said} className="note" role="status" data-state={EDV.statusState}>{EDV.status}</p>
			{" "}
			<ul id="ed_list" className="edlist">{paths.map((p) => {
				const e = m.get(p), s = edStats(p);
				return (
					<li key={p} data-path={p} data-add={s.add} data-del={s.del}>
						<button type="button" className="edopen" title={"Open " + p + " in the Source panel's editor"} disabled={e.text === null}
							onClick={() => edOpen(p)}>{p}</button>
						<span className="note">{(e.base === null ? " new," : e.text === null ? " deleted," : "") + " +" + s.add + " -" + s.del + " "}</span>
						<button type="button" className="eddiffbtn" aria-pressed={ED.listDiff === p} onClick={() => edListDiff(p)}>{ED.listDiff === p ? "Hide diff" : "Diff"}</button>
						{" "}
						<button type="button" className="edrevert" title={"Throw away your edits of " + p + " (press twice)"}
							onClick={() => edArmed("file:" + p, () => edRevert([p]))}>{ED.armed === "file:" + p ? "Revert? Press again" : "Revert"}</button>
					</li>
				);
			})}</ul>
			{" "}
			<pre id="ed_diff" className="eddiff" hidden={!diff}>{diff ? <EdDiff path={diff} e={m.get(diff)} /> : null}</pre>
			{" "}
			<RebuildPanel />
			{" "}
			<HotPatchPanel />
		</div>
	);
});

// A count with its bar: run of total, and with a comparison what ran there.
function CovCell({ run, total, what, cmp, cmpLabel }) {
	const p = covPct(run, total);
	let title = !total ? "no " + what : run === null ? total.toLocaleString("en") + " " + what + "; what ran is not known (see below)" :
		run.toLocaleString("en") + " of " + total.toLocaleString("en") + " " + what + " have executed";
	if (cmp)
		title += "; in " + cmpLabel + " " + cmp.was + " ran, " + cmp.plus + " ran only here, " + cmp.minus + " only there";
	return (
		<span className="covcell" title={title}>
			<span className="covbar"><span style={{ width: p === null ? "0" : Math.max(p > 0 ? 2 : 0, Math.round(100 * p)) + "%" }} /></span>
			<span className="mono">{(run === null ? "–" : run.toLocaleString("en")) + "/" + total.toLocaleString("en") +
				(p === null ? "" : " " + (p > 0 && p < 0.01 ? "<1" : Math.floor(100 * p)) + "%")}</span>
			{cmp ? <span className="mono covwas">{"there " + cmp.was.toLocaleString("en") + " +" + cmp.plus + " −" + cmp.minus}</span> : null}
		</span>
	);
}

// A row of the tree: its arrow, its name, its lines and instructions, and
// for a file or function its ★ and ☆ (K1).
function CovRow({ r, cmpLabel }) {
	const { s, bm } = r;
	const toggle = () => covToggle(r.path);
	const data = { "data-kind": r.kind, "data-path": r.path, "data-depth": r.depth, "data-lines": s.lines, "data-lines-run": s.linesRun ?? "",
		"data-ins": s.ins, "data-ins-run": s.insRun ?? "" };
	if (s.cmp)
		Object.assign(data, { "data-was": s.cmp.was, "data-plus": s.cmp.plus, "data-minus": s.cmp.minus });
	if (bm && bm.kind === "fn")
		Object.assign(data, { "data-pa": bm.t.pa, "data-fn": bm.t.name });
	const label = "Bookmark " + (bm ? bm.kind === "fn" ? bm.t.name + "()" : bm.t.path : "");
	return (
		<>
			<div className="trow covrow" {...data}>
				<button type="button" className="ttog" disabled={!r.kids} aria-label={(r.open ? "collapse " : "expand ") + (r.path || "all files")}
					onClick={toggle}>{r.kids ? (r.open ? "▾" : "▸") : "·"}</button>
				<button type="button" className="tname mono covname" style={{ paddingLeft: Math.min(r.depth, 20) * 0.6 + "em" }} title={r.title}
					onClick={r.onName || toggle}>{r.label}</button>
				<CovCell run={s.linesRun} total={s.lines} what="lines with code" cmp={s.cmp} cmpLabel={cmpLabel} />
				<CovCell run={s.insRun} total={s.ins} what="instructions" />
				<span className="covbm">
					{r.stars ? <span className="bmstar tipmark" tabIndex={0} role="button" aria-label={bmTip(r.stars)} data-tip={bmTip(r.stars)}>★</span> : null}
					{bm ? <button type="button" className="bmadd" title={label} aria-label={label}
						onClick={(ev) => { ev.stopPropagation(); bmAsk(bm); }}>☆</button> : null}
				</span>
			</div>
			{r.detail ? <DomNodes nodes={r.detail} /> : null}
		</>
	);
}

// The tree with its status line, the comparison picker (E8) and its notes.
const CovTree = memo(function CovTree() {
	useController(COVV);
	const v = COVV.view;
	const ok = v && v.state === "ok";
	const cmp = ok ? v.cmp : null;
	return (
		<>
			<p id="cov_status" className="note">{v ? v.status : ""}</p>
			{" "}
			<div id="cov_cmp_box" className="scan" hidden={!cmp || !cmp.shown}>
				<label>
					{"compare with "}
					<select id="cov_cmp" value={cmp ? cmp.value : ""} onChange={(e) => covCompare(e.target.value)}>
						{(cmp ? cmp.options : []).map(([val, text]) => <option key={val} value={val}>{text}</option>)}
					</select>
				</label>
				<Tip text={CMP_TIP} />
				{" "}
				<span id="cov_cmp_status" className="note">{cmp ? cmp.status : ""}</span>
			</div>
			{" "}
			<div id="cov_tree" className="ctree covtree" data-state={v ? v.state : undefined} data-rows={ok ? v.rows.length : undefined}
				data-coverage={ok ? v.coverage : undefined}>
				{ok ? <div className="trow thead covrow"><span className="ttog"></span><span className="tname">file or function</span>
					<span className="covcell">lines run</span><span className="covcell">instructions run</span><span className="covbm"></span></div> : null}
				{ok ? v.rows.map((r, i) => <CovRow key={i} r={r} cmpLabel={v.cmpLabel} />) : null}
				{ok && v.more ? <p className="note">{v.more.toLocaleString("en") + " more rows: narrow the filter or close some directories."}</p> : null}
			</div>
			{" "}
			<p id="covtree_info" className="note">{v ? v.info : ""}</p>
		</>
	);
});

function SourceTabPanel() {
	return (
		<>
			<CovControls />
			{" "}
			<EdBox />
			{" "}
			<CovTree />
		</>
	);
}

export { SourceTabPanel };
