/** @jsxRuntime classic */
/** @jsx h */
/** @jsxFrag Fragment */
// The Source tab (R3): the coverage tree (coverage.js) and Your edits
// (srcedit.js) with the Rebuild and boot box (RebuildPanel, rebuild.jsx).
// The component is the tab's markup and renders once (RebuildPanel renders
// on RB); coverage.js and srcedit.js draw into it by id, and coverage.js
// draws its tree when the tab is shown (the store's tabShows).

import { h, Fragment } from "./html.js";

function SourceTabPanel() {
	return (
		<>
			<div className="scan">
				<input id="cov_filter" type="search" placeholder="filter" aria-label="filter files and functions" />
				<span className="tip" tabIndex="0" role="button" title="All the kernel's code, by directory, file and function, from its DWARF: for the kernels built here a -g build of the same tree whose code is byte-identical to the kernel that boots, for Debian its -dbg package. Lines run: of the lines with code (the line table's rows; a line of inlined code and the line of its call both count), those where an instruction started while the page stepped the CPU. Instructions run: of the function's instructions (llvm-objdump of the same ELF, in CI), the addresses where an instruction started while stepping. Only what the page steps is recorded, the same record as the tint in the Page listing: code v86's JIT ran (the fast-forward to the kernel entry, full speed) is not. Tap a directory to open it, a file for the whole file in the Source panel with its executed lines (the other lines of a statement written over several lines, e.g. an asm statement, are marked ┆ with the statement's status and are not counted), a function to open it in the Page tab. The totals come with the kernel; which lines ran is read from the line tables of the code that ran, fetched when needed. An imported vmlinux has no tree: its DWARF is only read around the addresses shown.">
					?
				</span>
				{" "}
				<label>
					{"sort "}
					<select id="cov_sort">
						<option value="path">by name</option>
						<option value="run">lines run (%)</option>
						<option value="notrun">lines not run</option>
						<option value="lines">lines with code</option>
						<option value="ins">instructions run (%)</option>
					</select>
				</label>
				{" "}
				<button id="cov_export" type="button">lcov</button>
				<span className="tip" tabIndex="0" role="button" title="Download an lcov tracefile (genhtml, IDE coverage plugins): every file's lines with code, DA:line,1 for a line that has executed, 0 for one that has not (ran or not, not how often). For a big kernel this first reads every shard of its line table (Debian: about 5 MB).">
					?
				</span>
				{" "}
				<span id="cov_export_status" className="note" />
			</div>
			{" "}
			<div id="ed_box" className="edbox">
				<div className="scan">
					<b>Your edits:</b>
					{" "}
					<span id="ed_count" className="note" />
				</div>
				{" "}
				<div className="scan">
					<button id="ed_download" type="button" disabled>Download patch</button>
					{" "}
					<button id="ed_upload_btn" type="button">Upload a patch</button>
					<input id="ed_upload" type="file" accept=".patch,.diff,text/x-diff,text/x-patch,text/plain" hidden />
					{" "}
					<button id="ed_revert_all" type="button" disabled>Revert all</button>
					<span className="tip" tabIndex="0" role="button" title="Edit any file of this kernel's source: open it in the Source panel (a file below, a C line in a listing, or Files) and press Edit. The edits are kept in this browser per kernel and file (IndexedDB), over the tree the kernel was built from; the listings and the coverage keep showing that tree. Download patch: every changed file as one patch, which `git apply` takes in the kernel's tree (the paths are the DWARF's, relative to the tree). Upload a patch: a unified diff (git diff, diff -u) applied to your edits, every file of it or none; it may make new files or delete them.">
						?
					</span>
				</div>
				{" "}
				<p id="ed_status" className="note" role="status" />
				{" "}
				<ul id="ed_list" className="edlist" />
				{" "}
				<pre id="ed_diff" className="eddiff" hidden />
				{" "}
				<div id="rb_box" className="rbbox" />
				{" "}
				<div id="hp_box" className="rbbox" />
			</div>
			{" "}
			<p id="cov_status" className="note" />
			{" "}
			<div id="cov_cmp_box" className="scan" hidden>
				<label>
					{"compare with "}
					<select id="cov_cmp" />
				</label>
				<span className="tip" tabIndex="0" role="button" title="What ran in an earlier build of this kernel (one rebuilt in this page, or the shipped one), next to what runs in this one: each file and function shows how many of its lines ran there, + the lines that ran only here, − those that ran only there. The earlier build's lines are moved to this build's text through the diff of each edited file (lines below an edit move with it); a line the edit changed counts as not the same line. The Source panel marks ◌ the lines that ran there and not (yet) here. Kept until the page is reloaded.">
					?
				</span>
				{" "}
				<span id="cov_cmp_status" className="note" />
			</div>
			{" "}
			<div id="cov_tree" className="ctree covtree" />
			{" "}
			<p id="covtree_info" className="note" />
		</>
	);
}

export { SourceTabPanel };
