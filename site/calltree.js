"use strict";

// The Calls tab's call tree and its top-N graph.

import { $, hex } from "./util.js";
import { api } from "./page.js";
import { frStartAt, frEndAt, frSmallAt, FramePath } from "./frames.js";
import { STEP_CONST } from "./stepper.js";
import { dbgTitle } from "./dbginfo.js";
import { G, buildTree, runStart, recTo, PALETTE, subsystem } from "./graphdata.js";
import { canvasSize, fitLabel } from "./graphlayout.js";
import { controller } from "./ui.jsx";

// Private consts (the layout loops read them per frame; see stepper.js).
const { FR_SIZE, FR_IRQ, FR_CONT } = STEP_CONST;

// ---- call tree ------------------------------------------------------------

// The call tree's rows (callspanel.jsx's CallTree draws GTREE): each
// { key, name, incl, depth, kids (has callees), open, nums, title, task,
// how, node }; more: rows not drawn; rootIncl: the root's count.
const GTREE = controller({ rows: null, more: false, rootIncl: undefined });

function drawTree() {
	const st = G.stepper, T = GTREE;
	if (!st) {
		Object.assign(T, { rows: null, more: false });
		T.changed();
		return;
	}
	const root = buildTree(st);
	T.rootIncl = root.incl;
	const total = Math.max(1, root.incl);
	if (!G.treeAuto) { // first look: open the paths that hold >= 5% of the run
		G.treeAuto = true;
		const open = (t, d) => {
			if (d > 8 || t.incl < total * 0.05)
				return;
			G.treeOpen.add(t.key);
			for (const c of t.kids.values())
				open(c, d + 1);
		};
		open(root, 0);
	}
	const q = G.q.trim().toLowerCase();
	// With a search, show matches and their ancestors, all expanded.
	const matches = (t) => !q || t.name.toLowerCase().includes(q) || [...t.kids.values()].some(matches);
	const pct = (v) => (100 * v / total).toFixed(v / total < 0.001 ? 3 : 1) + "%";
	const rows = [];
	const add = (t, depth) => {
		if (!matches(t) || rows.length > 2000)
			return;
		const kids = [...t.kids.values()].sort((a, b) => b.incl - a.incl);
		const open = q ? true : G.treeOpen.has(t.key);
		rows.push({ key: t.key, name: t.name, incl: t.incl, depth, kids: kids.length > 0, open,
			nums: pct(t.incl).padStart(6) + " " + pct(t.self).padStart(6) + " " + String(t.calls).padStart(5),
			title: "inclusive " + t.incl.toLocaleString("en") + ", self " + t.self.toLocaleString("en") + " instructions, " + t.calls + " calls",
			task: t.task, how: t.how, node: t.node !== undefined && t.node < st.symName.length ? t.node : -1 });
		if (open)
			for (const c of kids)
				add(c, depth + 1);
	};
	add(root, 0);
	Object.assign(T, { rows, more: rows.length > 2000 });
	T.changed();
}

// A row's toggle: its callees shown or hidden.
function treeToggle(key) {
	if (G.treeOpen.has(key))
		G.treeOpen.delete(key);
	else
		G.treeOpen.add(key);
	drawTree();
}

// A row's name, pointed at: its entry and file:line (computed then, not
// for every row).
const treeNameTitle = (node, name) => "entry " + hex(G.stepper.symStart[node]) + ": " + dbgTitle(G.stepper.symStart[node], name);

// ---- top N graph ----------------------------------------------------------

// The scans of Top 30, each its own small function so that V8 inlines the
// FramePath and frStartAt calls into its loop (drawTop is too big for that).
// Inclusive count per symbol (Map, in the order symbols were first
// counted), counting only the outermost frame of each symbol on a path
// (recursion is not counted twice); frames that start before `start` or
// have a `hide` flag are not counted.
function topIncl(fr, start, now, hide, symbols) {
	const sum = new Float64Array(symbols), seen = new Uint8Array(symbols), order = [];
	const path = new FramePath(fr, symbols);
	for (let c = 0, chunks = fr.chunkCount(); c < chunks; c++) {
		const base = fr.baseOf(c), ds = fr.startsRaw(c), de = fr.endsRaw(c), dd = fr.depthsRaw(c);
		const par = fr.parentsOf(c), nodes = fr.nodesOf(c), fl = fr.flagsOf(c);
		for (let i = 0, k = c * FR_SIZE, len = fr.chunkLen(c); i < len; i++, k++) {
			const node = nodes[i], s = frStartAt(fr, base, ds, i, k);
			path.toParent(par[i], frSmallAt(fr, dd, i, k, "depth") - 1);
			if (s >= start && !(fl[i] & hide) && !path.count[node]) {
				const e = frEndAt(fr, de, i, k, s);
				if (!seen[node]) {
					seen[node] = 1;
					order.push(node);
				}
				sum[node] += (e < 0 ? now : e) - s;
			}
			path.push(k, node, 0);
		}
	}
	return new Map(order.map((id) => [id, sum[id]]));
}

