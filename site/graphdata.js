"use strict";

// The Calls tab's state (G), colours, and what its views read from the
// stepper's frames: run bounds, subsystems, counts, the call tree, tasks.

import { runState } from "./page.js";
import { frStartAt, frEndAt, frSmallAt } from "./frames.js";
import { STEP_CONST } from "./stepper.js";
import { DBG, dbgFrames, dbgHas, dbgTypes } from "./dbginfo.js";
import { varsTasks } from "./vartasks.js";

// Private consts (the layout loops read them per frame; see stepper.js).
const { FR_SIZE, FR_IRQ, FR_CONT } = STEP_CONST;

const G = {
	stepper: null,
	view: "timeline",
	fold: true,
	follow: true,
	x0: 0, x1: 1,         // timeline window in steps
	yOff: 0,              // timeline vertical scroll in px
	userMoved: false,
	selected: -1,         // frame index (timeline) or symbol node (top N)
	version: -1,
	dirty: true,
	timer: null,
	pointers: new Map(),
	press: null,
	boxes: [],            // hit boxes of the last top-N drawing
	treeOpen: new Set(),  // expanded call-tree paths
	transpose: false,     // timeline: time downwards, depth across
	flame: false,         // timeline: root at the bottom (flame) or top (icicle)
	chains: true,         // timeline: single-child chains as one box
	maxDepth: 0,          // timeline: rows shown below the root, 0 = all
	focus: -1,            // timeline: frame the view is re-rooted at
	xOff: 0,              // transposed: horizontal scroll in px
	rects: [],            // timeline hit boxes of the last drawing
	layout: null,
};
const ROW = 17, MINI = 26;
const SWITCH = "#6a5acd"; // task switch lines: quiet slate blue
const IRQ_COLOUR = "#b0b0b0"; // frames of interrupt and exception handlers
const PALETTE = { init: "#2ca02c", mm: "#1f77b4", fs: "#ff7f0e", kernel: "#d62728", arch: "#9467bd", lib: "#8c564b",
	drivers: "#17becf", include: "#bcbd22", other: "#7f7f7f" };

// ---- data -------------------------------------------------------------------

// The run starts at the kernel entry once it has been reached.
const runStart = () => (G.stepper && G.stepper.sinceEntry >= 0 ? G.stepper.sinceEntry : 0);
const fEnd = (st, k) => { const e = st.fr.end(k); return e < 0 ? recTo(st) : e; };
// The count the recordings go to: after stepping back (R2) the stepper
// is behind them, and the timeline still ends there, with now marked.
const recTo = (st) => (st.behind && st.behind() ? st.ahead : st.steps);
const hiddenFrame = (st, k) => G.fold && st.fr.irq(k);

// Subsystem of a symbol: the top directory of the source file its first
// instruction comes from (DWARF, only where already loaded: colouring
// never fetches; a small kernel's shards all load up front), else a guess
// from the name.
function subsystem(st, node) {
	if (!G.subsys || G.subsysVersion !== DBG.version) {
		G.subsys = new Map();
		G.subsysVersion = DBG.version;
	}
	let dir = G.subsys.get(node);
	if (dir === undefined) {
		dir = null;
		const r = node < st.symStart.length ? dbgFrames(st.symStart[node], true) : null;
		const f = r && r.state === "ok" ? r.frames[r.frames.length - 1] : null;
		if (f && f.file && !f.file.startsWith("/"))
			dir = f.file.split("/")[0];
		G.subsys.set(node, dir);
	}
	const name = st.nodeName(node);
	if (dir)
		return PALETTE[dir] ? dir : "other";
	if (/^(kmem|kmalloc|alloc|free|page|slab|memblock|mm_|vm|pgd|pte|pmd)/.test(name))
		return "mm";
	if (/^(vfs|do_sys|path|dentry|inode|file|mount|d_)/.test(name))
		return "fs";
	if (/^(native|x86|cpu|apic|idt|tsc|setup_)/.test(name))
		return "arch";
	return "kernel";
}

function frameColour(st, k) {
	return st.fr.irq(k) ? IRQ_COLOUR : PALETTE[subsystem(st, st.fr.node(k))];
}

// Inclusive / self counts of a frame (open frames up to now).
function frameCounts(st, k) {
	const incl = fEnd(st, k) - st.fr.start(k);
	let child = st.fr.child(k);
	// Open children are not in fChild yet.
	for (const o of st.allOpen())
		if (st.fr.parent(o) === k)
			child += recTo(st) - st.fr.start(o);
	return { incl, self: incl - child };
}

