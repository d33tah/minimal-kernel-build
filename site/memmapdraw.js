"use strict";

// The live memory map's drawing (memmap.js): cells, labels, animations,
// the hint and the text screen (MMAP's methods, spread into MMAP).

import { $ } from "./util.js";
import { MEV } from "./memevents.js";
import { MM_TOKENS, MM_N, MM_CELLS, MM_L_EMU, MM_L_DEV, MM_L_CPU, MM_L_RAM, MM_LAYERS, MM_LAYER_NAMES, mmBytes,
	MM_HINT_MS, MM_COLS, MM_ROWS, mmNum, MM_CGA, MM_CP437 } from "./memmaplayout.js";

const MM_DRAW = {
	// ---- drawing -------------------------------------------------------------

	readColours() {
		const cs = getComputedStyle(document.documentElement);
		const rgb = (name) => {
			const v = cs.getPropertyValue("--" + name).trim();
			const m = /^#([0-9a-f]{6})$/i.exec(v);
			if (m)
				return [parseInt(m[1].slice(0, 2), 16), parseInt(m[1].slice(2, 4), 16), parseInt(m[1].slice(4, 6), 16)];
			const p = /rgba?\(([^)]*)\)/.exec(v);
			return p ? p[1].split(",").slice(0, 3).map(Number) : [128, 128, 128];
		};
		const bg = rgb("bg");
		const dark = bg[0] + bg[1] + bg[2] < 384;
		this.colours = {
			dark, bg, fg: rgb("fg"), line: rgb("line"),
			kind: { ram: rgb("page-free"), reserved: rgb("r-hole"), mmio: rgb("r-vga"), rom: rgb("r-rom"), none: bg },
			layer: MM_TOKENS.map(rgb), hint: rgb("mm-hint"), key: dark + ":" + bg.join(),
		};
	},

	cellPx(d) {
		const s = this.scale;
		return [(this.cellX[d] + 0.5) * s, (this.cellY[d] + 0.5) * s];
	},

	draw() {
		const canvas = $("mm_map");
		const css = Math.max(128, Math.round(canvas.getBoundingClientRect().width)) || 512;
		const dpr = Math.min(2, window.devicePixelRatio || 1);
		const px = Math.round(css * dpr);
		if (canvas.width !== px) {
			canvas.width = canvas.height = px;
			this.borders = null;
		}
		const cs = getComputedStyle(document.documentElement);
		const key = cs.getPropertyValue("--bg").trim();
		if (!this.colours || this.colourKey !== key) {
			this.readColours();
			this.colourKey = key;
		}
		const C = this.colours, s = px / MM_N;
		this.scale = s;
		if (!this.img) {
			this.img = new ImageData(MM_N, MM_N);
			this.off = document.createElement("canvas");
			this.off.width = this.off.height = MM_N;
		}
		const data = this.img.data, h = this.heat, segs = this.segs;
		const kindRgb = segs.map((g) => C.kind[g.kind] || C.kind.none);
		for (let d = 0; d < MM_CELLS; d++) {
			const g = this.cellSeg[d], base = kindRgb[g];
			let r, gg, b;
			// region tint, stronger once touched
			const touched = this.touched[d];
			const a = segs[g].kind === "none" ? (this.cellX[d] + this.cellY[d]) & 2 ? 0.25 : 0.1 : touched ? 0.75 : 0.45;
			r = C.bg[0] + (base[0] - C.bg[0]) * a;
			gg = C.bg[1] + (base[1] - C.bg[1]) * a;
			b = C.bg[2] + (base[2] - C.bg[2]) * a;
			if (touched) {
				// a faint trail of who touched it last... first layer set
				const l = touched & 4 ? MM_L_EMU : touched & 2 ? MM_L_DEV : touched & 1 ? MM_L_CPU : MM_L_RAM;
				const lc = C.layer[l];
				r += (lc[0] - r) * 0.25;
				gg += (lc[1] - gg) * 0.25;
				b += (lc[2] - b) * 0.25;
			}
			let best = 0, bl = 0;
			for (let l = 0; l < MM_LAYERS; l++) {
				const v = h[l * MM_CELLS + d];
				if (v > best) {
					best = v;
					bl = l;
				}
			}
			if (best > 0) {
				const lc = C.layer[bl];
				r += (lc[0] - r) * best;
				gg += (lc[1] - gg) * best;
				b += (lc[2] - b) * best;
			}
			const o = (this.cellY[d] * MM_N + this.cellX[d]) * 4;
			data[o] = r;
			data[o + 1] = gg;
			data[o + 2] = b;
			data[o + 3] = 255;
		}
		this.off.getContext("2d").putImageData(this.img, 0, 0);
		const ctx = canvas.getContext("2d");
		ctx.imageSmoothingEnabled = false;
		ctx.drawImage(this.off, 0, 0, px, px);
		// region borders
		if (!this.borders) {
			const p = new Path2D();
			for (let y = 0; y < MM_N; y++) {
				for (let x = 0; x < MM_N; x++) {
					const g = this.cellSeg[this.xyCell[y * MM_N + x]];
					if (x + 1 < MM_N && this.cellSeg[this.xyCell[y * MM_N + x + 1]] !== g) {
						p.moveTo((x + 1) * s, y * s);
						p.lineTo((x + 1) * s, (y + 1) * s);
					}
					if (y + 1 < MM_N && this.cellSeg[this.xyCell[(y + 1) * MM_N + x]] !== g) {
						p.moveTo(x * s, (y + 1) * s);
						p.lineTo((x + 1) * s, (y + 1) * s);
					}
				}
			}
			this.borders = p;
		}
		ctx.lineWidth = Math.max(1, dpr);
		ctx.strokeStyle = "rgb(" + C.fg.join() + ",0.45)";
		ctx.stroke(this.borders);
		this.drawLabels(ctx, dpr);
		this.drawAnims(ctx, dpr);
		if (this.hl)
			this.drawHint(ctx, dpr);
		// the CPU marker: the cell of the code of the last CPU event
		if (this.cpuCell >= 0) {
			const [x, y] = this.cellPx(this.cpuCell);
			ctx.strokeStyle = "rgb(" + C.layer[MM_L_CPU].join() + ")";
			ctx.lineWidth = 2 * dpr;
			ctx.beginPath();
			ctx.arc(x, y, 4 * dpr, 0, 2 * Math.PI);
			ctx.stroke();
		}
		for (const [d, dash] of [[this.pick, []], [document.activeElement === canvas ? this.cursor : -1, [3 * dpr, 2 * dpr]]]) {
			if (d < 0)
				continue;
			ctx.setLineDash(dash);
			ctx.strokeStyle = "rgb(" + C.fg.join() + ")";
			ctx.lineWidth = 2 * dpr;
			ctx.strokeRect(this.cellX[d] * s - dpr, this.cellY[d] * s - dpr, s + 2 * dpr, s + 2 * dpr);
			ctx.setLineDash([]);
		}
	},

	// A label centred at x, its first baseline at y, wrapped at spaces into
	// lines of at most about 45% of the map's width (never cut short); with
	// taken (the boxes drawn so far), not drawn where it would cover another
	// one. labelsDrawn keeps what was drawn (the tests read it).
	label(ctx, text, x, y, dpr, colour, taken) {
		const C = this.colours, lh = 12 * dpr;
		ctx.font = Math.round(10 * dpr) + "px ui-sans-serif, system-ui, sans-serif";
		const W = ctx.canvas.width, H = ctx.canvas.height, maxW = Math.max(110 * dpr, W * 0.45);
		const lines = [];
		for (const word of text.split(" ")) {
			const longer = lines.length ? lines[lines.length - 1] + " " + word : "";
			if (lines.length && ctx.measureText(longer).width <= maxW)
				lines[lines.length - 1] = longer;
			else
				lines.push(word);
		}
		const widths = lines.map((l) => ctx.measureText(l).width), w = Math.max(...widths);
		const lx = Math.max(2 * dpr, Math.min(W - w - 2 * dpr, x - w / 2));
		const ly = Math.max(9 * dpr, Math.min(H - 3 * dpr - (lines.length - 1) * lh, y));
		const box = [lx - 2 * dpr, ly - 9 * dpr, lx + w + 2 * dpr, ly + 3 * dpr + (lines.length - 1) * lh];
		if (taken) {
			if (taken.some((b) => b[0] < box[2] && box[0] < b[2] && b[1] < box[3] && box[1] < b[3]))
				return;
			taken.push(box);
		}
		ctx.fillStyle = "rgb(" + C.bg.join() + ",0.8)";
		ctx.fillRect(box[0], box[1], box[2] - box[0], box[3] - box[1]);
		ctx.fillStyle = colour || "rgb(" + C.fg.join() + ")";
		lines.forEach((l, k) => ctx.fillText(l, lx + (w - widths[k]) / 2, ly + k * lh));
		if (this.labelsDrawn)
			this.labelsDrawn.push({ text, lines, left: box[0] / dpr, right: box[2] / dpr, width: W / dpr });
	},

	drawLabels(ctx, dpr) {
		const minCells = ctx.canvas.width / dpr < 420 ? 300 : 100, taken = [];
		this.labelsDrawn = [];
		// first touch: the newest three stories as callouts, newest on top;
		// region names only where they leave room
		if (this.firstOnly) {
			for (const e of this.story.slice(-3).reverse()) {
				const [x, y] = this.cellPx(e.cell);
				ctx.globalAlpha = Math.max(0.35, 1 - (this.t - e.t) / 6000);
				this.label(ctx, MM_LAYER_NAMES[e.layer].split(" (")[0] + ": " + MEV.nameOf(e.region).name, x, y - 8 * dpr, dpr,
					"rgb(" + this.colours.layer[e.layer].join() + ")", taken);
				ctx.globalAlpha = 1;
			}
		}
		for (const g of [...this.segs].sort((a, b) => b.cells - a.cells)) {
			if (g.cells < minCells)
				continue;
			const r = MEV.nameOf(g.region);
			const name = g.kind === "none" ? "no standard name" : r.name.replace(/, before any E820 answer described it/, "");
			const [x, y] = this.cellPx(g.centre);
			this.label(ctx, name, x, y + 4 * dpr, dpr, null, taken);
		}
	},

	drawAnims(ctx, dpr) {
		const C = this.colours, s = this.scale, W = ctx.canvas.width;
		for (const a of this.anims) {
			const p = Math.min(1, (this.t - a.t0) / a.dur);
			if (a.type === "drop") {
				// the emulator pours bytes in along the curve, from above
				const upto = a.c0 + Math.floor((a.c1 - a.c0 + 1) * Math.min(1, p * 1.6));
				const lc = C.layer[MM_L_EMU];
				ctx.fillStyle = "rgb(" + lc.join() + "," + (1 - p * 0.6) + ")";
				for (let d = a.c0; d < upto; d++)
					ctx.fillRect(this.cellX[d] * s, this.cellY[d] * s, s, s);
				const [x, y] = this.cellPx(Math.min(a.c1, upto));
				ctx.strokeStyle = "rgb(" + lc.join() + "," + (1 - p) + ")";
				ctx.lineWidth = 2 * dpr;
				ctx.beginPath();
				ctx.moveTo(x, 0);
				ctx.lineTo(x, y);
				ctx.stroke();
				if (p < 0.9) {
					const c = MEV.cols, k = a.i & (MEV.cap - 1), why = MEV.reasons[c.src[k]] || "emulator";
					this.label(ctx, why + ", " + mmBytes(c.size[k]), x, Math.max(14 * dpr, y - 10 * dpr), dpr, "rgb(" + lc.join() + ")");
				}
			} else {
				const lc = C.layer[a.layer];
				const [tx, ty] = this.cellPx(a.to);
				const [fx, fy] = a.from >= 0 ? this.cellPx(a.from) : [0, ty];
				const q = Math.min(1, p * 2), x = fx + (tx - fx) * q, y = fy + (ty - fy) * q;
				ctx.strokeStyle = "rgb(" + lc.join() + "," + (0.6 * (1 - p)) + ")";
				ctx.lineWidth = 1.5 * dpr;
				ctx.beginPath();
				ctx.moveTo(fx, fy);
				ctx.lineTo(x, y);
				ctx.stroke();
				ctx.fillStyle = "rgb(" + lc.join() + "," + (1 - p * 0.7) + ")";
				ctx.beginPath();
				ctx.arc(x, y, (2.5 + (q >= 1 ? 6 * (p - 0.5) : 0)) * dpr, 0, 2 * Math.PI);
				ctx.fill();
				if (a.edge && p < 0.5 && a.i >= MEV.first())
					this.label(ctx, MEV.event(a.i).source || "device", Math.min(W / 3, 40 * dpr), fy, dpr);
			}
		}
	},

	// A quest's hint (quests.js): open the map, bring it into view and let
	// the cells of the address ranges [lo, hi) blink for MM_HINT_MS, with a
	// label.
	hint(ranges, label) {
		const box = $("mm");
		if (!box.open)
			box.open = true; // its toggle event starts the map
		this.hl = { ranges, label, t0: -1, cells: 0 };
		this.dirtyAll = true;
		$("mm_map").scrollIntoView({ block: "center" });
		this.schedule();
	},

	drawHint(ctx, dpr) {
		const h = this.hl, C = this.colours, s = this.scale;
		if (h.t0 < 0)
			h.t0 = this.t;
		const age = this.t - h.t0;
		if (age > MM_HINT_MS || !this.segs.length) {
			this.hl = null;
			return;
		}
		const a = 0.45 + 0.35 * Math.sin(age / 160);
		ctx.fillStyle = "rgb(" + C.hint.join() + "," + a.toFixed(3) + ")";
		let cells = 0, first = -1;
		for (const [lo, hi] of h.ranges) {
			const c1 = this.cellOf(Math.min(2 ** 32 - 1, Math.max(lo, hi - 1)));
			for (let d = this.cellOf(lo); d <= c1; d++, cells++)
				ctx.fillRect(this.cellX[d] * s, this.cellY[d] * s, s, s);
			if (first < 0)
				first = Math.round((this.cellOf(lo) + c1) / 2);
		}
		h.cells = cells;
		if (first >= 0 && h.label) {
			const [x, y] = this.cellPx(first);
			this.label(ctx, h.label, x, y - 10 * dpr, dpr, "rgb(" + C.hint.join() + ")");
		}
	},

	drawText() {
		const canvas = $("mm_text");
		const cw = 8, chh = 16;
		if (canvas.width !== MM_COLS * cw) {
			canvas.width = MM_COLS * cw;
			canvas.height = MM_ROWS * chh;
		}
		if (this.textLast >= 0) {
			const row = Math.floor(this.textLast / MM_COLS);
			if (row < this.textTop || row >= this.textTop + MM_ROWS)
				this.textTop = Math.max(0, row - MM_ROWS + 1);
		}
		const ctx = canvas.getContext("2d");
		ctx.font = "14px ui-monospace, 'DejaVu Sans Mono', monospace";
		ctx.textBaseline = "top";
		const full = this.textTop !== this.textDrawnTop || this.textFull;
		this.textDrawnTop = this.textTop;
		this.textFull = false;
		let glow = false;
		for (let r = 0; r < MM_ROWS; r++) {
			for (let col = 0; col < MM_COLS; col++) {
				const ch = (this.textTop + r) * MM_COLS + col, o = ch * 2;
				if (o + 1 >= this.text.length)
					continue;
				// a cell is drawn again while it glows and once after
				const age = (this.t - this.textAge[ch]) * this.speed;
				if (!full && age > 1200 + 100 * this.speed)
					continue;
				glow = true;
				const code = this.text[o], attr = this.text[o + 1];
				ctx.fillStyle = MM_CGA[(attr >> 4) & 7];
				ctx.fillRect(col * cw, r * chh, cw, chh);
				if (code && code !== 0x20) {
					ctx.fillStyle = MM_CGA[attr & 15];
					ctx.fillText(MM_CP437[code], col * cw, r * chh + 1);
				}
				const lit = (this.t - this.textLit[ch]) * this.speed;
				if (lit < 1000) {
					ctx.fillStyle = "rgba(255, 216, 77," + (0.35 * (1 - lit / 1000)) + ")";
					ctx.fillRect(col * cw, r * chh, cw, chh);
				}
			}
		}
		this.textGlow = glow;
		this.textDirty = false;
		$("mm_text_cap").textContent = "Colour text memory 0xb8000 (IBM PC memory map), rows " + this.textTop + "-" + (this.textTop + MM_ROWS - 1) +
			": the characters as the guest writes them (CGA layout, code page 437)" + (this.textFrom > 0 ? ", over the screen at instruction " + mmNum(this.textFrom) : "") + ".";
	},

	drawKey() {
		const ul = $("mm_key");
		ul.replaceChildren(...MM_LAYER_NAMES.map((name, l) => {
			const li = document.createElement("li");
			const sw = document.createElement("span");
			sw.className = "sw mm-sw mm-l" + l;
			li.append(sw, " " + name);
			return li;
		}));
	},
};

export { MM_DRAW };
