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
// Here: the timeline's drawing and what the views share (status, ticks,
// the info box, exports). The tab's state and data are in graphdata.js,
// the timeline's layout in graphlayout.js, tree and top in calltree.js,
// moving around in graphnav.js, the controls in graphinit.js.

import { realNow } from "./clock.js";
import { $, hex, num } from "./util.js";
import { runState, tab, image, api } from "./page.js";
import { frStartAt, frEndAt, frSmallAt } from "./frames.js";
import { STEP_CONST } from "./stepper.js";
import { dbgHas, dbgTitle } from "./dbginfo.js";
import { srcChainNodes } from "./source.js";
import { ffInfo } from "./fastforward.js";
import { fnDetailEl } from "./fnfacts.js";
import { G, runStart, fEnd, recTo, MINI, frameColour, ROW, taskNames, SWITCH, taskInfo, frameCounts,
	buildTree } from "./graphdata.js";
import { canvasSize, timelineLayout, layoutDeepest, isUnder, frameLabel, fitLabel } from "./graphlayout.js";
import { drawTree, drawTop } from "./calltree.js";
import { sandwich } from "./graphnav.js";

// Private consts (the layout loops read them per frame; see stepper.js).
const { FR_SIZE, FR_BIG, FR_BIG16 } = STEP_CONST;

