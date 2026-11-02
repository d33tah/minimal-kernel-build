"use strict";

// Graph tab: the call structure the stepper reconstructed (stepper.js:
// graphEnter records symbol transitions, stackEnter turns them into call
// frames). Three views:
//   timeline  flame chart, x = instruction number, y = stack depth
//   tree      call tree like `perf report --children`
//   top       node-link graph of the 30 symbols with the largest inclusive
//             count, call edges only, layered by depth
// Interrupt and exception frames (entered through an entry stub, and
// everything they call) are muted and hidden by "hide interrupt handling".

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
const PALETTE = { init: "#2ca02c", mm: "#1f77b4", fs: "#ff7f0e", kernel: "#d62728", arch: "#9467bd", lib: "#8c564b",
	drivers: "#17becf", include: "#bcbd22", other: "#7f7f7f" };

function graphReset(st) {
	G.stepper = st;
	G.selected = -1;
	G.version = -1;
	G.userMoved = false;
	G.yOff = 0;
	G.xOff = 0;
	G.focus = -1;
	G.layout = null;
	G.treeOpen = new Set(["0"]);
	G.treeAuto = false;
	G.subsys = null;
	renderGraphInfo();
}

// ---- data -------------------------------------------------------------------

const nowStep = () => (G.stepper ? G.stepper.steps : 0);
// The run starts at the kernel entry once it has been reached.
const runStart = () => (G.stepper && G.stepper.sinceEntry >= 0 ? G.stepper.sinceEntry : 0);
const fEnd = (st, k) => (st.fEnd[k] < 0 ? st.steps : st.fEnd[k]);
const hiddenFrame = (st, k) => G.fold && st.fIrq[k];

// Subsystem of a symbol: the top directory of its source file (sources.json),
// else a guess from the name.
function subsystem(st, node) {
	if (!G.subsys) {
		G.subsys = new Map();
		if (typeof sources === "object" && sources)
			for (const [fn, f] of sources.ranges) {
				const name = sources.funcs[fn];
				if (!G.subsys.has(name))
					G.subsys.set(name, sources.files[f].split("/")[0]);
			}
	}
	const name = st.nodeName(node);
	const dir = G.subsys.get(name);
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
	return st.fIrq[k] ? "#b0b0b0" : PALETTE[subsystem(st, st.fNode[k])];
}

// Inclusive / self counts of a frame (open frames up to now).
function frameCounts(st, k) {
	const incl = fEnd(st, k) - st.fStart[k];
	let child = st.fChild[k];
	// Open children are not in fChild yet.
	for (const o of st.allOpen())
		if (st.fParent[o] === k)
			child += st.steps - st.fStart[o];
	return { incl, self: incl - child };
}

