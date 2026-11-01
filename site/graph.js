"use strict";

// Graph tab: the symbol transitions the stepper recorded (stepper.js,
// resetGraph/graphEnter) drawn live on a canvas. Layout is incremental and
// deterministic: a node gets its place once, when it is first seen, one
// column right of the node it was first entered from, in the next free row
// of that column. Nothing is re-laid-out, so the picture only grows.

const G = {
	pos: new Map(),     // node id -> { x, y, depth }
	rows: [],           // per depth: next free row
	laid: 0,            // nodes of stepper.nodeOrder placed so far
	view: { x: -40, y: -40, scale: 1 },
	userMoved: false,   // after a pan or zoom, stop auto-fitting
	selected: -1,
	version: -1,
	timer: null,
	pointers: new Map(),
	press: null,
	stepper: null,
};
const COL_W = 170, ROW_H = 24;
// Interrupt and timer handling: folded into one node by "hide interrupt handling".
const IRQ_RE = /irq|interrupt|timer|tick|softirq|rcu|sched_clock|apic|8259|jiffies|clockevent|hrtimer|account_|update_process_times|update_wall|do_timer|calc_global_load|irqentry|idtentry|ret_from_intr|entry_INT80|preempt_schedule_irq/i;
const FOLD = -10; // id of the folded node

function folded(st, id) {
	return G.fold && id < st.symName.length && IRQ_RE.test(st.symName[id]);
}

// Nodes and edges as drawn: with folding, interrupt symbols become FOLD.
function viewGraph(st) {
	const map = (id) => (folded(st, id) ? FOLD : id);
	const edges = new Map();
	for (const [key, e] of st.edges) {
		const f = map(e.from), t = map(e.to);
		if (f === t)
			continue;
		const k = f + "," + t;
		const v = edges.get(k) || { from: f, to: t, count: 0, calls: 0, recent: false };
		v.count += e.count;
		v.calls += e.calls;
		edges.set(k, v);
	}
	for (const key of st.recent) {
		const e = st.edges.get(key);
		const v = e && edges.get(map(e.from) + "," + map(e.to));
		if (v)
			v.recent = true;
	}
	let foldSteps = 0;
	if (G.fold)
		for (const id of st.nodeOrder)
			if (folded(st, id))
				foldSteps += st.nodeSteps[id];
	G.foldSteps = foldSteps;
	return { map, edges };
}
const PALETTE = ["#1f77b4", "#d62728", "#2ca02c", "#9467bd", "#ff7f0e", "#17becf", "#8c564b", "#e377c2", "#bcbd22", "#7f7f7f",
	"#3949ab", "#00897b"];

function graphReset(st) {
	G.pos = new Map();
	G.rows = [];
	G.laid = 0;
	G.selected = -1;
	G.version = -1;
	G.userMoved = false;
	G.stepper = st;
	renderGraphInfo();
}

function graphLayout() {
	const st = G.stepper;
	for (; G.laid < st.nodeOrder.length; G.laid++) {
		const id = st.nodeOrder[G.laid];
		const parent = G.pos.get(st.nodeParent[id]);
		const depth = parent ? parent.depth + 1 : 0;
		const row = G.rows[depth] || 0;
		G.rows[depth] = row + 1;
		G.pos.set(id, { x: depth * COL_W, y: row * ROW_H, depth });
	}
}

function nodeColour(st, id) {
	if (id === FOLD)
		return "#e0245e";
	const n = st.symName.length;
	if (id >= n)
		return "#9e9e9e";
	const prefix = st.symName[id].replace(/^_+/, "").split("_")[0];
	let h = 0;
	for (let i = 0; i < prefix.length; i++)
		h = (h * 31 + prefix.charCodeAt(i)) >>> 0;
	return PALETTE[h % PALETTE.length];
}

const gSteps = (st, id) => (id === FOLD ? G.foldSteps : st.nodeSteps[id]);
const gName = (st, id) => (id === FOLD ? "(interrupt handling)" : st.nodeName(id));
const nodeRadius = (st, id) => 3 + 2.2 * Math.log10(1 + gSteps(st, id));

function graphFit() {
	const c = $("graph_canvas");
	if (!G.pos.size)
		return;
	let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
	for (const p of G.pos.values()) {
		x0 = Math.min(x0, p.x); y0 = Math.min(y0, p.y);
		x1 = Math.max(x1, p.x); y1 = Math.max(y1, p.y);
	}
	const w = c.clientWidth, h = c.clientHeight;
	const scale = Math.min(2, Math.max(0.05, Math.min((w - 40) / (x1 - x0 + COL_W), (h - 40) / (y1 - y0 + ROW_H))));
	G.view = { x: x0 - 20 / scale, y: y0 - 20 / scale, scale };
}

