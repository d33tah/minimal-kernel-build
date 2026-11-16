/** @jsxRuntime classic */
/** @jsx h */
/** @jsxFrag Fragment */
// The Source panel (R3): the aside beside every tab (#src_panel) with a
// file's text (source.js), its file switcher and outline, and the editor
// (srcedit.js). The component is the markup and renders once; source.js
// and srcedit.js draw into it by id. Its Rebuild and boot button is a
// component on RB (rebuild.jsx), like the Source tab's box.

import { h, Fragment } from "./html.js";

function SourceAside() {
	return (
		<>
			<div className="scan srchead">
				<b id="src_title" />
				{" "}
				<span className="srcbtns">
					<button id="bm_add_src" type="button" className="bmadd" aria-label="Bookmark the picked line" title="Bookmark the picked line (tap a line to pick it), else the line opened, or the whole file; or right-click (long-press) a line, or press B">
						{"☆"}
					</button>
					{" "}
					<button id="src_files" type="button" aria-expanded="false" aria-controls="src_switch" title="Open another file: the call stack's, the recent ones, or any file of the kernel's debug info and source, filtered by name or path (arrows and Enter, Escape closes). A directory of the path in the title lists its files.">
						<span aria-hidden="true">📂</span>
						{" Files"}
					</button>
					{" "}
					<button id="src_edit" type="button" aria-pressed="false">
						<span aria-hidden="true">✎</span>
						{" Edit"}
					</button>
					{" "}
					<button id="src_close" type="button">Close</button>
				</span>
			</div>
			{" "}
			<div id="src_switch" className="srcswitch" hidden>
				<input id="src_q" type="search" role="combobox" aria-autocomplete="list" aria-expanded="false" aria-controls="src_list" aria-label="Filter the source files by name or path" placeholder="file name or path, e.g. printk" autoComplete="off" spellCheck="false" />
				{" "}
				<ul id="src_list" role="listbox" aria-label="Source files" />
			</div>
			{" "}
			<div id="src_nav" className="srcnav" hidden>
				<select id="src_outline" aria-label="Go to a function of this file" />
				{" "}
				<select id="src_incl" aria-label="Open a header whose code is inlined into this file" />
			</div>
			{" "}
			<div id="ed_bar" className="edbar" hidden>
				<span id="ed_state" className="note" role="status" />
				{" "}
				<span className="edbtns">
					<button id="ed_diffbtn" type="button" aria-pressed="false" title="This file's changes as a diff, in place of its text (again: back to the text)">
						Diff
					</button>
					{" "}
					<button id="ed_revert" type="button" title="Throw away your edits of this file (press twice)">Revert</button>
					{" "}
					<span id="rb_go_slot" className="rbslot" />
				</span>
			</div>
			{" "}
			<div id="ed_area" className="edarea">
				<pre id="ed_gutter" className="edgutter" aria-hidden="true" />
				<textarea id="ed_text" className="edtext" wrap="off" spellCheck="false" autoComplete="off" autoCapitalize="off" aria-label="Text of the file" />
			</div>
			{" "}
			<pre id="ed_pdiff" className="eddiff edpanel" />
			{" "}
			<p id="src_info" className="note" />
			{" "}
			<div id="src_addrs" className="note srcaddrs" hidden />
			{" "}
			<pre id="src_body" className="srcview" tabIndex="0" />
		</>
	);
}

export { SourceAside };