// Call tree aggregated by call path (perf report --children), from the
// frames that start at or after the kernel entry. With more than one task
// the outermost calls sit under a node per task (like perf's --sort comm).
function buildTree(st) {
	const start = runStart(), fr = st.fr, now = recTo(st);
	const root = { key: "0", name: "(kernel, from the entry at 0x100000)", incl: now - start, self: 0, calls: 1, kids: new Map() };
	// frame -> its tree node, an index into nodes (-1: none)
	const nodeOf = new Int32Array(fr.n).fill(-1), nodes = [];
	const tasks = st.taskCount > 1 ? taskInfo(st) : null;
	const taskNode = (t) => {
		let x = root.kids.get("@" + t);
		if (!x) {
			x = { key: "0/@" + t, name: "task " + t + ": " + tasks[t].name, task: t, how: tasks[t].how, incl: 0, self: 0, calls: 1, kids: new Map() };
			root.kids.set("@" + t, x);
		}
		return x;
	};
	for (let c = 0, chunks = fr.chunkCount(); c < chunks; c++) {
		const base = fr.baseOf(c), ds = fr.startsRaw(c), de = fr.endsRaw(c);
		const par = fr.parentsOf(c), sym = fr.nodesOf(c), fl = fr.flagsOf(c);
		for (let i = 0, k = c * FR_SIZE, len = fr.chunkLen(c); i < len; i++, k++) {
			const s = frStartAt(fr, base, ds, i, k);
			if (s < start)
				continue;
			const p = par[i], pt = p >= 0 ? nodeOf[p] : -1;
			const top = pt < 0;
			let parent = top ? (tasks ? taskNode(fr.task(k)) : root) : nodes[pt];
			let name = st.nodeName(sym[i]);
			if (G.fold && (fl[i] & FR_IRQ)) {
				if (!top && fr.irqOf(p)) { // inside a folded interrupt: count in it
					nodeOf[k] = pt;
					continue;
				}
				name = "(interrupt and exception handling)";
			}
			let t = parent.kids.get(name);
			if (!t) {
				t = { key: parent.key + "/" + name, name, node: sym[i], incl: 0, self: 0, calls: 0, kids: new Map(), id: nodes.length };
				nodes.push(t);
				parent.kids.set(name, t);
			}
			// Inclusive count: open frames up to now (frameCounts).
			const e = frEndAt(fr, de, i, k, s), incl = (e < 0 ? now : e) - s;
			t.incl += incl;
			if (top && tasks)
				parent.incl += incl;
			if (!(fl[i] & FR_CONT)) // a continuation is the same call, resumed
				t.calls++;
			nodeOf[k] = t.id;
		}
	}
	// self = incl - sum of children's incl
	const fix = (t) => {
		let kids = 0;
		for (const c of t.kids.values()) {
			fix(c);
			kids += c.incl;
		}
		t.self = Math.max(0, t.incl - kids);
	};
	fix(root);
	return root;
}


// Per task { name, how, comm?, pid?, va? }: "comm (pid N)" from its
// task_struct where vartasks.js varsTasks can read it, else a guess (taskGuesses).
function taskInfo(st) {
	const types = dbgHas() ? dbgTypes().state : "none";
	const key = st.fr.n + ":" + st.steps + ":" + st.taskCount + ":" + types + ":" + runState;
	if (G.taskInfo && G.taskInfo.key === key)
		return G.taskInfo.list;
	const guess = taskGuesses(st);
	const list = varsTasks(st).map((k, t) => k.why ? { name: guess[t], guess: true,
		how: (t ? "named after its thread function (the first function it ran)" : "the boot task") +
			"; its task_struct was not read: " + k.why } :
		{ name: k.comm + " (pid " + k.pid + ")", how: "comm and pid from " + k.how, comm: k.comm, pid: k.pid, va: k.va });
	G.taskInfo = { key, list };
	return list;
}

function taskNames(st) {
	return taskInfo(st).map((x) => x.name);
}

// Task names without task_struct: the boot task, then each task's thread function.
function taskGuesses(st) {
	const n = st.fr.n;
	if (G.taskNames && G.taskNames.n === n)
		return G.taskNames.names;
	const names = new Array(st.taskCount).fill("");
	names[0] = "swapper (boot task)";
	const skip = /^(ret_from_fork.*|kthread|__x86_indirect_thunk.*|__x86_return_thunk|__switch_to.*|schedule_tail.*|finish_task_switch.*|startup_32.*|i386_start_kernel|start_kernel)$/;
	// The thread function: called (maybe through a retpoline thunk) by
	// kthread or ret_from_fork, with only fork/kthread code above it.
	const thunk = /^__x86_indirect_thunk/;
	const fallback = new Array(st.taskCount).fill("");
	const setup = /^(sched_setscheduler.*|housekeeping_.*|set_cpus_allowed.*|complete|schedule.*|preempt_.*|__kthread_parkme|_raw_spin.*|kthread_.*|__set_current.*)$/;
	const fr = st.fr;
	for (let c = 0, chunks = fr.chunkCount(); c < chunks; c++) {
		const dt = fr.tasksRaw(c), dd = fr.depthsRaw(c), fl = fr.flagsOf(c), nodes = fr.nodesOf(c);
		for (let i = 0, k = c * FR_SIZE, len = fr.chunkLen(c); i < len; i++, k++) {
			const t = frSmallAt(fr, dt, i, k, "task");
			if (names[t] || frSmallAt(fr, dd, i, k, "depth") > 3 || (fl[i] & (FR_CONT | FR_IRQ)) || skip.test(st.nodeName(nodes[i])))
				continue;
			let p = st.fr.parentOf(k), viaThunk = false;
			while (p >= 0 && thunk.test(st.nodeName(st.fr.nodeOf(p)))) {
				p = st.fr.parentOf(p);
				viaThunk = true;
			}
			// kthread calls the thread function through a function pointer
			// (a retpoline thunk on Debian), after setup calls of its own.
			const pn = p >= 0 ? st.nodeName(st.fr.nodeOf(p)) : "";
			if (/^ret_from_fork/.test(pn) || (pn === "kthread" && viaThunk))
				names[t] = st.nodeName(nodes[i]);
			else if (pn === "kthread" && !setup.test(st.nodeName(nodes[i])))
				fallback[t] = st.nodeName(nodes[i]); // without retpolines: kthread's last other call
		}
	}
	for (let t = 1; t < names.length; t++)
		names[t] = names[t] || fallback[t];
	for (let t = 1; t < names.length; t++)
		names[t] = names[t] || "task " + t;
	G.taskNames = { n, names };
	return names;
}

export { G, ROW, MINI, SWITCH, IRQ_COLOUR, PALETTE, runStart, fEnd, recTo, hiddenFrame, subsystem, frameColour,
	frameCounts, buildTree, taskInfo, taskNames, taskGuesses };