function drawGraph() {
	const st = G.stepper;
	const c = $("graph_canvas");
	const dpr = window.devicePixelRatio || 1;
	const w = c.clientWidth, h = c.clientHeight;
	if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) {
		c.width = Math.round(w * dpr);
		c.height = Math.round(h * dpr);
	}
	const ctx = c.getContext("2d");
	ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
	ctx.clearRect(0, 0, w, h);
	if (!st)
		return;
	graphLayout();
	if (!G.userMoved)
		graphFit();
	const css = getComputedStyle(document.documentElement);
	const fg = css.getPropertyValue("--fg").trim(), muted = css.getPropertyValue("--muted").trim();
	const accent = css.getPropertyValue("--accent").trim(), hot = css.getPropertyValue("--changed").trim();
	const { x: vx, y: vy, scale } = G.view;
	const sx = (x) => (x - vx) * scale, sy = (y) => (y - vy) * scale;
	const vg = viewGraph(st);
	if (G.fold && !G.pos.has(FOLD))
		G.pos.set(FOLD, { x: 0, y: -2 * ROW_H, depth: -1 });
	// Edges: solid when some transition landed on the symbol's first byte
	// (call-like), dashed when all went into the middle (returns, jumps).
	for (const e of vg.edges.values()) {
		const a = G.pos.get(e.from), b = G.pos.get(e.to);
		if (!a || !b)
			continue;
		const isRecent = e.recent;
		ctx.strokeStyle = isRecent ? hot : muted;
		ctx.globalAlpha = isRecent ? 1 : 0.45;
		ctx.lineWidth = (isRecent ? 1.5 : 0.6) + 0.5 * Math.log2(e.count);
		ctx.setLineDash(e.calls ? [] : [4, 3]);
		const x0 = sx(a.x), y0 = sy(a.y), x1 = sx(b.x), y1 = sy(b.y);
		ctx.beginPath();
		ctx.moveTo(x0, y0);
		if (b.depth <= a.depth) // back or sideways: bend it
			ctx.quadraticCurveTo((x0 + x1) / 2 - 30 * scale, (y0 + y1) / 2 - 20 * scale, x1, y1);
		else
			ctx.lineTo(x1, y1);
		ctx.stroke();
		if (b.depth > a.depth && scale > 0.3) { // arrow head
			const ang = Math.atan2(y1 - y0, x1 - x0), r = nodeRadius(st, e.to) * Math.min(scale, 1.5) + 2;
			ctx.setLineDash([]);
			ctx.beginPath();
			ctx.moveTo(x1 - r * Math.cos(ang), y1 - r * Math.sin(ang));
			ctx.lineTo(x1 - (r + 6) * Math.cos(ang - 0.4), y1 - (r + 6) * Math.sin(ang - 0.4));
			ctx.lineTo(x1 - (r + 6) * Math.cos(ang + 0.4), y1 - (r + 6) * Math.sin(ang + 0.4));
			ctx.closePath();
			ctx.fillStyle = ctx.strokeStyle;
			ctx.fill();
		}
	}
	ctx.setLineDash([]);
	ctx.globalAlpha = 1;
	const labels = scale >= 0.6;
	ctx.font = "11px ui-monospace, monospace";
	ctx.textBaseline = "middle";
	for (const [id, p] of G.pos) {
		if (id === FOLD && !G.fold)
			continue;
		if (folded(st, id))
			continue;
		const x = sx(p.x), y = sy(p.y);
		if (x < -50 || y < -20 || x > w + 200 || y > h + 20)
			continue;
		const r = nodeRadius(st, id) * Math.min(Math.max(scale, 0.4), 1.5);
		ctx.beginPath();
		ctx.arc(x, y, r, 0, 2 * Math.PI);
		ctx.fillStyle = nodeColour(st, id);
		ctx.fill();
		const cur = id === vg.map(st.curNode), sel = id === G.selected;
		if (cur || sel) {
			ctx.lineWidth = cur ? 3 : 2;
			ctx.strokeStyle = cur ? hot : fg;
			ctx.beginPath();
			ctx.arc(x, y, r + 3, 0, 2 * Math.PI);
			ctx.stroke();
		}
		if (labels || cur || sel) {
			ctx.fillStyle = fg;
			ctx.fillText(gName(st, id), x + r + 3, y);
		}
	}
}

