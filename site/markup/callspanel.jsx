/** @jsxRuntime classic */
/** @jsx h */
/** @jsxFrag Fragment */
// The Calls tab (R3): the call timeline and graph (graph.js, canvases),
// the call tree and the Top 30. The component is the tab's markup and
// renders once; graph.js draws into it by id and starts or stops drawing
// as its tab is shown or left (the store's tabShows).

import { h, Fragment } from "./html.js";

function CallsPanel() {
	return (
		<>
			<div className="scan">
				<span className="tabs">
					<button id="gview_timeline" type="button" aria-pressed="true">Timeline</button>
					{"\n      "}
					<button id="gview_tree" type="button" aria-pressed="false">Call tree</button>
					{"\n      "}
					<button id="gview_top" type="button" aria-pressed="false">Top 30</button>
				</span>
				{" "}
				<label>
					<input type="checkbox" id="graph_fold" defaultChecked />
					{" hide interrupts"}
					<span className="tip" tabIndex="0" role="button" title="Hide the frames of interrupts and exceptions: everything entered through an entry stub (irq_entries_start, asm_*) and what it calls, such as the PIT's IRQ0 timer path. In the call tree they become one (interrupt and exception handling) entry.">
						?
					</span>
				</label>
				{" "}
				<label id="graph_follow_l">
					<input type="checkbox" id="graph_follow" defaultChecked />
					{" follow"}
				</label>
				{" "}
				<button id="graph_fit" type="button">Fit</button>
				{" "}
				<button id="graph_json" type="button">JSON</button>
				{" "}
				<button id="graph_dot" type="button">DOT</button>
				{" "}
				<button id="bm_add_calls" type="button" className="bmadd" title="Bookmark the selected box's function; or right-click (long-press) a box, or point at it and press B">
					{"☆ Bookmark"}
				</button>
				{" "}
				<input id="graph_search" type="search" placeholder="search" aria-label="search the timeline or the call tree" />
				{" "}
				<span id="graph_tl_opts" className="group">
					{"\n      "}
					<button id="graph_transpose" type="button" aria-pressed="false" title="Time downwards, call depth across: better for deep stacks on wide screens">
						Transpose
					</button>
					{"\n      "}
					<button id="graph_flame" type="button" aria-pressed="false" title="Root at the bottom (flame graph) instead of the top (icicle)">Flame</button>
					{"\n      "}
					<label>
						<input type="checkbox" id="graph_chains" defaultChecked />
						{" chains"}
						<span className="tip" tabIndex="0" role="button" title={"Draw a call whose caller has a single call and almost no own instructions in the caller's box: a \\u203a b \\u203a c."}>
							?
						</span>
					</label>
					{"\n      "}
					<label>
						{"depth "}
						<input type="range" id="graph_depth" min="0" max="64" defaultValue="0" aria-label="maximum depth shown, 0 = all" />
						{" "}
						<span id="graph_depth_v">all</span>
					</label>
					{"\n    "}
				</span>
				{" "}
				<span className="tip" tabIndex="0" role="button" title="Calls reconstructed from the symbol transitions the stepper records (stepping, slow motion, Run to, and the background count with ?speed=full; not at full JIT speed): a jump to the first byte of a symbol is a call, a jump into the middle of a symbol on the stack is a return to it, an entry stub starts an interrupt; a jump into the middle of a symbol not on the stack (a tail jump) replaces the top frame; a task switch suspends the calls of the task it leaves, which continue when it runs again; a thin slate-blue line marks each task switch, with the incoming task's name on top (tap it for details): comm and pid read from its task_struct with the offsets from the DWARF (the running task through current_task, the others through the task_struct current_task named when they last switched away, or the task list), else a guess (the boot task, or the task's thread function) and the tooltip says why. With more than one task the call tree has a row per task above its calls. Timeline: x = instructions from the kernel entry, y = call depth, colour = source directory (init, mm, fs, kernel, arch, lib, include), grey = interrupts; calls narrower than a pixel are merged per pixel column (tap one to zoom in); the strip on top is the whole run (drag in it to move); drag to pan, wheel or pinch to zoom; tap a box for its counts; double-click or double-tap a box (or select it and press Enter; the arrow keys move the selection) to open its function in the Page tab at its first instruction and in the Source panel at its line (Back returns here); boxes with no symbol, the merged columns and the empty area do nothing. Call tree: inclusive %, self %, calls, like perf report --children. Top 30: the symbols with the largest inclusive count, call edges between them, layered by depth; a double click opens a symbol as in the timeline. JSON has the transitions and the call tree; DOT the transitions.">
					?
				</span>
			</div>
			{" "}
			<p id="graph_status" className="note" />
			{" "}
			<div id="graph_crumbs" className="crumbs note" hidden />
			{" "}
			<canvas id="graph_canvas" tabIndex="0" aria-label="call chart: double-click a box, or select it and press Enter, to open its function in the Page tab and the Source panel; arrow keys move the selection" />
			{" "}
			<div id="graph_tree" className="ctree" hidden />
			{" "}
			<div id="graph_info" className="syminfo" />
		</>
	);
}

export { CallsPanel };
