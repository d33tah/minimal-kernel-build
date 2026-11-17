"use strict";

// The live memory map (memmap.js): its constants and Hilbert curve, the
// layout of the address space into segments, and applying the log of
// memory events to the cells (MMAP's methods, spread into MMAP).

import { MEV, MEV_M_BULK, MEV_M_WRITE, MEV_ISA_DMA, pciBdf } from "./memevents.js";

const MM_ORDER = 7, MM_N = 1 << MM_ORDER, MM_CELLS = MM_N * MM_N;
const MM_MIN_CELLS = 16, MM_GAP_CELLS = 24, MM_RAM_SHARE = 0.6;
const MM_L_CPU = 0, MM_L_DEV = 1, MM_L_EMU = 2, MM_L_RAM = 3, MM_LAYERS = 4;
const MM_LAYER_NAMES = ["CPU outside RAM (memory-mapped I/O)", "device in RAM (DMA)", "emulator (loader, restore, page edits)", "CPU write to a RAM page"];
const MM_TOKENS = ["mm-cpu", "mm-dev", "mm-emu", "mm-ram"];
const MM_TEXT_LO = 0xb8000, MM_TEXT_HI = 0xc0000, MM_COLS = 80, MM_ROWS = 25;
const MM_SAMPLE_CAP = 1 << 22; // page numbers kept from the CPU's RAM writes
const MM_KEY = "mkb-memmap-open";
const MM_HINT_MS = 8000; // a quest's hint blinks this long
// IBM PC code page 437
const MM_CP437 = " ☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼ !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~⌂" +
	"ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ";
// the CGA's 16 colours (IBM Color/Graphics Monitor Adapter)
const MM_CGA = ["#000000", "#0000aa", "#00aa00", "#00aaaa", "#aa0000", "#aa00aa", "#aa5500", "#aaaaaa",
	"#555555", "#5555ff", "#55ff55", "#55ffff", "#ff5555", "#ff55ff", "#ffff55", "#ffffff"];

// Hilbert curve: position d -> (x, y) on an n x n square (n a power of 2).
function mmHilbert(n, d) {
	let x = 0, y = 0;
	for (let s = 1, t = d; s < n; s *= 2, t = Math.floor(t / 4)) {
		const rx = 1 & (t / 2), ry = 1 & (t ^ rx);
		if (!ry) {
			if (rx) {
				x = s - 1 - x;
				y = s - 1 - y;
			}
			[x, y] = [y, x];
		}
		x += s * rx;
		y += s * ry;
	}
	return [x, y];
}

const mmHex = (n) => "0x" + (n >>> 0).toString(16);
const mmBytes = (n) => n >= 2 ** 30 && n % 2 ** 30 === 0 ? n / 2 ** 30 + " GiB" : n >= 2 ** 20 && n % 2 ** 20 === 0 ? n / 2 ** 20 + " MiB" :
	n >= 1024 && n % 1024 === 0 ? n / 1024 + " KiB" : n.toLocaleString("en-US") + " bytes";
const mmNum = (n) => n.toLocaleString("en-US");