function graphStatus() {
	const st = G.stepper;
	const el = $("graph_status");
	const nodes = st ? st.nodeOrder.length : 0, edges = st ? st.edges.size : 0;
	el.dataset.nodes = nodes;
	el.dataset.edges = edges;
	el.textContent = !st ? "No stepper for this kernel." :
		nodes + " symbols, " + edges + " distinct transitions" +
		(st.curNode >= 0 ? "; now in " + st.nodeName(st.curNode) : "") + ".";
}

function graphTick() {
	if (!G.stepper || tab !== "graph")
		return;
	if (G.stepper.graphVersion !== G.version || G.dirty) {
		G.version = G.stepper.graphVersion;
		G.dirty = false;
		drawGraph();
		graphStatus();
		if (G.selected >= 0)
			renderGraphInfo();
	}
}

function graphShow(visible) {
	clearInterval(G.timer);
	G.timer = null;
	if (visible) {
		G.dirty = true;
		graphTick();
		G.timer = setInterval(graphTick, 100); // ~10 fps
	}
}

function graphNodeAt(px, py) {
	const st = G.stepper;
	if (!st)
		return -1;
	const { x: vx, y: vy, scale } = G.view;
	let best = -1, bestD = 18 * 18;
	for (const [id, p] of G.pos) {
		if (folded(st, id) || (id === FOLD && !G.fold))
			continue;
		const dx = (p.x - vx) * scale - px, dy = (p.y - vy) * scale - py;
		const d = dx * dx + dy * dy;
		if (d < bestD) {
			bestD = d;
			best = id;
		}
	}
	return best;
}

function renderGraphInfo() {
	const el = $("graph_info");
	el.textContent = "";
	const st = G.stepper;
	const id = G.selected;
	if (!st || (id < 0 && id !== FOLD)) {
		el.textContent = "Tap a symbol for its callers and callees.";
		return;
	}
	const n = st.symName.length;
	const head = document.createElement("div");
	const b = document.createElement("b");
	b.textContent = gName(st, id);
	head.append(b, " " + num(gSteps(st, id)) + " instructions inside" +
		(id === FOLD ? "" : ", first entered at step " + num(st.nodeFirst[id])) + " ");
	if (id >= 0 && id < n) {
		const open = document.createElement("button");
		open.type = "button";
		open.textContent = "Page tab";
		open.addEventListener("click", () => jumpTo(st.symStart[id]));
		head.append(open);
	}
	el.append(head);
	const list = (title, edges, other) => {
		const d = document.createElement("div");
		d.className = "glist";
		const t = document.createElement("b");
		t.textContent = title + " (" + edges.length + ")";
		d.append(t, " ");
		edges.sort((x, y) => y.count - x.count);
		for (const e of edges.slice(0, 15)) {
			const btn = document.createElement("button");
			btn.type = "button";
			btn.className = "symlink";
			btn.textContent = gName(st, e[other]) + " \u00d7" + e.count + (e.calls ? "" : " (jump/return)");
			btn.addEventListener("click", () => { G.selected = e[other]; G.dirty = true; renderGraphInfo(); });
			d.append(btn, " ");
		}
		if (edges.length > 15)
			d.append("and " + (edges.length - 15) + " more");
		el.append(d);
	};
	const inc = [], out = [];
	for (const e of viewGraph(st).edges.values()) {
		if (e.to === id)
			inc.push(e);
		if (e.from === id)
			out.push(e);
	}
	list("from", inc, "from");
	list("to", out, "to");
	el.dataset.node = gName(st, id);
	el.dataset.callers = inc.length;
}

function graphData() {
	const st = G.stepper;
	const n = st.symName.length;
	return {
		kernel: image ? image.name : null,
		note: "Symbol transitions recorded while stepping (v86 interpreter); runs at full JIT speed are not recorded.",
		steps: st.steps,
		nodes: st.nodeOrder.map((id) => ({
			id, name: st.nodeName(id), paddr: id < n ? st.symStart[id] : null,
			size: id < n ? st.symEnd[id] - st.symStart[id] : null,
			instructions: st.nodeSteps[id], first_step: st.nodeFirst[id],
		})),
		edges: [...st.edges.values()].map((e) => ({
			from: e.from, to: e.to, from_name: st.nodeName(e.from), to_name: st.nodeName(e.to),
			count: e.count, calls: e.calls, first_step: e.first,
		})),
	};
}