// Edges between the shown symbols (Map from * symbols + to -> count): from
// each shown frame to its nearest shown ancestor; depth: the layer of a
// symbol, the fewest shown symbols above any of its frames; first: the
// start of its first frame. The path marks the shown frames (hidden ones
// are not).
function topEdges(fr, start, hide, symbols, shown) {
	const shownNode = new Uint8Array(symbols);
	for (const id of shown)
		shownNode[id] = 1;
	const edges = new Map(), depth = new Map(), first = new Map();
	const path = new FramePath(fr, symbols, (q) => (shownNode[fr.nodeOf(q)] && !(fr.irqOf(q) && hide) ? 1 : 0));
	for (let c = 0, chunks = fr.chunkCount(); c < chunks; c++) {
		const base = fr.baseOf(c), ds = fr.startsRaw(c), dd = fr.depthsRaw(c);
		const par = fr.parentsOf(c), nodes = fr.nodesOf(c), fl = fr.flagsOf(c);
		for (let i = 0, k = c * FR_SIZE, len = fr.chunkLen(c); i < len; i++, k++) {
			const id = nodes[i], show = shownNode[id] && !(fl[i] & hide) ? 1 : 0;
			path.toParent(par[i], frSmallAt(fr, dd, i, k, "depth") - 1);
			const s = show ? frStartAt(fr, base, ds, i, k) : 0;
			if (show && s >= start) {
				if (!first.has(id))
					first.set(id, s);
				const d = path.markedLen, p = d ? path.marked[d - 1] : -1;
				if (p >= 0 && fr.nodeOf(p) !== id && !(fl[i] & FR_CONT)) {
					const key = fr.nodeOf(p) * symbols + id;
					edges.set(key, (edges.get(key) || 0) + 1);
				}
				depth.set(id, Math.min(depth.has(id) ? depth.get(id) : 1e9, d));
			}
			path.push(k, id, show);
		}
	}
	return { edges, depth, first };
}

function drawTop() {
	const st = G.stepper;
	const c = $("graph_canvas");
	if (!st) {
		canvasSize(c, 200);
		return;
	}
	const start = runStart(), symbols = st.nodeSteps.length;
	const hide = G.fold ? FR_IRQ : 0; // hiddenFrame: a flag bit
	const incl = topIncl(st.fr, start, recTo(st), hide, symbols);
	const shown = new Set([...incl].sort((a, b) => b[1] - a[1]).slice(0, 30).map(([id]) => id));
	const { edges, depth, first } = topEdges(st.fr, start, hide, symbols, shown);
	// Layout: rows by depth, ordered by first call, wrapped to the width.
	const w = c.clientWidth;
	const ctxm = c.getContext("2d");
	ctxm.font = "12px system-ui, sans-serif";
	const byDepth = new Map();
	for (const id of shown) {
		const d = depth.has(id) ? depth.get(id) : 0;
		if (!byDepth.has(d)) byDepth.set(d, []);
		byDepth.get(d).push(id);
	}
	const pos = new Map();
	let y = 10;
	for (const d of [...byDepth.keys()].sort((a, b) => a - b)) {
		const ids = byDepth.get(d).sort((a, b) => first.get(a) - first.get(b));
		let x = 8;
		for (const id of ids) {
			const label = st.nodeName(id) + " " + pct0(incl.get(id), recTo(st) - start);
			const bw = Math.min(w - 16, ctxm.measureText(label).width + 12);
			if (x + bw > w - 8 && x > 8) {
				x = 8;
				y += 34;
			}
			pos.set(id, { x, y, w: bw, label });
			x += bw + 10;
		}
		y += 52;
	}
	const { ctx } = canvasSize(c, Math.max(200, y));
	const css = getComputedStyle(document.documentElement);
	const fg = css.getPropertyValue("--fg").trim(), muted = css.getPropertyValue("--muted").trim();
	ctx.font = "12px system-ui, sans-serif";
	ctx.textBaseline = "middle";
	ctx.strokeStyle = muted;
	for (const [key, count] of edges) {
		const pa = pos.get(Math.floor(key / symbols)), pb = pos.get(key % symbols);
		ctx.lineWidth = 0.8 + Math.log2(count) * 0.5;
		ctx.globalAlpha = pb.y > pa.y ? 0.6 : 0.25; // upward: a symbol also called from deeper down
		ctx.setLineDash(pb.y > pa.y ? [] : [4, 3]);
		ctx.beginPath();
		ctx.moveTo(pa.x + pa.w / 2, pa.y + 22);
		ctx.lineTo(pb.x + pb.w / 2, pb.y);
		ctx.stroke();
	}
	ctx.globalAlpha = 1;
	ctx.setLineDash([]);
	G.boxes = [];
	let bmarked = 0;
	for (const [id, p] of pos) {
		const bm = id < st.symName.length && api.show.bmFnNames(st.symStart[id]);
		if (bm)
			bmarked++;
		ctx.fillStyle = PALETTE[subsystem(st, id)];
		ctx.fillRect(p.x, p.y, p.w, 22);
		if (id === G.selected) {
			ctx.strokeStyle = fg;
			ctx.lineWidth = 2;
			ctx.strokeRect(p.x - 1, p.y - 1, p.w + 2, 24);
		}
		ctx.fillStyle = "#fff";
		ctx.fillText(fitLabel(ctx, (bm ? "\u2605 " : "") + p.label, p.w - 10), p.x + 6, p.y + 11);
		G.boxes.push([id, p.x, p.y, p.w, 22]);
	}
	c.dataset.nodes = pos.size;
	c.dataset.bmarks = bmarked;
	c.dataset.edges = edges.size;
}

const pct0 = (v, total) => (100 * v / Math.max(1, total)).toFixed(1) + "%";

export { drawTree, drawTop, GTREE, treeToggle, treeNameTitle };
