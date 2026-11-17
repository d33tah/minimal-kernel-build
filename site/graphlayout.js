"use strict";

// The Calls timeline's layout (rows per frame, the visible window) and
// the canvas helpers the Calls views share.

import { frStartAt, frEndAt } from "./frames.js";
import { STEP_CONST } from "./stepper.js";
import { G, recTo } from "./graphdata.js";

// Private consts (the layout loops read them per frame; see stepper.js).
const { FR_SIZE, FR_IRQ, FR_BITS, FR_MASK } = STEP_CONST;

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
// Per frame: row (-1 hidden) and isHead (1: the frame starts its box; the
// others are in chain under their head); deepest (the deepest row below
// it) only once a depth cap asks for it (layoutDeepest). A row is at most
// the frame's depth, so 16 bits hold it unless a frame was deeper than that.
// The arrays are views of buffers kept from one layout to the next, with
// room to grow: while a run goes on with Calls open the layout is made
// again at every redraw, and new arrays each time held the old layout, the
// new one and the garbage of the ones before (6 bytes a frame each; now 3,
// and 2 more for deepest with a depth cap).
const layoutBuf = { row: null, isHead: null, deepest: null };

function layoutView(name, T, n) {
	let b = layoutBuf[name];
	if (!b || !(b instanceof T) || b.length < n) {
		b = new T(n + (n >> 3) + 4096);
		layoutBuf[name] = b;
	}
	return b.subarray(0, n);
}

function timelineLayout(st) {
	const fr = st.fr, n = fr.n;
	const key = n + "," + G.fold + "," + G.chains + "," + st.steps;
	if (G.layout && G.layout.key === key)
		return G.layout;
	G.layout = null; // its arrays are about to be written over
	const Row = fr.maxDepth < 0x7fff ? Int16Array : Int32Array;
	const now = recTo(st); // the end of open frames (fEnd)
	const hide = G.fold ? FR_IRQ : 0; // hiddenFrame: a flag bit
	const chunks = fr.chunkCount();
	// isHead while it is made: bits 0-1 the children shown, counted up to 2
	// (only "exactly one" matters), bit 2 the head flag.
	const row = layoutView("row", Row, n).fill(-1), isHead = layoutView("isHead", Uint8Array, n).fill(0);
	for (let c = 0, k = 0; c < chunks; c++) {
		const par = fr.parentsOf(c), fl = fr.flagsOf(c), len = fr.chunkLen(c);
		for (let i = 0; i < len; i++, k++) {
			const p = par[i];
			if (p >= 0 && (isHead[p] & 3) < 2 && !(fl[i] & hide))
				isHead[p]++;
		}
	}
	const chain = new Map(); // head -> names below it
	const chains = G.chains;
	for (let c = 0, k = 0; c < chunks; c++) {
		const par = fr.parentsOf(c), fl = fr.flagsOf(c), len = fr.chunkLen(c);
		const base = fr.baseOf(c), ds = fr.startsRaw(c), de = fr.endsRaw(c);
		for (let i = 0; i < len; i++, k++) {
			if (fl[i] & hide)
				continue;
			const p = par[i];
			if (p < 0 || row[p] < 0) {
				row[k] = 0;
				isHead[k] |= 4;
				continue;
			}
			if (chains && (isHead[p] & 3) === 1) {
				const pc = p >>> FR_BITS, pi = p & FR_MASK;
				const sp = frStartAt(fr, fr.baseOf(pc), fr.startsRaw(pc), pi, p), ep = frEndAt(fr, fr.endsRaw(pc), pi, p, sp);
				const sk = frStartAt(fr, base, ds, i, k), ek = frEndAt(fr, de, i, k, sk);
				const incl = (ep < 0 ? now : ep) - sp;
				const self = incl - fr.child(p) - (ep < 0 || ek < 0 ? (ek < 0 ? now : ek) - sk : 0);
				if (self <= Math.max(2, incl * 0.01)) {
					row[k] = row[p];
					// The head of p's box: up through the frames in its chain.
					let h = p;
					while (!(isHead[h] & 4))
						h = fr.parentOf(h);
					if (!chain.has(h))
						chain.set(h, []);
					chain.get(h).push(k);
					continue;
				}
			}
			row[k] = row[p] + 1;
			isHead[k] |= 4;
		}
	}
	for (let k = 0; k < n; k++)
		isHead[k] >>= 2;
	G.layout = { key, row, isHead, chain, deepest: null };
	return G.layout;
}

// L.deepest: per frame the deepest row of the frames below it.
function layoutDeepest(st, L) {
	if (L.deepest)
		return L.deepest;
	const fr = st.fr, row = L.row, n = row.length;
	const deepest = layoutView("deepest", row.constructor, n);
	deepest.set(row);
	for (let c = fr.chunkCount() - 1; c >= 0; c--) {
		const par = fr.parentsOf(c), k0 = c * FR_SIZE;
		for (let i = Math.min(fr.chunkLen(c), n - k0) - 1; i >= 0; i--) {
			const p = par[i], k = k0 + i;
			if (p >= 0 && row[k] >= 0 && deepest[k] > deepest[p])
				deepest[p] = deepest[k];
		}
	}
	L.deepest = deepest;
	return deepest;
}

function frameLabel(st, L, k) {
	const names = [st.nodeName(st.fr.node(k))];
	for (const c of L.chain.get(k) || [])
		names.push(st.nodeName(st.fr.node(c)));
	return names.join(" › ");
}

function isUnder(st, k, f) {
	for (let p = k; p >= 0; p = st.fr.parentOf(p))
		if (p === f)
			return true;
	return false;
}

export { canvasSize, fitLabel, timelineLayout, layoutDeepest, frameLabel, isUnder };
