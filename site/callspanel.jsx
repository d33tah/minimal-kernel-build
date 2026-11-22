// The Calls tab (A1): the call timeline and Top 30 (graph.js, calltree.js:
// one canvas), the call tree and the controls. A component on graphdata.js's
// G and GV, graph.js's GSTAT and GINFO and calltree.js's GTREE (ui.jsx's
// controller()), mounted when the tab is first shown (shell.jsx). The
// canvas is drawn by graph.js and calltree.js (wireGraphCanvas gives it its
// pointer and keys); the rest is drawn here. Element ids and data
// attributes are the ones the tests and the modules read.

import { memo, useLayoutEffect, useRef } from "react";
import { api } from "./page.js";
import { useController, DomNodes, Tip } from "./ui.jsx";
import { G, GV } from "./graphdata.js";
import { GSTAT, GINFO, focusFrame } from "./graph.js";
import { GTREE, treeToggle, treeNameTitle } from "./calltree.js";
import { wireGraphCanvas, graphView, graphFit, graphToggle, graphChains, graphDepth, graphFollow, graphSearch, graphFold, graphJson,
	graphDotFile } from "./graphinit.js";
import { bmAsk } from "./bookmarks.js";
import { bmDraftHere } from "./bmdata.js";

const CALLS_TIP = "Calls reconstructed from the symbol transitions the stepper records (stepping, slow motion, Run to, and the " +
	"background count with ?speed=full; not at full JIT speed): a jump to the first byte of a symbol is a call, a jump into the middle of " +
	"a symbol on the stack is a return to it, an entry stub starts an interrupt; a jump into the middle of a symbol not on the stack (a " +
	"tail jump) replaces the top frame; a task switch suspends the calls of the task it leaves, which continue when it runs again; a thin " +
	"slate-blue line marks each task switch, with the incoming task's name on top (tap it for details): comm and pid read from its " +
	"task_struct with the offsets from the DWARF (the running task through current_task, the others through the task_struct " +
	"current_task named when they last switched away, or the task list), else a guess (the boot task, or the task's thread function) " +
	"and the tooltip says why. With more than one task the call tree has a row per task above its calls. Timeline: x = instructions " +
	"from the kernel entry, y = call depth, colour = source directory (init, mm, fs, kernel, arch, lib, include), grey = interrupts; " +
	"calls narrower than a pixel are merged per pixel column (tap one to zoom in); the strip on top is the whole run (drag in it to " +
	"move); drag to pan, wheel or pinch to zoom; tap a box for its counts; double-click or double-tap a box (or select it and press " +
	"Enter; the arrow keys move the selection) to open its function in the Page tab at its first instruction and in the Source panel " +
	"at its line (Back returns here); boxes with no symbol, the merged columns and the empty area do nothing. Call tree: inclusive %, " +
	"self %, calls, like perf report --children. Top 30: the symbols with the largest inclusive count, call edges between them, " +
	"layered by depth; a double click opens a symbol as in the timeline. JSON has the transitions and the call tree; DOT the transitions.";
const FOLD_TIP = "Hide the frames of interrupts and exceptions: everything entered through an entry stub (irq_entries_start, asm_*) and " +
	"what it calls, such as the PIT's IRQ0 timer path. In the call tree they become one (interrupt and exception handling) entry.";

// The status line: calls, symbols and transitions recorded.
const GraphStatus = memo(function GraphStatus() {
	useController(GSTAT);
	const s = GSTAT;
	return <p id="graph_status" className="note" data-nodes={s.nodes} data-edges={s.edges} data-frames={s.frames}>{s.text}</p>;
});

// From the root to the focused frame; each entry focuses there.
function Crumbs({ c }) {
	return (
		<div id="graph_crumbs" className="crumbs note" hidden={!c || !c.on} data-key={c ? c.key : undefined}>
			{c && c.on && <>
				<button type="button" className="symlink" onClick={() => focusFrame(-1)}>root</button>
				{c.items.map(([k, name]) => <span key={k}>{" › "}<button type="button" className="symlink"
					onClick={() => focusFrame(k)}>{name}</button></span>)}
			</>}
		</div>
	);
}