const MM_LAYOUT = {
	// ---- layout --------------------------------------------------------------

	layout() {
		const ram = MEV.cpu.memory_size[0] >>> 0;
		if (this.activeRef === MEV.active && this.ramRef === ram && this.segs.length)
			return false;
		this.activeRef = MEV.active;
		this.ramRef = ram;
		const pts = new Set([0, ram, 2 ** 32]);
		for (const r of MEV.active) {
			pts.add(Math.min(r.lo, 2 ** 32));
			pts.add(Math.min(r.hi, 2 ** 32));
		}
		const edges = [...pts].sort((a, b) => a - b);
		const segs = [];
		for (let k = 0; k + 1 < edges.length; k++) {
			const lo = edges[k], hi = edges[k + 1];
			if (hi <= lo)
				continue;
			const id = MEV.findRegion(lo);
			const r = MEV.nameOf(id);
			const last = segs[segs.length - 1];
			if (last && last.region === id && last.hi === lo)
				last.hi = hi;
			else
				segs.push({ lo, hi, region: id, kind: r.known ? r.kind : "none" });
		}
		const key = segs.map((s) => s.lo + ":" + s.hi + ":" + s.region).join(",");
		if (key === this.segKey)
			return false;
		this.segKey = key;
		// cells: a minimum each (stretches no standard names: a few), then
		// RAM MM_RAM_SHARE of the rest by size, the other regions the
		// remainder by the square root of their size (a 16 MiB framebuffer
		// would dwarf 2 MiB of RAM), largest remainders
		let left = MM_CELLS, ramBytes = 0, otherW = 0;
		for (const s of segs) {
			s.cells = s.kind === "none" ? MM_GAP_CELLS : MM_MIN_CELLS;
			left -= s.cells;
			if (s.kind === "ram")
				ramBytes += s.hi - s.lo;
			else if (s.kind !== "none")
				otherW += Math.sqrt(s.hi - s.lo);
		}
		const ramLeft = otherW ? Math.floor(left * MM_RAM_SHARE) : left, otherLeft = left - ramLeft;
		const share = segs.map((s) => (s.kind === "none" ? 0 : s.kind === "ram" ? ramLeft * (s.hi - s.lo) / ramBytes : otherLeft * Math.sqrt(s.hi - s.lo) / otherW));
		let given = 0;
		segs.forEach((s, k) => {
			s.cells += Math.floor(share[k]);
			given += Math.floor(share[k]);
		});
		const order = segs.map((s, k) => k).sort((a, b) => (share[b] % 1) - (share[a] % 1));
		for (let k = 0; given < left; k++, given++)
			segs[order[k % order.length]].cells++;
		let c = 0;
		segs.forEach((s, k) => {
			s.c0 = c;
			c += s.cells;
			s.c1 = c;
			this.cellSeg.fill(k, s.c0, s.c1);
			let sx = 0, sy = 0;
			for (let d = s.c0; d < s.c1; d++) {
				sx += this.cellX[d];
				sy += this.cellY[d];
			}
			// the label's place: the segment's cell nearest its centroid
			const mx = sx / s.cells, my = sy / s.cells;
			let best = s.c0, bd = Infinity;
			for (let d = s.c0; d < s.c1; d++) {
				const dd = (this.cellX[d] - mx) ** 2 + (this.cellY[d] - my) ** 2;
				if (dd < bd) {
					bd = dd;
					best = d;
				}
			}
			s.centre = best;
		});
		this.segs = segs;
		this.borders = null;
		this.renderRegions();
		return true;
	},

	segOf(addr) {
		const s = this.segs;
		let lo = 0, hi = s.length - 1;
		while (lo < hi) {
			const mid = (lo + hi + 1) >> 1;
			if (s[mid].lo <= addr)
				lo = mid;
			else
				hi = mid - 1;
		}
		return lo;
	},

	cellOf(addr) {
		const s = this.segs[this.segOf(addr)];
		return s.c0 + Math.min(s.cells - 1, Math.floor((addr - s.lo) * s.cells / (s.hi - s.lo)));
	},

	// The addresses of cell d: [lo, hi)
	cellRange(d) {
		const s = this.segs[this.cellSeg[d]], k = d - s.c0, n = s.hi - s.lo;
		const lo = s.lo + Math.ceil(k * n / s.cells), hi = s.lo + Math.ceil((k + 1) * n / s.cells);
		return [lo, Math.max(hi, lo + 1)];
	},

	segName(s) {
		const r = MEV.nameOf(s.region);
		if (s.kind === "none")
			return "no standard names " + mmHex(s.lo) + "-" + mmHex(s.hi - 1) + " (" + mmBytes(s.hi - s.lo) + ")";
		return r.name + (r.std ? " (" + r.std + ")" : "");
	},

	// ---- applying the log ----------------------------------------------------

	reset() {
		this.heat.fill(0);
		this.touched.fill(0);
		this.story = [];
		this.storySeen = new Set();
		this.anims = [];
		this.idx = MEV.first();
		this.upto = -1;
		this.sampleAt = 0;
		this.cpuCell = -1;
		const b = this.textBase;
		this.textFrom = 0;
		this.text.fill(0);
		if (b && this.cursorPos() >= b.pos) {
			this.text.set(b.bytes.subarray(0, this.text.length));
			this.textFrom = b.pos;
		}
		this.textAge.fill(-1e9);
		this.textLit.fill(-1e9);
		this.textLast = -1;
		this.textDirty = true;
		this.textFull = true;
		this.dirtyAll = true;
		this.storyDirty = true;
	},

	// Where the map is: live, the log's end; else the slider's event.
	cursorPos() {
		if (this.live)
			return Infinity;
		const i = this.target;
		return i >= MEV.n ? Infinity : MEV.cols.pos[i & (MEV.cap - 1)];
	},

	// A first touch of a region by an initiator and source: the CPU running
	// code in some region, a device, the emulator for some reason.
	storyAdd(pos, layer, region, i, cell, source) {
		const key = region + ":" + layer + ":" + source;
		if (this.storySeen.has(key))
			return false;
		this.storySeen.add(key);
		this.story.push({ pos, layer, region, i, cell, source, t: this.t });
		this.storyDirty = true;
		return true;
	},

	light(c0, c1, layer, amount = 1) {
		const h = this.heat, base = layer * MM_CELLS, bit = 1 << layer;
		for (let d = c0; d <= c1; d++) {
			if (h[base + d] < amount)
				h[base + d] = amount;
			this.touched[d] |= bit;
		}
	},

	// Only first touch: the whole segment of cell d lights once.
	lightSeg(d, layer) {
		const g = this.segs[this.cellSeg[d]];
		this.light(g.c0, g.c1 - 1, layer);
	},

	// Apply events [idx, to) (and the samples before them); animate: pulses
	// for the newest ones, drops for all.
	apply(to, animate) {
		const c = MEV.cols, m = MEV.cap - 1;
		const n = to - this.idx;
		const animFrom = animate ? to - 48 : to;
		for (let i = Math.max(this.idx, MEV.first()); i < to; i++) {
			const k = i & m, meta = c.meta[k], ini = meta & 3, addr = c.addr[k], pos = c.pos[k];
			this.applySamples(pos);
			const bulk = meta & MEV_M_BULK, size = Math.max(1, c.size[k]);
			const end = Math.min(2 ** 32 - 1, addr + size - 1);
			const c0 = this.cellOf(addr), c1 = this.cellOf(end);
			const write = !!(meta & MEV_M_WRITE);
			const first = this.storyAdd(pos, ini, c.region[k], i, c0, ini !== MM_L_CPU ? c.src[k] : c.eipPhys[k] >= 0 ? MEV.regionOf(c.eipPhys[k] >>> 0) : -1);
			if (!this.firstOnly)
				this.light(c0, c1, ini, write || ini !== MM_L_CPU ? 1 : 0.6);
			else if (first)
				this.lightSeg(c0, ini);
			if (ini === MM_L_CPU && c.eipPhys[k] >= 0)
				this.cpuCell = this.cellOf(c.eipPhys[k] >>> 0);
			if (write && !bulk && addr >= MM_TEXT_LO && addr < MM_TEXT_HI && pos >= this.textFrom) {
				const v = c.val[k], vh = c.valHi[k];
				for (let b = 0; b < c.size[k] && addr + b < MM_TEXT_HI; b++)
					this.text[addr - MM_TEXT_LO + b] = (b < 4 ? v >>> 8 * b : vh >>> 8 * (b - 4)) & 0xff;
				const ch = (addr - MM_TEXT_LO) >> 1, code = this.text[ch * 2];
				// a character glows when painted; a clear (spaces) does not
				this.textAge[ch] = this.t;
				if (!(addr & 1) && code && code !== 0x20)
					this.textLit[ch] = this.t;
				this.textLast = ch;
				this.textDirty = true;
			}
			// The emulator's writes (the loader's drops) always: a slow
			// frame applies more than 48 events and they come first.
			if ((i >= animFrom || animate && ini === MM_L_EMU) && (!this.firstOnly || first))
				this.animate(i, ini, c0, c1, write, bulk);
		}
		this.idx = to;
		if (n > 0)
			this.dirtyAll = true;
	},

	applySamples(pos) {
		while (this.sampleAt < this.samples.length && this.samples[this.sampleAt].pos <= pos) {
			const s = this.samples[this.sampleAt++];
			for (const p of s.pages) {
				const a = p * 4096, c0 = this.cellOf(a), c1 = this.cellOf(a + 4095);
				const first = this.storyAdd(s.pos, MM_L_RAM, MEV.regionOf(a), -1, c0, -1);
				if (!this.firstOnly)
					this.light(c0, c1, MM_L_RAM, 0.8);
				else if (first)
					this.lightSeg(c0, MM_L_RAM);
			}
		}
	},

	animate(i, ini, c0, c1, write, bulk) {
		if (this.anims.length > 40)
			this.anims.shift();
		const c = MEV.cols, k = i & (MEV.cap - 1), dur = 900 / this.speed;
		if (ini === MM_L_EMU && c1 > c0) {
			this.anims.push({ type: "drop", c0, c1, t0: this.t, dur: Math.min(2400, 300 + (c1 - c0) * 2) / this.speed, i });
		} else if (ini === MM_L_CPU) {
			if (c.eipPhys[k] < 0)
				return;
			const from = this.cellOf(c.eipPhys[k] >>> 0);
			// one pulse per target segment at a time
			const seg = this.cellSeg[c0];
			if (this.anims.some((a) => a.type === "pulse" && a.seg === seg && this.t - a.t0 < dur / 3))
				return;
			this.anims.push({ type: "pulse", from, to: c0, seg, t0: this.t, dur, layer: ini, write, i });
		} else if (ini === MM_L_DEV) {
			const src = c.src[k];
			let from = -1;
			if (src >= 0 && src < MEV_ISA_DMA) {
				const tag = "PCI " + pciBdf(src) + " ";
				const s = this.segs.find((g) => MEV.nameOf(g.region).name.startsWith(tag));
				if (s)
					from = s.centre;
			}
			this.anims.push({ type: "pulse", from, to: c0, seg: this.cellSeg[c0], t0: this.t, dur, layer: ini, write, edge: from < 0, i });
		}
	},

	sample() {
		if (this.trackFrom < 0 || !this.trackable() || !MEV.st)
			return;
		const st = MEV.st, pos = st.steps;
		if (pos === this.trackFrom)
			return;
		const pages = (MEV.cpu.memory_size[0] >>> 12);
		const bits = new Uint8Array(MEV.cpu.wasm_memory.buffer, this.dirtyPtr, (pages + 7) >> 3);
		const out = [];
		for (let b = 0; b < bits.length; b++) {
			const v = bits[b];
			if (v)
				for (let j = 0; j < 8; j++)
					if (v >> j & 1)
						out.push(b * 8 + j);
		}
		this.dirtyPtr = MEV.ex.mev_track_begin();
		const from = this.trackFrom;
		this.trackFrom = pos;
		if (!out.length || pos < from)
			return;
		this.samples.push({ from, pos, pages: Uint32Array.from(out) });
		this.sampleN += out.length;
		while (this.sampleN > MM_SAMPLE_CAP && this.samples.length > 1) {
			this.sampleN -= this.samples.shift().pages.length;
			this.sampleAt = Math.max(0, this.sampleAt - 1);
		}
	},
};

export { MM_N, MM_CELLS, MM_L_CPU, MM_L_DEV, MM_L_EMU, MM_L_RAM, MM_LAYERS, MM_LAYER_NAMES, MM_TOKENS, MM_TEXT_LO,
	MM_TEXT_HI, MM_COLS, MM_ROWS, MM_KEY, MM_HINT_MS, MM_CP437, MM_CGA, mmHilbert, mmHex, mmBytes, mmNum, MM_LAYOUT };
