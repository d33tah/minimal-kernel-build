"use strict";

// Moving around the Calls tab: opening a box's function, the selected
// node, what is under the pointer, the summary and the sandwich view.

import { realNow } from "./clock.js";
import { $ } from "./util.js";
import { api } from "./page.js";
import { G, runStart, recTo, fEnd } from "./graphdata.js";

// Open a box's function in the code browser (G1): the Page tab at its
// first instruction and the Source panel at its line (symview.js openFunction,
// one history entry: Back returns here as it was). Boxes with no symbol
// (BIOS, setup, kernel code without a symbol) do nothing: false.
// One double click or tap can ask twice (the canvas's dblclick and the
// pointerup double-press detection's timer). An open cancels that timer,
// and the 600 ms in which a second ask is ignored count from the end of
// the open: on a slow host the open itself takes longer than that (B2).
function graphOpen(node) {
	const st = G.stepper;
	if (G.openTimer) {
		clearTimeout(G.openTimer);
		G.openTimer = 0;
	}
	if (!st || node < 0 || node >= st.symName.length || realNow() - (G.openedAt || 0) < 600)
		return false;
	G.openedAt = realNow();
	// The press that opened is followed by its click (and dblclick) events,
	// which would land on whatever the Page tab shows under the pointer.
	const swallow = (ev) => {
		if (realNow() - G.openedAt < 500) {
			ev.stopPropagation();
			ev.preventDefault();
		}
	};
	for (const t of ["click", "dblclick"])
		document.addEventListener(t, swallow, true);
	api.show.openFunction(st.symStart[node], st.symName[node]);
	G.openedAt = realNow();
	setTimeout(() => {
		for (const t of ["click", "dblclick"])
			document.removeEventListener(t, swallow, true);
	}, 500);
	return true;
}

// The function node of the selected box (timeline frame or Top 30 node).
const selectedNode = () => (G.selected < 0 || !G.stepper ? -1 : G.view === "timeline" ? G.stepper.fr.node(G.selected) : G.view === "top" ? G.selected : -1);

// The view as Back must restore it (nav.js navCapture/navRestore).
function graphCapture() {
	return { st: G.stepper, view: G.view, x0: G.x0, x1: G.x1, yOff: G.yOff, xOff: G.xOff, userMoved: G.userMoved,
		follow: G.follow, selected: G.selected, selSwitch: G.selSwitch, focus: G.focus, flame: G.flame, transpose: G.transpose };
}

// "start_kernel" for the history's labels: the selected box's function.
function graphSelName(v) {
	const st = v && v.st;
	if (!st || v.selected < 0)
		return "";
	return st.nodeName(v.view === "timeline" ? st.fr.node(v.selected) ?? -1 : v.selected);
}

// The function node of the box at client point (x, y), or -1 (a bookmark's target, K1).
function graphNodeAt(x, y) {
	const st = G.stepper, r = $("graph_canvas").getBoundingClientRect();
	const px = x - r.left, py = y - r.top;
	let node = -1;
	if (st && G.view === "timeline") {
		const k = timelineFrameAt(px, py);
		node = k >= 0 ? st.fr.node(k) : -1;
	} else if (st && G.view === "top") {
		const hit = G.boxes.find(([, bx, by, bw, bh]) => px >= bx && px <= bx + bw && py >= by && py <= by + bh);
		node = hit ? hit[0] : -1;
	}
	return node >= 0 && node < st.symName.length ? node : -1;
}

function timelineFrameAt(px, py) {
	for (let i = G.rects.length - 1; i >= 0; i--) {
		const [k, x, y, w, h] = G.rects[i];
		if (px >= x && px <= x + w && py >= y && py <= y + h)
			return k;
	}
	return -1;
}

// Summary for tests: frames at depth <= 3 by inclusive count, the run
// from the entry, and the instructions inside depth-0 frames (code with no
// symbol, such as the relocation stub, has no frame of its own).
function timelineSummary() {
	const st = G.stepper;
	if (!st)
		return null;
	const start = runStart();
	let rootSum = 0;
	const big = [];
	for (let k = 0; k < st.fr.n; k++) {
		if (st.fr.start(k) < start && fEnd(st, k) <= start)
			continue;
		const incl = fEnd(st, k) - Math.max(st.fr.start(k), start);
		if (st.fr.parent(k) < 0)
			rootSum += incl;
		if (st.fr.depth(k) <= 3)
			big.push({ name: st.nodeName(st.fr.node(k)), depth: st.fr.depth(k), incl, irq: !!st.fr.irq(k) });
	}
	big.sort((a, b) => b.incl - a.incl);
	const d = Float64Array.from({ length: st.fr.n }, (_, k) => st.fr.depth(k)).sort();
	return { total: recTo(st) - start, rootSum, frames: st.fr.n, top: big.slice(0, 25),
		maxDepth: d.length ? d[d.length - 1] : 0, p99Depth: d.length ? d[Math.floor(d.length * 0.99)] : 0 };
}

// Callers and callees of the selected frame's function, aggregated over
// all its frames (the "sandwich").
function sandwich(st, node) {
	const callers = new Map(), callees = new Map();
	let cont = 0;
	const add = (m, id, incl) => {
		const v = m.get(id) || { count: 0, incl: 0 };
		v.count += cont ? 0 : 1;
		v.incl += incl;
		m.set(id, v);
	};
	for (let k = 0; k < st.fr.n; k++) {
		const p = st.fr.parent(k);
		if (p < 0)
			continue;
		const incl = fEnd(st, k) - st.fr.start(k);
		cont = st.fr.cont(k);
		if (st.fr.node(k) === node && st.fr.node(p) !== node)
			add(callers, st.fr.node(p), incl);
		if (st.fr.node(p) === node && st.fr.node(k) !== node)
			add(callees, st.fr.node(k), incl);
	}
	const sorted = (m) => [...m].sort((a, b) => b[1].incl - a[1].incl);
	return { callers: sorted(callers), callees: sorted(callees) };
}

export { graphOpen, selectedNode, graphCapture, graphSelName, graphNodeAt, timelineFrameAt, timelineSummary,
	sandwich };