// The canvas: graph.js and calltree.js draw it, wireGraphCanvas gives it
// its pointer and keys.
const GraphCanvas = memo(function GraphCanvas({ hidden }) {
	const ref = useRef(null);
	useLayoutEffect(() => wireGraphCanvas(ref.current), []);
	return <canvas id="graph_canvas" ref={ref} tabIndex={0} hidden={hidden} aria-label={"call chart: double-click a box, or select it " +
		"and press Enter, to open its function in the Page tab and the Source panel; arrow keys move the selection"} />;
});

// The call tree, like perf report --children.
const CallTree = memo(function CallTree({ hidden }) {
	useController(GTREE);
	const T = GTREE;
	return (
		<div id="graph_tree" className="ctree" hidden={hidden} data-root-incl={T.rows ? T.rootIncl : undefined}>
			{T.rows && <div className="trow thead"><span className="ttog" /><span className="tnum mono">{"  incl   self calls"}</span>
				<span className="tname">symbol (call path)</span></div>}
			{T.rows && T.rows.map((r) =>
				<div key={r.key} className="trow" data-name={r.name} data-incl={r.incl} data-task={r.task}>
					<button type="button" className="ttog" disabled={!r.kids} onClick={() => treeToggle(r.key)}>
						{r.kids ? (r.open ? "▾" : "▸") : "·"}</button>
					<span className="tnum mono" title={r.title}>{r.nums}</span>
					<span className="tname mono" style={{ paddingLeft: Math.min(r.depth, 30) * 0.7 + "em" }}
						title={r.task !== undefined ? "Task " + r.task + ": " + r.name.replace(/^task \d+: /, "") + "\n" + r.how : undefined}
						onClick={r.node >= 0 ? () => api.show.jumpTo(G.stepper.symStart[r.node]) : undefined}
						onMouseEnter={r.node >= 0 ? (ev) => { ev.currentTarget.title = treeNameTitle(r.node, r.name); } : undefined}>
						{r.name}</span>
				</div>)}
			{T.more && "(more rows not shown: search to narrow)"}
		</div>
	);
});

// The selected box: counts, where it was called from, callers and callees.
const GraphInfo = memo(function GraphInfo() {
	useController(GINFO);
	const I = GINFO, m = I.m;
	return (
		<div id="graph_info" className="syminfo" data-node={I.node} data-switch={I.sw} data-from={I.from} data-callers={I.callers}
			data-callees={I.callees}>
			{m && !m.name && m.text}
			{m && m.name && <>
				<div>
					<b>{m.name}</b>{m.text || " "}
					{m.focus >= 0 && <><button id="graph_focus" type="button" title="Re-root the timeline at this call"
						onClick={() => focusFrame(m.focus)}>Focus</button>{" "}</>}
					{m.open >= 0 && <><button type="button" onClick={() => api.show.jumpTo(m.open)}>Page tab</button>{" "}
						<span className="note" id="graph_open_how">{"Double-click the box (double tap, or Enter) to open " + m.name +
							" in the Page tab and the Source panel."}</span></>}
				</div>
				{m.fn && <DomNodes nodes={m.fn} />}
				{m.src && <div className="glist" id="graph_src">
					{m.src.map((s) => <span key={s.label}><b>{s.label}</b>{" "}
						<span className="mono">{s.nodes ? <DomNodes nodes={s.nodes} /> : "unknown"}</span>{" "}</span>)}
				</div>}
				{m.lists && m.lists.map((l) => <div key={l.title} className="glist"><b>{l.title + " "}</b>
					{l.items.map((t, i) => <span key={i} className="mono">{t}</span>)}
					{!l.items.length ? "none" : l.more ? "and " + l.more + " more" : null}
				</div>)}
			</>}
		</div>
	);
});