// Call tree aggregated by call path (perf report --children), from the
// frames that start at or after the kernel entry.
function buildTree(st) {
	const start = runStart();
	const root = { key: "0", name: "(kernel, from the entry at 0x100000)", incl: st.steps - start, self: 0, calls: 1, kids: new Map() };
	const nodeOf = new Map(); // frame -> tree node
	for (let k = 0; k < st.fStart.length; k++) {
		if (st.fStart[k] < start)
			continue;
		const p = st.fParent[k];
		let parent = p >= 0 && nodeOf.has(p) ? nodeOf.get(p) : root;
		let name = st.nodeName(st.fNode[k]);
		if (G.fold && st.fIrq[k]) {
			if (p >= 0 && st.fIrq[p] && nodeOf.has(p)) { // inside a folded interrupt: count in it
				nodeOf.set(k, nodeOf.get(p));
				continue;
			}
			name = "(interrupt and exception handling)";
		}
		let t = parent.kids.get(name);
		if (!t) {
			t = { key: parent.key + "/" + name, name, node: st.fNode[k], incl: 0, self: 0, calls: 0, kids: new Map() };
			parent.kids.set(name, t);
		}
		const c = frameCounts(st, k);
		t.incl += c.incl;
		t.calls++;
		nodeOf.set(k, t);
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

// ---- timeline -------------------------------------------------------------

function canvasSize(c, h) {
	const dpr = window.devicePixelRatio || 1;
	const w = c.clientWidth;
	c.style.height = h + "px";
	if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) {
		c.width = Math.round(w * dpr);
		c.height = Math.round(h * dpr);
	}
	const ctx = c.getContext("2d");
	ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
	ctx.clearRect(0, 0, w, h);
	return { ctx, w, h };
}

function fitLabel(ctx, text, w) {
	if (ctx.measureText(text).width <= w)
		return text;
	let lo = 0, hi = text.length;
	while (lo < hi) {
		const m = (lo + hi + 1) >> 1;
		if (ctx.measureText(text.slice(0, m) + "…").width <= w) lo = m; else hi = m - 1;
	}
	return lo >= 3 ? text.slice(0, lo) + "…" : "";
}

// Display rows of the frames: interrupt frames hidden (when folded), and
// single-child chains whose parent has almost no self time drawn as one
// box "a > b > c" (when compressing). Cached until frames or settings change.
function timelineLayout(st) {
	const n = st.fStart.length;
	const key = n + "," + G.fold + "," + G.chains + "," + st.steps;
	if (G.layout && G.layout.key === key)
		return G.layout;
	const kids = new Int32Array(n), row = new Int32Array(n).fill(-1), head = new Int32Array(n);
	for (let k = 0; k < n; k++) {
		const p = st.fParent[k];
		if (p >= 0 && !hiddenFrame(st, k))
			kids[p]++;
	}
	const chain = new Map(); // head -> names below it
	for (let k = 0; k < n; k++) {
		if (hiddenFrame(st, k))
			continue;
		const p = st.fParent[k];
		if (p < 0 || row[p] < 0) {
			row[k] = 0;
			head[k] = k;
			continue;
		}
		const incl = fEnd(st, p) - st.fStart[p];
		const self = incl - st.fChild[p] - (st.fEnd[p] < 0 || st.fEnd[k] < 0 ? fEnd(st, k) - st.fStart[k] : 0);
		if (G.chains && kids[p] === 1 && self <= Math.max(2, incl * 0.01)) {
			row[k] = row[p];
			head[k] = head[p];
			if (!chain.has(head[k]))
				chain.set(head[k], []);
			chain.get(head[k]).push(k);
		} else {
			row[k] = row[p] + 1;
			head[k] = k;
		}
	}
	const deepest = Int32Array.from(row);
	for (let k = n - 1; k >= 0; k--) {
		const p = st.fParent[k];
		if (p >= 0 && row[k] >= 0 && deepest[k] > deepest[p])
			deepest[p] = deepest[k];
	}
	G.layout = { key, row, head, chain, deepest };
	return G.layout;
}

function frameLabel(st, L, k) {
	const names = [st.nodeName(st.fNode[k])];
	for (const c of L.chain.get(k) || [])
		names.push(st.nodeName(st.fNode[c]));
	return names.join(" › ");
}

function isUnder(st, k, f) {
	for (let p = k; p >= 0; p = st.fParent[p])
		if (p === f)
			return true;
	return false;
}

function drawTimeline() {
	const st = G.stepper;
	const c = $("graph_canvas");
	const h = Math.max(260, Math.min(window.innerHeight * (G.transpose ? 0.75 : 0.62), G.transpose ? 900 : 640));
	const { ctx, w } = canvasSize(c, h);
	G.rects = [];
	if (!st)
		return;
	const css = getComputedStyle(document.documentElement);
	const fg = css.getPropertyValue("--fg").trim(), muted = css.getPropertyValue("--muted").trim();
	const hot = css.getPropertyValue("--changed").trim(), line = css.getPropertyValue("--line").trim();
	const bg = css.getPropertyValue("--bg").trim();
	const L = timelineLayout(st);
	const focus = G.focus >= 0 && G.focus < st.fStart.length ? G.focus : -1;
	const t0 = focus >= 0 ? st.fStart[focus] : runStart();
	const t1 = Math.max(t0 + 1, focus >= 0 ? fEnd(st, focus) : st.steps);
	if (!G.userMoved) {
		G.x0 = t0;
		G.x1 = t1;
	} else if (G.follow && focus < 0 && runState === "stepped" && st.steps > G.x1) {
		const span = G.x1 - G.x0;
		G.x1 = st.steps + span * 0.1;
		G.x0 = G.x1 - span;
	}
	const span = Math.max(1, G.x1 - G.x0);
	const rowOff = focus >= 0 ? L.row[focus] : 0;
	const cap = G.maxDepth > 0 ? G.maxDepth : Infinity;
	const q = G.view === "timeline" ? $("graph_search").value.trim().toLowerCase() : "";
	ctx.font = "11px system-ui, sans-serif";
	ctx.textBaseline = "middle";
	const n = st.fStart.length;
	// Minimap strip: the whole run (or the focused frame), shallow rows.
	const msx = (t) => (t - t0) / (t1 - t0) * w;
	for (let k = 0; k < n; k++) {
		const r = L.row[k] - rowOff;
		if (r < 0 || r > 5 || L.head[k] !== k || (focus >= 0 && !isUnder(st, k, focus)))
			continue;
		const a = Math.max(st.fStart[k], t0), b = Math.min(fEnd(st, k), t1);
		if (b <= a)
			continue;
		const x = msx(a), ww = msx(b) - x;
		if (ww < 0.5)
			continue;
		ctx.fillStyle = frameColour(st, k);
		ctx.fillRect(x, 2 + r * 3.5, Math.max(ww, 0.5), 3);
	}
	ctx.strokeStyle = fg;
	ctx.lineWidth = 1;
	ctx.strokeRect(msx(G.x0) + 0.5, 0.5, Math.max(2, msx(G.x1) - msx(G.x0)), MINI - 2);
	ctx.fillStyle = line;
	ctx.fillRect(0, MINI, w, 1);
	// Geometry: time runs along `along` pixels; rows across.
	const COL = 120;
	const areaTop = MINI + 6, areaH = h - areaTop - 18;
	const along = G.transpose ? areaH : w;
	const tp = (t) => (t - G.x0) / span * along;
	const rect = (a, b, r) => {
		const p0 = Math.max(0, tp(a)), p1 = Math.min(along, tp(b));
		if (G.transpose)
			return [r * COL - G.xOff, areaTop + p0, COL - 1, p1 - p0];
		if (G.flame)
			return [p0, areaTop + areaH - (r + 1) * ROW + G.yOff, p1 - p0, ROW - 1];
		return [p0, areaTop + r * ROW - G.yOff, p1 - p0, ROW - 1];
	};
	// Follow: keep the frame that runs now (the innermost open one) in
	// the visible band, scrolling across rows only when it leaves it.
	if (G.follow && runState === "stepped" && st.stack.length) {
		const cur = st.stack[st.stack.length - 1];
		const r = L.row[cur] >= 0 ? Math.min(L.row[cur] - rowOff, cap) : -1;
		if (r >= 0) {
			if (G.transpose) {
				const x = r * COL - G.xOff;
				if (x < COL || x > w - 2 * COL)
					G.xOff = Math.max(0, r * COL - Math.floor(w / 2));
			} else {
				const pos = r * ROW - G.yOff; // distance from the root edge
				if (pos < 2 * ROW || pos > areaH - 3 * ROW)
					G.yOff = Math.max(0, r * ROW - Math.floor(areaH / 2));
			}
		}
		G.followRow = r;
	}
	let drawn = 0, labelled = 0, matches = 0;
	const labels = [], more = [];
	for (let k = 0; k < n; k++) {
		if (L.head[k] !== k || L.row[k] < 0)
			continue;
		const r = L.row[k] - rowOff;
		if (r < 0 || r > cap)
			continue;
		const a = st.fStart[k], b = fEnd(st, k);
		if (b < G.x0 || a > G.x1)
			continue;
		if (focus >= 0 && !isUnder(st, k, focus))
			continue;
		const [x, y, rw, rh] = rect(a, b, r);
		if ((G.transpose ? rh : rw) < 1)
			continue;
		if (y + rh < areaTop || y > areaTop + areaH || x + rw < 0 || x > w)
			continue;
		const label = frameLabel(st, L, k);
		const hit = q && label.toLowerCase().includes(q);
		if (hit)
			matches++;
		ctx.fillStyle = frameColour(st, k);
		ctx.globalAlpha = q ? (hit ? 1 : 0.18) : st.fIrq[k] ? 0.6 : 0.9;
		ctx.fillRect(x, y, Math.max(rw - 0.5, 0.5), Math.max(rh - 0.5, 0.5));
		ctx.globalAlpha = 1;
		if (k === G.selected) {
			ctx.strokeStyle = fg;
			ctx.lineWidth = 2;
			ctx.strokeRect(x + 1, y + 1, rw - 2, rh - 2);
		}
		G.rects.push([k, x, y, rw, rh]);
		drawn++;
		if (G.transpose ? rh >= 13 : rw >= 40)
			labels.push([k, x, y, rw, rh, label]);
		if (r === cap && L.deepest[k] - rowOff > cap)
			more.push([x, y, rw, rh, L.deepest[k] - rowOff - cap]);
	}
	ctx.fillStyle = "#fff";
	const pinned = new Set(); // transposed: one label pinned at the top per column
	for (const [, x, y, rw, rh, label] of labels) {
		let ly = y + rh / 2;
		if (G.transpose) {
			ly = y + 7;
			if (y < areaTop) {
				if (pinned.has(x) || y + rh < areaTop + 14)
					continue;
				pinned.add(x);
				ly = areaTop + 7;
			}
		}
		const text = fitLabel(ctx, label, rw - 6);
		if (text) {
			ctx.fillText(text, Math.max(x, 0) + 3, ly);
			labelled++;
		}
	}
	ctx.fillStyle = fg;
	for (const [x, y, rw, rh, d] of more) // "+N deeper" past the depth cap
		if (G.transpose)
			ctx.fillText("+" + d, x + rw + 2, y + Math.min(rh, 14) / 2);
		else
			ctx.fillText("+" + d, x + 2, G.flame ? y - 6 : y + rh + 6);
	// Sticky labels: rows scrolled out of view keep the names of the
	// frames over the middle of the window.
	if (!G.transpose && G.yOff > 0) {
		const tm = (G.x0 + G.x1) / 2, hidden = [];
		for (let k = 0; k < n; k++)
			if (L.head[k] === k && L.row[k] >= rowOff && st.fStart[k] <= tm && fEnd(st, k) > tm &&
				(focus < 0 || isUnder(st, k, focus))) {
				const [, y] = rect(st.fStart[k], fEnd(st, k), L.row[k] - rowOff);
				if (G.flame ? y > areaTop + areaH : y < areaTop)
					hidden.push([L.row[k], frameLabel(st, L, k)]);
			}
		if (hidden.length) {
			hidden.sort((a, b) => a[0] - b[0]);
			const yb = G.flame ? areaTop + areaH - ROW : areaTop;
			ctx.fillStyle = bg;
			ctx.fillRect(0, yb, w, ROW);
			ctx.fillStyle = muted;
			ctx.fillText(fitLabel(ctx, "↑ " + hidden.map((x) => x[1]).join(" › "), w - 8), 4, yb + ROW / 2);
		}
	}
	// Now.
	if (st.steps >= G.x0 && st.steps <= G.x1) {
		ctx.fillStyle = hot;
		const p = tp(st.steps);
		if (G.transpose)
			ctx.fillRect(0, areaTop + p - 1, w, 2);
		else
			ctx.fillRect(p - 1, MINI, 2, h - MINI);
	}
	ctx.fillStyle = muted;
	ctx.fillText((G.x0 - runStart()).toLocaleString("en") + " – " + (G.x1 - runStart()).toLocaleString("en") +
		" instructions from the entry" + (G.transpose ? " (top to bottom)" : ""), 4, h - 8);
	c.dataset.drawn = drawn;
	c.dataset.labels = labelled;
	c.dataset.matches = matches;
	// For tests: is the running frame's row inside the drawing area?
	if (st.stack.length) {
		const cur = st.stack[st.stack.length - 1];
		const r = L.row[cur] - rowOff;
		if (L.row[cur] >= 0 && r >= 0) {
			const [x, y, rw, rh] = rect(st.fStart[cur], st.steps, Math.min(r, cap));
			c.dataset.curRow = r;
			c.dataset.curVisible = G.transpose ? (x >= 0 && x + rw <= w) : (y >= areaTop && y + rh <= areaTop + areaH);
		}
	}
	c.dataset.focus = focus >= 0 ? st.nodeName(st.fNode[focus]) : "";
	renderCrumbs(st);
}

// Breadcrumb from the root to the focused frame; each entry re-focuses.
function renderCrumbs(st) {
	const el = $("graph_crumbs");
	const f = G.focus;
	const key = f + "," + (st ? st.fStart.length : 0);
	if (el.dataset.key === key)
		return;
	el.dataset.key = key;
	el.textContent = "";
	if (!st || f < 0) {
		el.hidden = true;
		return;
	}
	el.hidden = false;
	const chain = [];
	for (let p = f; p >= 0; p = st.fParent[p])
		chain.unshift(p);
	const btn = (text, k) => {
		const b = document.createElement("button");
		b.type = "button";
		b.className = "symlink";
		b.textContent = text;
		b.addEventListener("click", () => focusFrame(k));
		return b;
	};
	el.append(btn("root", -1));
	for (const k of chain.slice(-8))
		el.append(" › ", btn(st.nodeName(st.fNode[k]), k));
}

function focusFrame(k) {
	G.focus = k;
	G.userMoved = false;
	G.yOff = 0;
	G.xOff = 0;
	G.dirty = true;
	graphTick();
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
	for (let k = 0; k < st.fStart.length; k++) {
		if (st.fStart[k] < start && fEnd(st, k) <= start)
			continue;
		const incl = fEnd(st, k) - Math.max(st.fStart[k], start);
		if (st.fParent[k] < 0)
			rootSum += incl;
		if (st.fDepth[k] <= 3)
			big.push({ name: st.nodeName(st.fNode[k]), depth: st.fDepth[k], incl, irq: !!st.fIrq[k] });
	}
	big.sort((a, b) => b.incl - a.incl);
	const d = st.fDepth.slice().sort((a, b) => a - b);
	return { total: st.steps - start, rootSum, frames: st.fStart.length, top: big.slice(0, 25),
		maxDepth: d.length ? d[d.length - 1] : 0, p99Depth: d.length ? d[Math.floor(d.length * 0.99)] : 0 };
}

// Callers and callees of the selected frame's function, aggregated over
// all its frames (the "sandwich").
function sandwich(st, node) {
	const callers = new Map(), callees = new Map();
	const add = (m, id, incl) => {
		const v = m.get(id) || { count: 0, incl: 0 };
		v.count++;
		v.incl += incl;
		m.set(id, v);
	};
	for (let k = 0; k < st.fStart.length; k++) {
		const p = st.fParent[k];
		if (p < 0)
			continue;
		const incl = fEnd(st, k) - st.fStart[k];
		if (st.fNode[k] === node && st.fNode[p] !== node)
			add(callers, st.fNode[p], incl);
		if (st.fNode[p] === node && st.fNode[k] !== node)
			add(callees, st.fNode[k], incl);
	}
	const sorted = (m) => [...m].sort((a, b) => b[1].incl - a[1].incl);
	return { callers: sorted(callers), callees: sorted(callees) };
}

// ---- call tree ------------------------------------------------------------

function drawTree() {
	const st = G.stepper;
	const box = $("graph_tree");
	box.textContent = "";
	if (!st)
		return;
	const root = buildTree(st);
	box.dataset.rootIncl = root.incl;
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
	const q = $("graph_search").value.trim().toLowerCase();
	// With a search, show matches and their ancestors, all expanded.
	const matches = (t) => !q || t.name.toLowerCase().includes(q) || [...t.kids.values()].some(matches);
	const pct = (v) => (100 * v / total).toFixed(v / total < 0.001 ? 3 : 1) + "%";
	let rows = 0;
	const head = document.createElement("div");
	head.className = "trow thead";
	head.innerHTML = '<span class="ttog"></span><span class="tnum mono">  incl   self calls</span><span class="tname">symbol (call path)</span>';
	box.append(head);
	const add = (t, depth) => {
		if (!matches(t) || rows > 2000)
			return;
		rows++;
		const row = document.createElement("div");
		row.className = "trow";
		row.dataset.name = t.name;
		row.dataset.incl = t.incl;
		const kids = [...t.kids.values()].sort((a, b) => b.incl - a.incl);
		const open = q ? true : G.treeOpen.has(t.key);
		const tog = document.createElement("button");
		tog.type = "button";
		tog.className = "ttog";
		tog.textContent = kids.length ? (open ? "\u25be" : "\u25b8") : "\u00b7";
		tog.disabled = !kids.length;
		tog.addEventListener("click", () => {
			if (G.treeOpen.has(t.key)) G.treeOpen.delete(t.key); else G.treeOpen.add(t.key);
			drawTree();
		});
		const nums = document.createElement("span");
		nums.className = "tnum mono";
		nums.textContent = pct(t.incl).padStart(6) + " " + pct(t.self).padStart(6) + " " + String(t.calls).padStart(5);
		nums.title = "inclusive " + t.incl.toLocaleString("en") + ", self " + t.self.toLocaleString("en") + " instructions, " + t.calls + " calls";
		const name = document.createElement("span");
		name.className = "tname mono";
		name.style.paddingLeft = Math.min(depth, 30) * 0.7 + "em";
		name.textContent = t.name;
		if (t.node !== undefined && t.node < st.symName.length)
			name.addEventListener("click", () => jumpTo(st.symStart[t.node]));
		row.append(tog, nums, name);
		box.append(row);
		if (open)
			for (const c of kids)
				add(c, depth + 1);
	};
	add(root, 0);
	if (rows > 2000)
		box.append("(more rows not shown: search to narrow)");
}

// ---- top N graph ----------------------------------------------------------

function drawTop() {
	const st = G.stepper;
	const c = $("graph_canvas");
	if (!st) {
		canvasSize(c, 200);
		return;
	}
	const start = runStart();
	// Inclusive count per symbol, counting only the outermost frame of each
	// symbol on a path (recursion is not counted twice).
	const incl = new Map();
	const n = st.fStart.length;
	for (let k = 0; k < n; k++) {
		if (st.fStart[k] < start || hiddenFrame(st, k))
			continue;
		let p = st.fParent[k], rec = false;
		while (p >= 0) {
			if (st.fNode[p] === st.fNode[k]) { rec = true; break; }
			p = st.fParent[p];
		}
		if (!rec)
			incl.set(st.fNode[k], (incl.get(st.fNode[k]) || 0) + fEnd(st, k) - st.fStart[k]);
	}
	const shown = new Set([...incl].sort((a, b) => b[1] - a[1]).slice(0, 30).map(([id]) => id));
	// Edges between shown symbols: from each shown frame to its nearest shown ancestor.
	const edges = new Map(), depth = new Map(), first = new Map();
	for (let k = 0; k < n; k++) {
		if (st.fStart[k] < start || hiddenFrame(st, k) || !shown.has(st.fNode[k]))
			continue;
		const id = st.fNode[k];
		if (!first.has(id))
			first.set(id, st.fStart[k]);
		let p = st.fParent[k];
		while (p >= 0 && !(shown.has(st.fNode[p]) && !hiddenFrame(st, p)))
			p = st.fParent[p];
		if (p >= 0 && st.fNode[p] !== id) {
			const key = st.fNode[p] + "," + id;
			edges.set(key, (edges.get(key) || 0) + 1);
		}
	}
	// Layer of a symbol: the fewest shown symbols above any of its frames.
	for (let k = 0; k < n; k++) {
		if (st.fStart[k] < start || hiddenFrame(st, k) || !shown.has(st.fNode[k]))
			continue;
		let d = 0;
		for (let q = st.fParent[k]; q >= 0; q = st.fParent[q])
			if (shown.has(st.fNode[q]) && !hiddenFrame(st, q))
				d++;
		const id = st.fNode[k];
		depth.set(id, Math.min(depth.has(id) ? depth.get(id) : 1e9, d));
	}
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
			const label = st.nodeName(id) + " " + pct0(incl.get(id), st.steps - start);
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
		const [a, b] = key.split(",").map(Number);
		const pa = pos.get(a), pb = pos.get(b);
		if (!pa || !pb)
			continue;
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
	for (const [id, p] of pos) {
		ctx.fillStyle = PALETTE[subsystem(st, id)];
		ctx.fillRect(p.x, p.y, p.w, 22);
		if (id === G.selected) {
			ctx.strokeStyle = fg;
			ctx.lineWidth = 2;
			ctx.strokeRect(p.x - 1, p.y - 1, p.w + 2, 24);
		}
		ctx.fillStyle = "#fff";
		ctx.fillText(fitLabel(ctx, p.label, p.w - 10), p.x + 6, p.y + 11);
		G.boxes.push([id, p.x, p.y, p.w, 22]);
	}
	c.dataset.nodes = pos.size;
	c.dataset.edges = edges.size;
}

const pct0 = (v, total) => (100 * v / Math.max(1, total)).toFixed(1) + "%";

// ---- shared -----------------------------------------------------------------

// Max and 99th-percentile call depth (sampled for big runs).
function timelineDepth(st) {
	const n = st.fDepth.length;
	if (!n)
		return null;
	if (G.depthKey === n)
		return G.depthVal;
	const step = Math.max(1, Math.floor(n / 200000));
	const d = [];
	let max = 0;
	for (let k = 0; k < n; k += step) {
		d.push(st.fDepth[k]);
		if (st.fDepth[k] > max) max = st.fDepth[k];
	}
	for (let k = 0; k < n; k++)
		if (st.fDepth[k] > max) max = st.fDepth[k];
	d.sort((a, b) => a - b);
	G.depthKey = n;
	G.depthVal = { max, p99: d[Math.floor(d.length * 0.99)] };
	return G.depthVal;
}

function graphStatus() {
	const st = G.stepper;
	const el = $("graph_status");
	const nodes = st ? st.nodeOrder.length : 0, edges = st ? st.edges.size : 0;
	el.dataset.nodes = nodes;
	el.dataset.edges = edges;
	el.dataset.frames = st ? st.fStart.length : 0;
	const depth = st ? timelineDepth(st) : null;
	el.textContent = !st ? "No stepper for this kernel." :
		num(st.fStart.length) + " calls, " + nodes + " symbols, " + edges + " transitions recorded" +
		(depth ? "; call depth max " + depth.max + ", p99 " + depth.p99 + (st.espUnwind ? " (ESP unwinding)" : "") : "") +
		(runState === "jit" ? "; running at full JIT speed: not recorded" : "") +
		(st.framesFull ? "; the timeline stopped at " + num(st.framesFull) +
			" calls because the tab's JavaScript heap was 80% full (the counters keep counting)" : "") + ".";
}

function drawGraph() {
	$("graph_canvas").hidden = G.view === "tree";
	$("graph_tree").hidden = G.view !== "tree";
	$("graph_search").hidden = G.view === "top";
	for (const id of ["graph_tl_opts"])
		$(id).hidden = G.view !== "timeline";
	$("graph_follow_l").hidden = G.view !== "timeline";
	for (const v of ["timeline", "tree", "top"])
		$("gview_" + v).setAttribute("aria-pressed", v === G.view);
	if (G.view === "timeline")
		drawTimeline();
	else if (G.view === "tree")
		drawTree();
	else
		drawTop();
	graphStatus();
}

function graphTick() {
	if (!G.stepper || tab !== "graph")
		return;
	if (G.stepper.graphVersion !== G.version || G.dirty) {
		// The call tree is DOM: rebuild it at most every 2 s while running.
		if (G.view === "tree" && !G.dirty && realNow() - (G.treeAt || 0) < 2000)
			return;
		G.treeAt = realNow();
		G.version = G.stepper.graphVersion;
		G.dirty = false;
		drawGraph();
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

function renderGraphInfo() {
	const el = $("graph_info");
	el.textContent = "";
	const st = G.stepper;
	if (!st || G.selected < 0) {
		el.textContent = G.view === "tree" ? "" : "Tap a box for its counts and callers.";
		delete el.dataset.node;
		return;
	}
	const head = document.createElement("div");
	const b = document.createElement("b");
	let node;
	if (G.view === "timeline") {
		const k = G.selected;
		node = st.fNode[k];
		const c = frameCounts(st, k);
		b.textContent = st.nodeName(node);
		const path = [];
		for (let p = st.fParent[k]; p >= 0 && path.length < 8; p = st.fParent[p])
			path.push(st.nodeName(st.fNode[p]));
		head.append(b, " inclusive " + num(c.incl) + ", self " + num(c.self) + " instructions; steps " +
			num(st.fStart[k] - runStart()) + "\u2013" + num(fEnd(st, k) - runStart()) + " from the entry" +
			(st.fIrq[k] ? "; in an interrupt" : "") + ". Called from: " + (path.join(" \u2190 ") || "(top)") + " ");
		const fb = document.createElement("button");
		fb.type = "button";
		fb.id = "graph_focus";
		fb.textContent = "Focus";
		fb.title = "Re-root the timeline at this call";
		fb.addEventListener("click", () => focusFrame(k));
		head.append(fb, " ");
	} else {
		node = G.selected;
		b.textContent = st.nodeName(node);
		head.append(b, " ");
	}
	if (node < st.symName.length) {
		const open = document.createElement("button");
		open.type = "button";
		open.textContent = "Page tab";
		open.addEventListener("click", () => jumpTo(st.symStart[node]));
		head.append(open);
	}
	el.append(head);
	el.dataset.node = st.nodeName(node);
	if (G.view === "timeline") {
		const sw = sandwich(st, node);
		const list = (title, items) => {
			const d = document.createElement("div");
			d.className = "glist";
			const t = document.createElement("b");
			t.textContent = title + " ";
			d.append(t);
			for (const [id, v] of items.slice(0, 12)) {
				const btn = document.createElement("span");
				btn.className = "mono";
				btn.textContent = st.nodeName(id) + " \u00d7" + v.count + " (" + num(v.incl) + ")  ";
				d.append(btn);
			}
			if (!items.length)
				d.append("none");
			else if (items.length > 12)
				d.append("and " + (items.length - 12) + " more");
			el.append(d);
		};
		list("callers", sw.callers);
		list("callees", sw.callees);
		el.dataset.callers = sw.callers.length;
		el.dataset.callees = sw.callees.length;
	}
}

function graphData() {
	const st = G.stepper;
	const n = st.symName.length;
	const tree = buildTree(st);
	const plain = (t) => ({ name: t.name, inclusive: t.incl, self: t.self, calls: t.calls,
		children: [...t.kids.values()].sort((a, b) => b.incl - a.incl).map(plain) });
	return {
		kernel: image ? image.name : null,
		note: "Recorded while stepping (v86 interpreter); runs at full JIT speed are not recorded. Calls are " +
			"reconstructed from symbol transitions: a jump to the first byte of a symbol is a call, a jump into " +
			"the middle of a symbol on the stack a return, an entry stub an interrupt.",
		steps: st.steps,
		entry_step: runStart(),
		nodes: st.nodeOrder.map((id) => ({
			id, name: st.nodeName(id), paddr: id < n ? st.symStart[id] : null,
			size: id < n ? st.symEnd[id] - st.symStart[id] : null,
			instructions: st.nodeSteps[id], first_step: st.nodeFirst[id],
		})),
		edges: [...st.edges.values()].map((e) => ({
			from: e.from, to: e.to, from_name: st.nodeName(e.from), to_name: st.nodeName(e.to),
			count: e.count, calls: e.calls, first_step: e.first,
		})),
		call_tree: plain(tree),
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
	// Position along the time axis and its length (transposed: vertical).
	const axis = (x, y) => (G.transpose ? [y - MINI - 6, c.clientHeight - MINI - 24] : [x, c.clientWidth]);
	const zoomAt = (x, y, f) => {
		if (G.view !== "timeline")
			return;
		const [px, w] = axis(x, y);
		const t = G.x0 + px / w * (G.x1 - G.x0);
		const span = Math.max(20, Math.min((G.x1 - G.x0) / f, 1e12));
		G.x0 = t - (px / w) * span;
		G.x1 = G.x0 + span;
		G.userMoved = true;
		G.dirty = true;
	};
	c.addEventListener("pointerdown", (ev) => {
		c.setPointerCapture(ev.pointerId);
		G.pointers.set(ev.pointerId, local(ev));
		G.press = G.pointers.size === 1 ? { at: local(ev), moved: false } : null;
	});
	c.addEventListener("pointermove", (ev) => {
		if (!G.pointers.has(ev.pointerId))
			return;
		const [x, y] = local(ev);
		const [px, py] = G.pointers.get(ev.pointerId);
		if (G.pointers.size === 1 && G.view === "timeline") {
			if (G.press && G.press.at[1] < MINI) { // drag in the minimap: move the window there
				const st = G.stepper, t0 = runStart(), t1 = Math.max(t0 + 1, st.steps);
				const span = G.x1 - G.x0, t = t0 + x / c.clientWidth * (t1 - t0);
				G.x0 = t - span / 2;
				G.x1 = t + span / 2;
			} else if (G.transpose) {
				const dt = (y - py) / (c.clientHeight - MINI - 24) * (G.x1 - G.x0);
				G.x0 -= dt;
				G.x1 -= dt;
				G.xOff = Math.max(0, G.xOff - (x - px));
			} else {
				const dt = (x - px) / c.clientWidth * (G.x1 - G.x0);
				G.x0 -= dt;
				G.x1 -= dt;
				G.yOff = Math.max(0, G.yOff + (G.flame ? 1 : -1) * (y - py));
			}
			if (G.press && Math.hypot(x - G.press.at[0], y - G.press.at[1]) > 5) {
				G.press.moved = true;
				if (G.follow) { // a manual pan stops following until "follow" is ticked again
					G.follow = false;
					$("graph_follow").checked = false;
				}
			}
			G.userMoved = true;
			G.dirty = true;
		} else if (G.pointers.size === 2) {
			const [other] = [...G.pointers].filter(([k]) => k !== ev.pointerId).map(([, v]) => v);
			const i = G.transpose ? 1 : 0, cur = [x, y], prev = [px, py];
			const d0 = Math.abs(prev[i] - other[i]), d1 = Math.abs(cur[i] - other[i]);
			if (d0 > 10)
				zoomAt((x + other[0]) / 2, (y + other[1]) / 2, d1 / d0);
			if (G.press)
				G.press.moved = true;
		}
		G.pointers.set(ev.pointerId, [x, y]);
	});
	const up = (ev) => {
		if (G.press && !G.press.moved && G.pointers.size === 1) {
			const [x, y] = local(ev);
			if (G.view === "timeline")
				G.selected = timelineFrameAt(x, y);
			else if (G.view === "top") {
				const hit = G.boxes.find(([, bx, by, bw, bh]) => x >= bx && x <= bx + bw && y >= by && y <= by + bh);
				G.selected = hit ? hit[0] : -1;
			}
			G.dirty = true;
			renderGraphInfo();
		}
		G.pointers.delete(ev.pointerId);
		G.press = null;
	};
	c.addEventListener("pointerup", up);
	c.addEventListener("pointercancel", (ev) => { G.pointers.delete(ev.pointerId); G.press = null; });
	c.addEventListener("wheel", (ev) => {
		if (G.view !== "timeline")
			return;
		ev.preventDefault();
		zoomAt(...local(ev), Math.exp(-ev.deltaY * 0.002));
	}, { passive: false });
	for (const v of ["timeline", "tree", "top"])
		$("gview_" + v).addEventListener("click", () => {
			G.view = v;
			G.selected = -1;
			G.dirty = true;
			renderGraphInfo();
			graphTick();
		});
	$("graph_fit").addEventListener("click", () => { G.userMoved = false; G.yOff = 0; G.xOff = 0; G.dirty = true; graphTick(); });
	c.addEventListener("dblclick", (ev) => {
		if (G.view !== "timeline")
			return;
		const k = timelineFrameAt(...local(ev));
		if (k >= 0)
			focusFrame(k);
	});
	const toggle = (id, key) => $(id).addEventListener("click", () => {
		G[key] = !G[key];
		$(id).setAttribute("aria-pressed", G[key]);
		G.userMoved = false;
		G.yOff = 0;
		G.xOff = 0;
		G.dirty = true;
		graphTick();
	});
	toggle("graph_transpose", "transpose");
	toggle("graph_flame", "flame");
	$("graph_chains").addEventListener("change", () => { G.chains = $("graph_chains").checked; G.dirty = true; graphTick(); });
	$("graph_depth").addEventListener("input", () => {
		G.maxDepth = Number($("graph_depth").value);
		$("graph_depth_v").textContent = G.maxDepth ? String(G.maxDepth) : "all";
		G.dirty = true;
		graphTick();
	});
	$("graph_follow").addEventListener("change", () => { G.follow = $("graph_follow").checked; });
	$("graph_search").addEventListener("input", () => { G.dirty = true; graphTick(); });
	$("graph_json").addEventListener("click", () => {
		if (G.stepper)
			download((image ? image.name : "kernel") + "-calls.json", JSON.stringify(graphData()), "application/json");
	});
	$("graph_dot").addEventListener("click", () => {
		if (G.stepper)
			download((image ? image.name : "kernel") + "-symbol-graph.dot", graphDot(), "text/vnd.graphviz");
	});
	window.addEventListener("resize", () => { G.dirty = true; });
	$("graph_fold").checked = G.fold;
	$("graph_fold").addEventListener("change", () => {
		G.fold = $("graph_fold").checked;
		G.selected = -1;
		G.dirty = true;
		graphTick();
		renderGraphInfo();
	});
}