function graphReset(st) {
	G.stepper = st;
	G.selected = -1;
	G.selSwitch = -1;
	G.taskNames = null;
	G.taskInfo = null;
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
	const line = css.getPropertyValue("--line").trim();
	const bg = css.getPropertyValue("--bg").trim();
	const L = timelineLayout(st);
	const focus = G.focus >= 0 && G.focus < st.fr.n ? G.focus : -1;
	const t0 = focus >= 0 ? st.fr.start(focus) : runStart();
	const t1 = Math.max(t0 + 1, focus >= 0 ? fEnd(st, focus) : recTo(st));
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
	// Minimap strip: the whole run (or the focused frame), shallow rows.
	const msx = (t) => (t - t0) / (t1 - t0) * w;
	// Calls narrower than a pixel mark their pixel column instead of being
	// dropped, so dense stretches of tiny calls do not look empty.
	const miniCol = [];
	for (let r = 0; r <= 5; r++)
		miniCol.push(new Int32Array(Math.ceil(w) + 1).fill(-1));
	const now = recTo(st), fr = st.fr, chunks = fr.chunkCount();
	for (let c = 0; c < chunks; c++) {
		const base = fr.baseOf(c), ds = fr.startsRaw(c), de = fr.endsRaw(c);
		for (let i = 0, k = c * FR_SIZE, len = fr.chunkLen(c); i < len; i++, k++) {
			const r = L.row[k] - rowOff;
			if (r < 0 || r > 5 || !L.isHead[k] || (focus >= 0 && !isUnder(st, k, focus)))
				continue;
			const s = frStartAt(fr, base, ds, i, k), e = frEndAt(fr, de, i, k, s);
			const a = Math.max(s, t0), b = Math.min(e < 0 ? now : e, t1);
			if (b <= a)
				continue;
			const x = msx(a), ww = msx(b) - x;
			if (ww < 1) {
				const col = Math.min(Math.floor(x + ww / 2), miniCol[r].length - 1);
				if (col >= 0)
					miniCol[r][col] = k;
				continue;
			}
			ctx.fillStyle = frameColour(st, k);
			ctx.fillRect(x, 2 + r * 3.5, ww, 3);
		}
	}
	for (let r = 0; r <= 5; r++)
		for (let col = 0; col < miniCol[r].length; col++)
			if (miniCol[r][col] >= 0) {
				ctx.fillStyle = frameColour(st, miniCol[r][col]);
				ctx.fillRect(col, 2 + r * 3.5, 1, 3);
			}
	ctx.strokeStyle = fg;
	ctx.lineWidth = 1;
	ctx.strokeRect(msx(G.x0) + 0.5, 0.5, Math.max(2, msx(G.x1) - msx(G.x0)), MINI - 2);
	ctx.fillStyle = line;
	ctx.fillRect(0, MINI, w, 1);
	// Geometry: time runs along `along` pixels; rows across.
	const COL = 120;
	// A strip above the rows for the task-switch labels (not transposed).
	const swStrip = st.swStep && st.swStep.length && !G.transpose ? 13 : 0;
	const areaTop = MINI + 6 + swStrip, areaH = h - areaTop - 18;
	G.areaTop = areaTop;
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
	let drawn = 0, labelled = 0, matches = 0, bmarked = 0;
	const labels = [], more = [];
	// Per visible row: pixel columns (along the time axis) covered by calls.
	// Calls narrower than a pixel are merged per column: the column is drawn
	// in the colour of the call covering most of it, more opaque the more
	// of it is covered, so the outline is the same at every zoom level.
	const AL = Math.ceil(along) + 1;
	const rowsCov = new Map(); // r -> { cov, best, frame, n, t0, t1, full }
	const rowCov = (r) => {
		let o = rowsCov.get(r);
		if (!o) {
			o = { cov: new Float32Array(AL), best: new Float32Array(AL), frame: new Int32Array(AL).fill(-1),
				n: new Uint32Array(AL), t0: new Float64Array(AL).fill(Infinity), t1: new Float64Array(AL), full: new Uint8Array(AL) };
			rowsCov.set(r, o);
		}
		return o;
	};
	// Every frame passes here, so the loop calls no function for most of
	// them (this function is too big for V8 to inline tp, rect, rowCov or
	// frStartAt into it): start and end decoded in place (as frStartAt and
	// frEndAt do), tp and rect worked out inline, and the row's coverage
	// looked up in an array.
	const row = L.row, isHead = L.isHead, x0 = G.x0, x1 = G.x1, tr = G.transpose, flame = G.flame, xOff = G.xOff, yOff = G.yOff;
	const deepest = cap < Infinity ? layoutDeepest(st, L) : null;
	const covOf = [];
	for (let c = 0; c < chunks; c++) {
		const base = fr.baseOf(c), ds = fr.startsRaw(c), de = fr.endsRaw(c);
		for (let i = 0, k = c * FR_SIZE, len = fr.chunkLen(c); i < len; i++, k++) {
			if (!isHead[k] || row[k] < 0)
				continue;
			const r = row[k] - rowOff;
			if (r < 0 || r > cap)
				continue;
			const a = ds[i] !== FR_BIG ? base + ds[i] : fr.start(k), e = de[i];
			const b = e === 0 ? now : e !== FR_BIG ? a + e - 1 : fr.end(k);
			if (b < x0 || a > x1)
				continue;
			if (focus >= 0 && !isUnder(st, k, focus))
				continue;
			const p0 = Math.max(0, (a - x0) / span * along), p1 = Math.min(along, (b - x0) / span * along);
			// rect(a, b, r)
			const x = tr ? r * COL - xOff : p0, rw = tr ? COL - 1 : p1 - p0, rh = tr ? p1 - p0 : ROW - 1;
			const y = tr ? areaTop + p0 : flame ? areaTop + areaH - (r + 1) * ROW + yOff : areaTop + r * ROW - yOff;
			if (y + rh < areaTop || y > areaTop + areaH || x + rw < 0 || x > w)
				continue;
			if (p1 - p0 < 1) {
				// Every column it touches (one or two), counted once.
				const o = covOf[r] || (covOf[r] = rowCov(r)), mid = Math.min(AL - 1, Math.max(0, Math.floor((p0 + p1) / 2)));
				const c0 = Math.min(AL - 1, Math.max(0, Math.floor(p0))), c1 = Math.min(AL - 1, Math.max(c0, Math.ceil(p1) - 1));
				for (let col = c0; col <= c1; col++) {
					const ov = Math.max(0, Math.min(p1, col + 1) - Math.max(p0, col));
					o.cov[col] += ov;
					if (col === mid)
						o.n[col]++;
					if (ov >= o.best[col] || o.frame[col] < 0) {
						o.best[col] = ov;
						o.frame[col] = k;
					}
					if (a < o.t0[col]) o.t0[col] = a;
					if (b > o.t1[col]) o.t1[col] = b;
				}
				continue;
			}
			{
				const o = rowCov(r);
				for (let col = Math.floor(p0); col < Math.min(AL, Math.ceil(p1)); col++)
					o.full[col] = 1;
			}
			// A bookmarked function (K1): ★ before its name.
			const bm = st.fr.node(k) < st.symName.length && api.show.bmFnNames(st.symStart[st.fr.node(k)]);
			if (bm)
				bmarked++;
			const label = (bm ? "\u2605 " : "") + frameLabel(st, L, k);
			const hit = q && label.toLowerCase().includes(q);
			if (hit)
				matches++;
			ctx.fillStyle = frameColour(st, k);
			ctx.globalAlpha = q ? (hit ? 1 : 0.18) : st.fr.irq(k) ? 0.6 : 0.9;
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
			if (r === cap && deepest[k] > row[k])
				more.push([x, y, rw, rh, deepest[k] - row[k]]);
		}
	}
	// The merged sub-pixel columns, as runs of covered columns per row.
	G.mergedHits = [];
	G.cover = new Map();
	for (const [r, o] of rowsCov) {
		const covered = new Uint8Array(AL);
		let run = null;
		const flush = () => {
			if (!run)
				return;
			const [x, y, rw, rh] = rect(run.t0, run.t1, r);
			const [px, py, pw, ph] = G.transpose ? [x, areaTop + run.c0, rw, run.c1 - run.c0] : [run.c0, y, run.c1 - run.c0, rh];
			G.mergedHits.push([px, py, pw, ph, run.n, run.t0, run.t1]);
			run = null;
		};
		for (let col = 0; col < AL; col++) {
			covered[col] = o.full[col];
			if (o.frame[col] < 0) {
				flush();
				continue;
			}
			covered[col] = 1;
			const k = o.frame[col];
			const [x, y, rw, rh] = rect(o.t0[col], o.t1[col], r);
			ctx.fillStyle = frameColour(st, k);
			ctx.globalAlpha = q ? 0.18 : 0.35 + 0.6 * Math.min(1, o.cov[col]);
			if (G.transpose)
				ctx.fillRect(x, areaTop + col, Math.max(rw - 0.5, 0.5), 1);
			else
				ctx.fillRect(col, y, 1, Math.max(rh - 0.5, 0.5));
			ctx.globalAlpha = 1;
			if (!run)
				run = { c0: col, c1: col + 1, n: 0, t0: o.t0[col], t1: o.t1[col] };
			run.c1 = col + 1;
			run.n += o.n[col];
			run.t0 = Math.min(run.t0, o.t0[col]);
			run.t1 = Math.max(run.t1, o.t1[col]);
		}
		flush();
		G.cover.set(r, covered);
	}
	ctx.fillStyle = "#fff";
	for (const [, x, y, rw, rh, label] of labels) {
		const ly = G.transpose ? y + 7 : y + rh / 2;
		const text = fitLabel(ctx, label, rw - 6);
		if (text) {
			ctx.fillText(text, Math.max(x, 0) + 3, ly);
			labelled++;
		}
	}
	// Task switches: a thin line across all rows at each switch, with the
	// incoming task's name on top where it does not overlap the previous.
	G.switchHits = [];
	let markers = 0, swLabels = 0;
	if (st.swStep && focus < 0) {
		const names = taskNames(st);
		ctx.font = "10px system-ui, sans-serif";
		let lastEnd = -Infinity;
		const vis = [];
		for (let i = 0; i < st.swStep.length; i++)
			if (st.swStep[i] >= G.x0 && st.swStep[i] <= G.x1 && !(G.fold && st.swIrq[i]))
				vis.push(i);
		for (let j = 0; j < vis.length; j++) {
			const i = vis[j];
			const p = tp(st.swStep[i]);
			markers++;
			// Switches closer than 4 px to a neighbour get a tick only, so
			// a busy stretch does not hide the calls under a wall of lines.
			const dense = (j > 0 && p - tp(st.swStep[vis[j - 1]]) < 4) ||
				(j + 1 < vis.length && tp(st.swStep[vis[j + 1]]) - p < 4);
			ctx.fillStyle = SWITCH;
			ctx.globalAlpha = dense ? 0.8 : 0.55;
			if (G.transpose)
				ctx.fillRect(dense ? w - 6 : 0, areaTop + p, dense ? 6 : w, 1);
			else
				ctx.fillRect(p, areaTop - swStrip, 1, dense ? 4 : areaH + swStrip);
			ctx.globalAlpha = 1;
			const name = names[st.swTask[i]];
			const tw = ctx.measureText(name).width + 4;
			if (G.transpose ? p - lastEnd < 12 : p - lastEnd < 8)
				continue;
			const lx = G.transpose ? w - tw - 2 : p + 1, ly = G.transpose ? areaTop + p + 6 : areaTop - 7;
			if (!G.transpose && lx + tw > w)
				continue;
			ctx.fillStyle = bg;
			ctx.globalAlpha = 0.8;
			ctx.fillRect(lx, ly - 6, tw, 12);
			ctx.globalAlpha = 1;
			ctx.fillStyle = SWITCH;
			ctx.fillText(name, lx + 2, ly);
			G.switchHits.push([i, lx, ly - 6, tw, 12]);
			lastEnd = G.transpose ? p + 12 : p + tw;
			swLabels++;
		}
		ctx.font = "11px system-ui, sans-serif";
	}
	c.dataset.switchMarkers = markers;
	c.dataset.switchLabels = swLabels;
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
		for (let k = 0, n = st.fr.n; k < n; k++)
			if (L.isHead[k] && L.row[k] >= rowOff && st.fr.start(k) <= tm && fEnd(st, k) > tm &&
				(focus < 0 || isUnder(st, k, focus))) {
				const [, y] = rect(st.fr.start(k), fEnd(st, k), L.row[k] - rowOff);
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
	// Now (or the end of the run): a dashed line in the text colour with a
	// label, so it does not read as a call.
	if (st.steps >= G.x0 && st.steps <= G.x1) {
		const p = tp(st.steps);
		const end = st.end ? "end" : "now";
		ctx.strokeStyle = fg;
		ctx.lineWidth = 1;
		ctx.setLineDash([4, 3]);
		ctx.beginPath();
		if (G.transpose) {
			ctx.moveTo(0, areaTop + p + 0.5);
			ctx.lineTo(w, areaTop + p + 0.5);
		} else {
			ctx.moveTo(p + 0.5, MINI);
			ctx.lineTo(p + 0.5, h);
		}
		ctx.stroke();
		ctx.setLineDash([]);
		ctx.font = "bold 10px system-ui, sans-serif";
		const tw = ctx.measureText(end).width + 4;
		const lx = G.transpose ? 2 : Math.min(p + 2, w - tw), ly = G.transpose ? areaTop + p - 7 : h - 22;
		ctx.fillStyle = bg;
		ctx.fillRect(lx, ly - 6, tw, 12);
		ctx.fillStyle = fg;
		ctx.fillText(end, lx + 2, ly);
		ctx.font = "11px system-ui, sans-serif";
	}
	c.dataset.nowMarker = st.steps >= G.x0 && st.steps <= G.x1 ? "1" : "0";
	ctx.fillStyle = muted;
	ctx.fillText((G.x0 - runStart()).toLocaleString("en") + " – " + (G.x1 - runStart()).toLocaleString("en") +
		" instructions from the entry" + (G.transpose ? " (top to bottom)" : ""), 4, h - 8);
	c.dataset.drawn = drawn;
	c.dataset.labels = labelled;
	c.dataset.bmarks = bmarked;
	c.dataset.matches = matches;
	// For tests: is the running frame's row inside the drawing area?
	if (st.stack.length) {
		const cur = st.stack[st.stack.length - 1];
		const r = L.row[cur] - rowOff;
		if (L.row[cur] >= 0 && r >= 0) {
			const [x, y, rw, rh] = rect(st.fr.start(cur), recTo(st), Math.min(r, cap));
			c.dataset.curRow = r;
			c.dataset.curVisible = G.transpose ? (x >= 0 && x + rw <= w) : (y >= areaTop && y + rh <= areaTop + areaH);
		}
	}
	c.dataset.focus = focus >= 0 ? st.nodeName(st.fr.node(focus)) : "";
	renderCrumbs(st);
}

// Breadcrumb from the root to the focused frame; each entry re-focuses.
function renderCrumbs(st) {
	const el = $("graph_crumbs");
	const f = G.focus;
	const key = f + "," + (st ? st.fr.n : 0);
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
	for (let p = f; p >= 0; p = st.fr.parent(p))
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
		el.append(" › ", btn(st.nodeName(st.fr.node(k)), k));
}

function focusFrame(k) {
	G.focus = k;
	G.userMoved = false;
	G.yOff = 0;
	G.xOff = 0;
	G.dirty = true;
	graphTick();
}

// Arrow keys on the timeline: Up the caller, Down the widest drawn callee,
// Left and Right the next drawn box in the same row.
function timelineArrow(key) {
	const st = G.stepper, L = G.layout;
	if (!st || !L || !G.rects.length)
		return false;
	const drawn = new Map(G.rects.map((r) => [r[0], r]));
	let k = G.selected, next = -1;
	if (k < 0 || !drawn.has(k)) {
		// Nothing selected yet: the widest box of the top row.
		let best = null;
		for (const r of G.rects)
			if (!best || L.row[r[0]] < L.row[best[0]] || (L.row[r[0]] === L.row[best[0]] && r[3] > best[3]))
				best = r;
		next = best[0];
	} else if (key === "ArrowUp") {
		for (let p = st.fr.parent(k); p >= 0 && next < 0; p = st.fr.parent(p))
			if (drawn.has(p))
				next = p;
	} else if (key === "ArrowDown") {
		let best = null;
		for (const r of G.rects)
			if (r[0] !== k && isUnder(st, r[0], k) && (!best || L.row[r[0]] < L.row[best[0]] || (L.row[r[0]] === L.row[best[0]] && r[3] > best[3])))
				best = r;
		next = best ? best[0] : -1;
	} else {
		const [, x] = drawn.get(k), dir = key === "ArrowRight" ? 1 : -1;
		let best = null;
		for (const r of G.rects)
			if (L.row[r[0]] === L.row[k] && (r[1] - x) * dir > 0 && (!best || Math.abs(r[1] - x) < Math.abs(best[1] - x)))
				best = r;
		next = best ? best[0] : -1;
	}
	if (next < 0)
		return true;
	G.selected = next;
	G.selSwitch = -1;
	G.dirty = true;
	renderGraphInfo();
	graphTick();
	return true;
}

function graphRestore(v) {
	if (!v || v.st !== G.stepper)
		return;
	for (const k of ["view", "x0", "x1", "yOff", "xOff", "userMoved", "follow", "selected", "selSwitch", "focus", "flame", "transpose"])
		G[k] = v[k];
	$("graph_follow").checked = G.follow;
	$("graph_flame").setAttribute("aria-pressed", G.flame);
	$("graph_transpose").setAttribute("aria-pressed", G.transpose);
	G.dirty = true;
	renderGraphInfo();
	graphTick();
}

// ---- shared -----------------------------------------------------------------

// Max and 99th-percentile call depth (sampled for big runs).
function timelineDepth(st) {
	const n = st.fr.n;
	if (!n)
		return null;
	if (G.depthKey === n)
		return G.depthVal;
	const step = Math.max(1, Math.floor(n / 200000));
	const d = [];
	let max = 0;
	for (let k = 0; k < n; k += step)
		d.push(st.fr.depth(k));
	const fr = st.fr;
	for (let c = 0, chunks = fr.chunkCount(); c < chunks; c++) {
		const dd = fr.depthsRaw(c);
		for (let i = 0, k = c * FR_SIZE, len = fr.chunkLen(c); i < len; i++, k++)
			if (dd[i] > max || dd[i] === FR_BIG16)
				max = Math.max(max, frSmallAt(fr, dd, i, k, "depth"));
	}
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
	el.dataset.frames = st ? st.fr.n : 0;
	const depth = st ? timelineDepth(st) : null;
	el.textContent = !st ? "No stepper for this kernel." :
		num(st.fr.n - st.contFrames) + " calls" +
		(st.contFrames ? " (plus " + num(st.contFrames) + " continuations of calls resumed after a task switch, in " + st.taskCount + " tasks)" : "") +
		", " + nodes + " symbols, " + edges + " transitions recorded" +
		(depth ? "; call depth max " + depth.max + ", p99 " + depth.p99 + (st.espUnwind ? " (ESP unwinding)" : "") : "") +
		(runState === "jit" ? "; running at full JIT speed: not recorded" : "") +
		(st.framesFull ? "; the timeline stopped at " + num(st.framesFull) +
			" calls because the tab's JavaScript heap was 80% full (the counters keep counting)" : "") +
		(typeof ffInfo === "object" && ffInfo && ffInfo.reached && !ffInfo.runTo ?
			"; the first " + num(ffInfo.steps) + " instructions (BIOS, setup, decompressor) were fast-forwarded with the JIT, the timeline starts at " + ffInfo.name : "") + ".";
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
	if (!G.stepper || tab !== "calls")
		return;
	// The count too: code that loops inside one function makes no symbol
	// transition, and its open call, the now line and "follow" go on.
	const version = G.stepper.graphVersion + "," + G.stepper.steps;
	if (version !== G.version || G.dirty) {
		// The call tree is DOM: rebuild it at most every 2 s while running.
		if (G.view === "tree" && !G.dirty && realNow() - (G.treeAt || 0) < 2000)
			return;
		G.treeAt = realNow();
		G.version = version;
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

// Hover text of a timeline frame: its name, where it was called from and
// its entry, with DWARF file:line and inlined-at chains.
function frameTitle(st, k) {
	const node = st.fr.node(k);
	const name = st.nodeName(node);
	const p = st.fr.parent(k);
	const from = st.fr.from(k);
	return name + "\ncalled from " + (from >= 0 ? hex(from) + ": " + dbgTitle(from, p >= 0 ? st.nodeName(st.fr.node(p)) : null).replace(/\n/g, "\n  ") : "unknown") +
		(node < st.symName.length ? "\nentry " + hex(st.symStart[node]) + ": " + dbgTitle(st.symStart[node], name).replace(/\n/g, "\n  ") : "");
}

function renderGraphInfo() {
	const el = $("graph_info");
	el.textContent = "";
	const st = G.stepper;
	if (st && G.selected < 0 && G.selSwitch >= 0 && G.view === "timeline") {
		const i = G.selSwitch, info = taskInfo(st)[st.swTask[i]];
		el.textContent = "Task switch " + num(st.swStep[i] - runStart()) + " instructions from the entry, to task " +
			st.swTask[i] + ": " + info.name + (st.swIrq[i] ? " (preempted on an interrupt's return)" : "") + ". Name: " + info.how + ".";
		el.dataset.switch = info.name;
		delete el.dataset.node;
		return;
	}
	delete el.dataset.switch;
	if (st && G.selected < 0 && G.mergedInfo && G.view === "timeline") {
		el.textContent = G.mergedInfo + ".";
		G.mergedInfo = "";
		delete el.dataset.node;
		return;
	}
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
		node = st.fr.node(k);
		const c = frameCounts(st, k);
		b.textContent = st.nodeName(node);
		const path = [];
		for (let p = st.fr.parent(k); p >= 0 && path.length < 8; p = st.fr.parent(p))
			path.push(st.nodeName(st.fr.node(p)));
		head.append(b, " inclusive " + num(c.incl) + ", self " + num(c.self) + " instructions; steps " +
			num(st.fr.start(k) - runStart()) + "\u2013" + num(fEnd(st, k) - runStart()) + " from the entry" +
			(st.fr.irq(k) ? "; in an interrupt" : "") +
			(st.fr.cont(k) ? "; continues a call suspended by a task switch" : "") +
			(st.taskCount > 1 ? "; task " + st.fr.task(k) + " (" + taskNames(st)[st.fr.task(k)] + ")" : "") + ". Called from: " + (path.join(" \u2190 ") || "(top)") + " ");
		const fb = document.createElement("button");
		fb.type = "button";
		fb.id = "graph_focus";
		fb.textContent = "Focus";
		fb.title = "Re-root the timeline at this call";
		fb.addEventListener("click", () => focusFrame(k));
		head.append(fb, " ");
		el.dataset.from = st.fr.from(k);
	} else {
		node = G.selected;
		b.textContent = st.nodeName(node);
		head.append(b, " ");
	}
	if (node < st.symName.length) {
		const open = document.createElement("button");
		open.type = "button";
		open.textContent = "Page tab";
		open.addEventListener("click", () => api.show.jumpTo(st.symStart[node]));
		head.append(open, " ");
		const how = document.createElement("span");
		how.className = "note";
		how.id = "graph_open_how";
		how.textContent = "Double-click the box (double tap, or Enter) to open " + st.nodeName(node) + " in the Page tab and the Source panel.";
		head.append(how);
	}
	el.append(head);
	el.dataset.node = st.nodeName(node);
	// What DW_TAG_subprogram says about the function of this symbol.
	if (node < st.symName.length && dbgHas())
		el.append(fnDetailEl(st.symName[node], st.symStart[node], "graph", "graph_fn"));
	if (G.view === "timeline") {
		// Where the call came from and where it starts, from the DWARF.
		const k = G.selected;
		const src = document.createElement("div");
		src.className = "glist";
		src.id = "graph_src";
		const add = (label, pa, sym) => {
			const t = document.createElement("b");
			t.textContent = label;
			const s = document.createElement("span");
			s.className = "mono";
			if (pa >= 0)
				s.append(hex(pa) + " ", ...srcChainNodes(pa, sym));
			else
				s.textContent = "unknown";
			src.append(t, " ", s, " ");
		};
		const parent = st.fr.parent(k);
		add(st.fr.irq(k) && st.entryKind[node] ? "interrupted at" : "called from", st.fr.from(k),
			parent >= 0 ? st.nodeName(st.fr.node(parent)) : null);
		if (node < st.symName.length)
			add("entry", st.symStart[node], st.symName[node]);
		el.append(src);
	}
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
	const plain = (t) => ({ name: t.name, ...(t.task !== undefined ? { task: t.task } : {}), inclusive: t.incl, self: t.self, calls: t.calls,
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

export { graphReset, drawTimeline, timelineArrow, graphRestore, timelineDepth, graphTick, graphShow, frameTitle,
	renderGraphInfo, graphData, graphDot, download };