// The depth slider: its own input listener (an input event a script sends
// does not bubble to React's root).
const DepthRange = memo(function DepthRange() {
	const ref = useRef(null);
	useLayoutEffect(() => {
		const el = ref.current, on = () => graphDepth(Number(el.value));
		el.addEventListener("input", on);
		return () => el.removeEventListener("input", on);
	}, []);
	return <input type="range" id="graph_depth" ref={ref} min="0" max="64" defaultValue={G.maxDepth} aria-label="maximum depth shown, 0 = all" />;
});

function CallsPanel() {
	useController(GV);
	const v = G.view;
	return (
		<>
			<div className="scan">
				<span className="tabs">
					<button id="gview_timeline" type="button" aria-pressed={v === "timeline"} onClick={() => graphView("timeline")}>Timeline</button>
					{"\n      "}
					<button id="gview_tree" type="button" aria-pressed={v === "tree"} onClick={() => graphView("tree")}>Call tree</button>
					{"\n      "}
					<button id="gview_top" type="button" aria-pressed={v === "top"} onClick={() => graphView("top")}>Top 30</button>
				</span>
				{" "}
				<label>
					<input type="checkbox" id="graph_fold" checked={G.fold} onChange={(ev) => graphFold(ev.target.checked)} />
					{" hide interrupts"}
					<Tip text={FOLD_TIP} />
				</label>
				{" "}
				<label id="graph_follow_l" hidden={v !== "timeline"}>
					<input type="checkbox" id="graph_follow" checked={G.follow} onChange={(ev) => graphFollow(ev.target.checked)} />
					{" follow"}
				</label>
				{" "}
				<button id="graph_fit" type="button" onClick={graphFit}>Fit</button>
				{" "}
				<button id="graph_json" type="button" onClick={graphJson}>JSON</button>
				{" "}
				<button id="graph_dot" type="button" onClick={graphDotFile}>DOT</button>
				{" "}
				<button id="bm_add_calls" type="button" className="bmadd" title="Bookmark the selected box's function; or right-click (long-press) a box, or point at it and press B"
					onClick={() => bmAsk(bmDraftHere("calls"))}>
					{"☆ Bookmark"}
				</button>
				{" "}
				<input id="graph_search" type="search" placeholder="search" aria-label="search the timeline or the call tree" hidden={v === "top"}
					value={G.q} onChange={(ev) => graphSearch(ev.target.value)} />
				{" "}
				<span id="graph_tl_opts" className="group" hidden={v !== "timeline"}>
					{"\n      "}
					<button id="graph_transpose" type="button" aria-pressed={G.transpose} onClick={() => graphToggle("transpose")}
						title="Time downwards, call depth across: better for deep stacks on wide screens">
						Transpose
					</button>
					{"\n      "}
					<button id="graph_flame" type="button" aria-pressed={G.flame} onClick={() => graphToggle("flame")}
						title="Root at the bottom (flame graph) instead of the top (icicle)">Flame</button>
					{"\n      "}
					<label>
						<input type="checkbox" id="graph_chains" checked={G.chains} onChange={(ev) => graphChains(ev.target.checked)} />
						{" chains"}
						<Tip text={"Draw a call whose caller has a single call and almost no own instructions in the caller's box: a › b › c."} />
					</label>
					{"\n      "}
					<label>
						{"depth "}
						<DepthRange />
						{" "}
						<span id="graph_depth_v">{G.maxDepth ? String(G.maxDepth) : "all"}</span>
					</label>
					{"\n    "}
				</span>
				{" "}
				<Tip text={CALLS_TIP} />
			</div>
			{" "}
			<GraphStatus />
			{" "}
			<Crumbs c={GV.crumbs} />
			{" "}
			<GraphCanvas hidden={v === "tree"} />
			{" "}
			<CallTree hidden={v !== "tree"} />
			{" "}
			<GraphInfo />
		</>
	);
}

export { CallsPanel };