function graphDot() {
	const d = graphData();
	const q = (s) => '"' + String(s).replace(/["\\]/g, "\\$&") + '"';
	const lines = ["digraph symbols {", "  rankdir=LR;", "  node [shape=box, fontname=monospace];"];
	for (const nd of d.nodes)
		lines.push("  n" + nd.id + " [label=" + q(nd.name + "\\n" + nd.instructions + " insns") + "];");
	for (const e of d.edges)
		lines.push("  n" + e.from + " -> n" + e.to + " [label=" + e.count + (e.calls ? "" : ", style=dashed") +
			", penwidth=" + (1 + Math.log2(e.count)).toFixed(1) + "];");
	lines.push("}");
	return lines.join("\n") + "\n";
}

function download(name, text, type) {
	const a = document.createElement("a");
	a.href = URL.createObjectURL(new Blob([text], { type }));
	a.download = name;
	a.click();
	setTimeout(() => URL.revokeObjectURL(a.href), 10000);
}

function initGraph() {
	const c = $("graph_canvas");
	const local = (ev) => {
		const r = c.getBoundingClientRect();
		return [ev.clientX - r.left, ev.clientY - r.top];
	};
	const zoomAt = (px, py, f) => {
		const v = G.view;
		const s = Math.min(4, Math.max(0.03, v.scale * f));
		v.x += px / v.scale - px / s;
		v.y += py / v.scale - py / s;
		v.scale = s;
		G.userMoved = true;
		G.dirty = true;
	};
	c.addEventListener("pointerdown", (ev) => {
		c.setPointerCapture(ev.pointerId);
		G.pointers.set(ev.pointerId, local(ev));
		G.press = G.pointers.size === 1 ? { at: local(ev), t: realNow(), moved: false } : null;
	});
	c.addEventListener("pointermove", (ev) => {
		if (!G.pointers.has(ev.pointerId))
			return;
		const [x, y] = local(ev);
		const [px, py] = G.pointers.get(ev.pointerId);
		if (G.pointers.size === 1) {
			G.view.x -= (x - px) / G.view.scale;
			G.view.y -= (y - py) / G.view.scale;
			if (G.press && Math.hypot(x - G.press.at[0], y - G.press.at[1]) > 5)
				G.press.moved = true;
			G.userMoved = true;
			G.dirty = true;
		} else if (G.pointers.size === 2) {
			const [other] = [...G.pointers].filter(([k]) => k !== ev.pointerId).map(([, v]) => v);
			const d0 = Math.hypot(px - other[0], py - other[1]), d1 = Math.hypot(x - other[0], y - other[1]);
			if (d0 > 0)
				zoomAt((x + other[0]) / 2, (y + other[1]) / 2, d1 / d0);
		}
		G.pointers.set(ev.pointerId, [x, y]);
	});
	const up = (ev) => {
		if (G.press && !G.press.moved && G.pointers.size === 1) {
			const [x, y] = local(ev);
			G.selected = graphNodeAt(x, y);
			G.dirty = true;
			renderGraphInfo();
		}
		G.pointers.delete(ev.pointerId);
		G.press = null;
	};
	c.addEventListener("pointerup", up);
	c.addEventListener("pointercancel", (ev) => { G.pointers.delete(ev.pointerId); G.press = null; });
	c.addEventListener("wheel", (ev) => {
		ev.preventDefault();
		const [x, y] = local(ev);
		zoomAt(x, y, Math.exp(-ev.deltaY * 0.0015));
	}, { passive: false });
	$("graph_fit").addEventListener("click", () => { G.userMoved = false; G.dirty = true; graphTick(); });
	$("graph_json").addEventListener("click", () => {
		if (G.stepper)
			download((image ? image.name : "kernel") + "-symbol-graph.json", JSON.stringify(graphData(), null, 1), "application/json");
	});
	$("graph_dot").addEventListener("click", () => {
		if (G.stepper)
			download((image ? image.name : "kernel") + "-symbol-graph.dot", graphDot(), "text/vnd.graphviz");
	});
	window.addEventListener("resize", () => { G.dirty = true; });
	$("graph_fold").addEventListener("change", () => {
		G.fold = $("graph_fold").checked;
		if (G.selected >= 0 && folded(G.stepper, G.selected))
			G.selected = FOLD;
		G.dirty = true;
		graphTick();
		renderGraphInfo();
	});
}
