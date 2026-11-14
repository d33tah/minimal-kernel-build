"use strict";

// Live memory map (M2): the whole 32-bit physical address space on a
// Hilbert curve, with the memory events of memevents.js lighting it up.
//
// The address space is cut into segments at every edge of a region MEV
// knows (IBM PC map, E820 answers, PCI BARs, firmware tables,
// IA32_APIC_BASE) and at the end of RAM; each segment gets a run of cells
// along a Hilbert curve of order MM_ORDER (neighbouring addresses stay
// neighbours on the square), at least MM_MIN_CELLS so the 1 KiB interrupt
// vector table shows, the rest shared by size; stretches no standard names
// get a few cells each. Each event lights the cells of its addresses in its
// initiator's colour, then fades: the CPU outside RAM (a pulse from the
// cell of the code that did it), a device in RAM (from the device's own
// region when a BAR names it), the emulator (a drop, filled along the
// curve: the loader placing the kernel before the first instruction). The
// CPU's writes to RAM are not events (too many); the map samples which RAM
// pages the CPU wrote between two frames (v86.wasm's checking-mode bitmap:
// a write needs a TLB entry made for writing, mev_track_begin empties the
// TLB) and lights them in a lighter blue.
//
// The colour text region (IBM PC map: 0xB8000, the CGA layout of a
// character byte and an attribute byte, code page 437 glyphs, the CGA
// palette) is drawn beside the map from the writes in the log, so the
// characters appear as the guest paints them.
//
// Time: the map follows the log live; its slider moves through the log
// event by event (Replay plays it at the chosen speed), a step back
// (reverse.js) truncates the log and the map follows. A cell's events
// (click, or Enter on the keyboard cursor) go to their instruction (a step
// back) and to their Source line. "Only first touch": each region lights
// once per initiator and the story list says who touched it first.

const MM_ORDER = 7, MM_N = 1 << MM_ORDER, MM_CELLS = MM_N * MM_N;
const MM_MIN_CELLS = 16, MM_GAP_CELLS = 24, MM_RAM_SHARE = 0.6;
const MM_L_CPU = 0, MM_L_DEV = 1, MM_L_EMU = 2, MM_L_RAM = 3, MM_LAYERS = 4;
const MM_LAYER_NAMES = ["CPU outside RAM (memory-mapped I/O)", "device in RAM (DMA)", "emulator (loader, restore, page edits)", "CPU write to a RAM page"];
const MM_TOKENS = ["mm-cpu", "mm-dev", "mm-emu", "mm-ram"];
const MM_TEXT_LO = 0xb8000, MM_TEXT_HI = 0xc0000, MM_COLS = 80, MM_ROWS = 25;
const MM_SAMPLE_CAP = 1 << 22; // page numbers kept from the CPU's RAM writes
const MM_KEY = "mkb-memmap-open";
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

const MMAP = {
	open: false,
	cpu: null, gen: 0,
	segs: [], segKey: "", activeRef: null, ramRef: -1,
	cellSeg: new Int16Array(MM_CELLS),
	cellX: new Uint8Array(MM_CELLS), cellY: new Uint8Array(MM_CELLS), xyCell: new Uint16Array(MM_CELLS),
	heat: new Float32Array(MM_CELLS * MM_LAYERS),
	touched: new Uint8Array(MM_CELLS),
	idx: 0,          // MEV events applied: [first, idx)
	upto: -1,        // the position applied up to (samples)
	live: true,      // follows the log; else the slider holds idx
	playing: false,
	speed: 1,
	firstOnly: false,
	samples: [],     // the CPU's RAM writes: { from, pos, pages: Uint32Array }
	sampleN: 0,
	sampleAt: 0,     // samples applied
	trackFrom: -1,
	text: new Uint8Array(MM_TEXT_HI - MM_TEXT_LO),
	textAge: new Float32Array((MM_TEXT_HI - MM_TEXT_LO) >> 1), // last write (drawn again)
	textLit: new Float32Array((MM_TEXT_HI - MM_TEXT_LO) >> 1), // last character painted (glows)
	textTop: 0, textLast: -1, textBase: null, textDirty: true,
	story: [], storySeen: new Set(),
	anims: [],
	cursor: -1,      // keyboard cursor cell
	pick: -1,        // the cell whose events are shown
	cpuCell: -1,     // the code of the last CPU event
	t: 0, raf: 0, frames: 0, drawMs: 0,
	colours: null,

	init() {
		for (let d = 0; d < MM_CELLS; d++) {
			const [x, y] = mmHilbert(MM_N, d);
			this.cellX[d] = x;
			this.cellY[d] = y;
			this.xyCell[y * MM_N + x] = d;
		}
		const box = $("mm");
		const q = new URLSearchParams(location.search);
		let remembered = false;
		try {
			remembered = localStorage.getItem(MM_KEY) === "1";
		} catch (e) { /* no storage: closed */ }
		if (q.get("memmap") === "1" || (remembered && q.get("memmap") !== "0"))
			box.open = true;
		box.addEventListener("toggle", () => this.setOpen(box.open, true));
		$("mm_play").addEventListener("click", () => this.play(!this.playing));
		$("mm_live").addEventListener("click", () => this.goLive());
		$("mm_speed").addEventListener("change", () => {
			this.speed = Number($("mm_speed").value) || 1;
		});
		$("mm_first").addEventListener("change", () => {
			this.firstOnly = $("mm_first").checked;
			this.dirtyAll = true;
		});
		$("mm_time").addEventListener("input", () => this.scrub(Number($("mm_time").value)));
		const map = $("mm_map");
		map.addEventListener("click", (ev) => this.clickAt(ev));
		map.addEventListener("mousemove", (ev) => this.hover(ev));
		map.addEventListener("mouseleave", () => this.hover(null));
		map.addEventListener("keydown", (ev) => this.key(ev));
		map.addEventListener("focus", () => {
			if (this.cursor < 0)
				this.cursor = this.segs.length ? this.segs[0].c0 : 0;
			this.announce(this.cursor);
			this.dirtyAll = true;
		});
		map.addEventListener("blur", () => { this.dirtyAll = true; });
		document.addEventListener("visibilitychange", () => this.schedule());
		new MutationObserver(() => this.schedule()).observe($("panel_memory"), { attributes: true, attributeFilter: ["hidden"] });
		this.drawKey();
		this.setOpen(box.open, false);
	},

	setOpen(open, remember) {
		this.open = open;
		if (remember) {
			try {
				localStorage.setItem(MM_KEY, open ? "1" : "0");
			} catch (e) { /* no storage */ }
		}
		if (typeof MEV === "object")
			MEV.need("map", open);
		if (open)
			this.attach();
		else
			this.detach();
		this.schedule();
	},

	// The CPU's RAM writes: the checking mode's bitmap (not while the
	// checking mode itself uses it).
	trackable() {
		return !!(MEV.ex && MEV.ex.mev_track_begin && !MEV.check && !MEV.missing);
	},

	attach() {
		if (!MEV.cpu || MEV.missing)
			return;
		if (this.cpu !== MEV.cpu || this.gen !== MEV.gen || MEV.n < this.idx) {
			this.cpu = MEV.cpu;
			this.gen = MEV.gen;
			this.samples = [];
			this.sampleN = 0;
			this.textBase = null;
			this.reset();
		}
		if (this.trackable()) {
			this.dirtyPtr = MEV.ex.mev_track_begin();
			this.trackFrom = MEV.st ? MEV.st.steps : 0;
		}
		// opened after the log began: the screen as it is now is the base
		if (!this.textBase && MEV.st && MEV.st.steps > 0)
			this.textBase = { pos: MEV.st.steps, bytes: MEV.st.text.slice() };
	},

	detach() {
		if (this.trackFrom >= 0 && MEV.ex && MEV.ex.mev_track_end && !MEV.check)
			MEV.ex.mev_track_end();
		this.trackFrom = -1;
		this.playing = false;
	},

	visible() {
		return this.open && !$("panel_memory").hidden && document.visibilityState !== "hidden";
	},

	schedule() {
		if (this.raf || !this.visible())
			return;
		this.raf = requestAnimationFrame((t) => {
			this.raf = 0;
			this.frame(t);
			this.schedule();
		});
	},

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
	// and drops for the newest ones.
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
			if (i >= animFrom && (!this.firstOnly || first))
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

	// ---- time ----------------------------------------------------------------

	sync() {
		const st = MEV.st;
		// a new machine, a cleared log, or a step back that cut the log
		if (this.cpu !== MEV.cpu || this.gen !== MEV.gen || MEV.n < this.idx || (st && st.steps < this.trackFrom)) {
			const back = this.cpu === MEV.cpu && this.gen === MEV.gen;
			this.cpu = MEV.cpu;
			this.gen = MEV.gen;
			const now = st ? st.steps : 0;
			if (back) {
				this.samples = this.samples.filter((s) => s.pos <= now);
				this.sampleN = this.samples.reduce((a, s) => a + s.pages.length, 0);
				if (this.textBase && this.textBase.pos > now)
					this.textBase = now > 0 ? { pos: now, bytes: st.text.slice() } : null;
			} else {
				this.samples = [];
				this.sampleN = 0;
				this.textBase = null;
			}
			if (this.trackable()) {
				this.dirtyPtr = MEV.ex.mev_track_begin();
				this.trackFrom = now;
			}
			this.live = true;
			this.playing = false;
			this.reset();
		}
		if (this.layout())
			this.rewind();
	},

	// Apply the log again from its start to the cursor (after a relayout or
	// a jump of the slider), with the fading as it would be there.
	rewind() {
		const keepLive = this.live, target = this.live ? MEV.n : this.target;
		this.reset();
		this.apply(Math.max(this.idx, target), false);
		if (keepLive && MEV.st)
			this.applySamples(MEV.st.steps);
		// the last events glow, older ones faded
		this.heat.fill(0);
		const from = Math.max(MEV.first(), this.idx - 600);
		const c = MEV.cols, m = MEV.cap - 1;
		for (let i = from; i < this.idx; i++) {
			const k = i & m, size = Math.max(1, c.size[k]);
			if (this.firstOnly)
				break; // the regions stay lit (lightSeg)
			this.light(this.cellOf(c.addr[k]), this.cellOf(Math.min(2 ** 32 - 1, c.addr[k] + size - 1)), c.meta[k] & 3, 0.15 + 0.85 * (i - from) / Math.max(1, this.idx - from));
		}
		this.dirtyAll = true;
	},

	scrub(i) {
		this.live = false;
		this.playing = false;
		this.target = MEV.first() + i;
		this.rewind();
		this.status();
	},

	goLive() {
		this.live = true;
		this.playing = false;
		this.rewind();
		this.status();
	},

	play(on) {
		if (!on) {
			this.playing = false;
			this.status();
			return;
		}
		this.live = false;
		this.playing = true;
		this.target = MEV.first();
		this.playAt = MEV.first();
		this.reset();
		this.status();
	},

	frame(t) {
		// live, at most about 20 frames a second: each one takes time from
		// the machine (the JIT runs between frames); a replay runs smooth
		if (this.live && this.t && t - this.t < 45)
			return;
		const dt = this.t ? Math.min(100, t - this.t) : 16;
		if (this.t && t - this.t > 150)
			this.textFull = true; // a pause of the frames: glows ended unseen
		this.t = t;
		if (!MEV.cpu || MEV.missing || !MEV.cols) {
			this.status();
			return;
		}
		const t0 = realNow(); // clock.js: performance.now is the virtual clock
		this.sync();
		this.sample();
		if (this.live) {
			this.apply(MEV.n, true);
			if (MEV.st)
				this.applySamples(MEV.st.steps);
		} else if (this.playing) {
			const total = MEV.n - MEV.first();
			const rate = this.speed * Math.max(60, total / 20); // the whole log in 20 s at 1x
			this.playAt = Math.min(MEV.n, this.playAt + rate * dt / 1000);
			this.target = Math.floor(this.playAt);
			this.apply(this.target, true);
			if (this.target >= MEV.n) {
				this.live = true;
				this.playing = false;
			}
		}
		// fading: about a second at 1x
		const f = Math.exp(-dt * this.speed / 900);
		let hot = false;
		const h = this.heat;
		for (let j = 0; j < h.length; j++) {
			if (h[j] > 0.01) {
				h[j] *= f;
				hot = true;
			} else if (h[j]) {
				h[j] = 0;
			}
		}
		this.anims = this.anims.filter((a) => t - a.t0 < a.dur);
		if (hot || this.anims.length || this.dirtyAll)
			this.draw();
		if (this.textDirty || this.textGlow)
			this.drawText();
		if (this.storyDirty || this.factsShown !== MEV.facts.length || (this.linesPending && this.frames % 30 === 0))
			this.renderStory();
		this.dirtyAll = false;
		this.frames++;
		this.drawMs += realNow() - t0;
		if (this.frames % 10 === 1 || this.idx !== this.statusIdx) {
			this.statusIdx = this.idx;
			this.status();
		}
	},

	status() {
		const el = $("mm_status");
		const slider = $("mm_time");
		if (!MEV.cpu || MEV.missing || !MEV.cols) {
			el.textContent = MEV.missing ? "This v86 build reports no memory events." : "Boot a kernel to see its memory.";
			return;
		}
		const first = MEV.first(), total = MEV.n - first;
		slider.max = String(Math.max(0, total));
		const at = this.live ? total : Math.min(total, this.target - first);
		if (document.activeElement !== slider || this.playing)
			slider.value = String(at);
		const pos = at > 0 ? MEV.cols.pos[(first + at - 1) & (MEV.cap - 1)] : 0;
		slider.setAttribute("aria-valuetext", "event " + mmNum(at) + " of " + mmNum(total) + ", instruction " + mmNum(pos));
		$("mm_play").textContent = this.playing ? "Pause" : "Replay";
		$("mm_play").setAttribute("aria-pressed", this.playing ? "true" : "false");
		$("mm_live").disabled = this.live;
		el.textContent = (this.live ? "Live: " : this.playing ? "Replaying: " : "At ") + mmNum(at) + " of " + mmNum(total) + " events" +
			(at ? ", instruction " + mmNum(pos) : "") + (this.trackable() ? "" : MEV.check ? "; the checking mode uses the CPU's write bitmap, so RAM writes are not shown" : "") + ".";
	},

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
			layer: MM_TOKENS.map(rgb), key: dark + ":" + bg.join(),
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

	// A label centred at x, its baseline at y; with taken (the boxes drawn so
	// far), not drawn where it would cover another one.
	label(ctx, text, x, y, dpr, colour, taken) {
		const C = this.colours;
		ctx.font = Math.round(10 * dpr) + "px ui-sans-serif, system-ui, sans-serif";
		const w = ctx.measureText(text).width, W = ctx.canvas.width;
		const lx = Math.max(2 * dpr, Math.min(W - w - 2 * dpr, x - w / 2));
		if (taken) {
			const box = [lx - 2 * dpr, y - 9 * dpr, lx + w + 2 * dpr, y + 3 * dpr];
			if (taken.some((b) => b[0] < box[2] && box[0] < b[2] && b[1] < box[3] && box[1] < b[3]))
				return;
			taken.push(box);
		}
		ctx.fillStyle = "rgb(" + C.bg.join() + ",0.8)";
		ctx.fillRect(lx - 2 * dpr, y - 9 * dpr, w + 4 * dpr, 12 * dpr);
		ctx.fillStyle = colour || "rgb(" + C.fg.join() + ")";
		ctx.fillText(text, lx, y);
	},

	drawLabels(ctx, dpr) {
		const minCells = ctx.canvas.width / dpr < 420 ? 300 : 100, taken = [];
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
			this.label(ctx, name.length > 28 ? name.slice(0, 27) + "…" : name, x, y + 4 * dpr, dpr, null, taken);
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

	// ---- lists ---------------------------------------------------------------

	// The first touches and what the guest learned from the standards
	// (MEV.facts: E820 answers, PCI devices and BARs, firmware tables) up
	// to the cursor, in position order.
	renderStory() {
		this.storyDirty = false;
		this.linesPending = false;
		this.factsShown = MEV.facts.length;
		const ol = $("mm_story");
		const upto = this.cursorPos();
		const facts = MEV.facts.filter((f) => f.pos <= upto).map((f) => ({ pos: f.pos, fact: f.text }));
		const all = [...this.story, ...facts].sort((a, b) => a.pos - b.pos).slice(-300);
		const items = all.map((e) => {
			const li = document.createElement("li");
			if (e.fact) {
				const sw = document.createElement("span");
				sw.className = "mm-sw";
				li.append(sw, " " + mmNum(e.pos) + ": the guest learned " + e.fact, " ", this.goButton(e.pos));
				return li;
			}
			const sw = document.createElement("span");
			sw.className = "sw mm-sw mm-l" + e.layer;
			const who = e.layer === MM_L_CPU && e.source >= 0 ? "the CPU running code in " + this.regionText(e.source, false) :
				e.layer === MM_L_CPU ? "the CPU" : e.layer === MM_L_RAM ? "the CPU writing RAM" : e.layer === MM_L_DEV ? "a device" : "the emulator";
			li.append(sw, " " + mmNum(e.pos) + ": " + who + " first touched " + this.regionText(e.region, true));
			if (e.i >= 0 && e.i >= MEV.first())
				li.append(": ", ...this.eventWhere(MEV.event(e.i)));
			li.append(" ", this.goButton(e.pos));
			return li;
		});
		ol.replaceChildren(...items);
	},

	// A region's name, with its addresses when another region has the same
	// name (two E820 entries of usable RAM), and its standard.
	regionText(id, std) {
		const r = MEV.nameOf(id);
		const twin = MEV.active.some((o) => o.id !== id && o.name === r.name);
		return r.name + (twin && r.known ? " " + mmHex(r.lo) + "-" + mmHex(r.hi - 1) : "") + (std && r.std ? " (" + r.std + ")" : "");
	},

	renderRegions() {
		const ul = $("mm_regions");
		ul.replaceChildren(...this.segs.map((g) => {
			const li = document.createElement("li");
			const sw = document.createElement("span");
			sw.className = "sw mm-sw mm-k-" + g.kind;
			const b = document.createElement("button");
			b.type = "button";
			b.className = "symlink";
			b.textContent = mmHex(g.lo) + "-" + mmHex(g.hi - 1) + " " + this.segName(g);
			b.addEventListener("click", () => this.showCell(g.c0));
			li.append(sw, " ", b);
			return li;
		}));
	},

	// "EIP fn file:line" (a Source link) or the device, the emulator's reason
	eventWhere(e) {
		if (e.initiator !== "cpu")
			return [e.source || ""];
		const out = ["at EIP " + mmHex(e.eip)];
		const st = MEV.st;
		if (st && e.eipPhys >= 0) {
			const i = st.symbolOf(e.eipPhys);
			if (i >= 0)
				out.push(" " + st.symName[i]);
			// starts reading the DWARF of that code; drawn again when it is in
			const f = typeof dbgFrames === "function" ? dbgFrames(e.eipPhys) : null;
			if (f && f.state === "loading")
				this.linesPending = true;
			if (f && f.state === "ok" && f.frames[0].file)
				out.push(" ", srcLinkEl(f.frames[0].file, f.frames[0].line, null, e.eipPhys));
		}
		return out;
	},

	goButton(pos) {
		const b = document.createElement("button");
		b.type = "button";
		b.className = "mm-go";
		b.textContent = "go there";
		b.title = "Step back to instruction " + mmNum(pos) + ", the one that did this (reverse history)";
		b.addEventListener("click", () => this.goTo(pos));
		return b;
	},

	goTo(pos) {
		if (!MEV.st || typeof revStepBack !== "function")
			return;
		if (pos < MEV.st.steps)
			revStepBack(MEV.st.steps - pos);
	},

	showCell(d) {
		this.pick = d;
		this.cursor = d;
		this.dirtyAll = true;
		const [lo, hi] = this.cellRange(d);
		const g = this.segs[this.cellSeg[d]];
		const box = $("mm_info");
		const h = document.createElement("p");
		h.className = "mono";
		h.textContent = mmHex(lo) + "-" + mmHex(hi - 1) + " (" + mmBytes(hi - lo) + "): " + this.segName(g);
		const ev = [];
		const c = MEV.cols, m = MEV.cap - 1;
		const to = this.live ? MEV.n : this.idx;
		for (let i = to - 1; i >= MEV.first() && ev.length < 30; i--) {
			const k = i & m, a = c.addr[k], e = a + Math.max(1, c.size[k]);
			if (a < hi && e > lo)
				ev.push(i);
		}
		const ol = document.createElement("ol");
		ol.className = "mev-list mono";
		for (const i of ev) {
			const e = MEV.event(i);
			const li = document.createElement("li");
			li.append(mmNum(e.pos) + " " + e.initiator + " " + (e.write ? "write " : "read ") + mmHex(e.addr) + " " +
				(e.bytes !== undefined ? e.bytes + " bytes" : e.size + " B = " + mmHex(e.value)) + " ", ...this.eventWhere(e), " ", this.goButton(e.pos));
			ol.append(li);
		}
		const pages = [];
		for (let k = this.samples.length - 1; k >= 0 && pages.length < 5; k--) {
			const s = this.samples[k];
			if (s.pos > (this.live ? Infinity : this.cursorPos()))
				continue;
			if (s.pages.some((p) => p * 4096 < hi && p * 4096 + 4096 > lo))
				pages.push(s);
		}
		for (const s of pages) {
			const li = document.createElement("li");
			li.append("CPU wrote here between instructions " + mmNum(s.from) + " and " + mmNum(s.pos) + " ", this.goButton(s.pos));
			ol.append(li);
		}
		const note = document.createElement("p");
		note.className = "note";
		note.textContent = ev.length || pages.length ? (ev.length >= 30 ? "The newest 30 events" : ev.length + " event" + (ev.length === 1 ? "" : "s")) +
			(pages.length ? ", and the CPU's last writes (sampled per frame)" : "") + ", newest first." : "No event here in the log.";
		box.replaceChildren(h, note, ol);
	},

	cellAt(ev) {
		const r = $("mm_map").getBoundingClientRect();
		const x = Math.floor((ev.clientX - r.left) / r.width * MM_N), y = Math.floor((ev.clientY - r.top) / r.height * MM_N);
		if (x < 0 || y < 0 || x >= MM_N || y >= MM_N)
			return -1;
		return this.xyCell[y * MM_N + x];
	},

	clickAt(ev) {
		const d = this.cellAt(ev);
		if (d >= 0 && this.segs.length)
			this.showCell(d);
	},

	hover(ev) {
		const d = ev ? this.cellAt(ev) : -1;
		const map = $("mm_map");
		if (d < 0 || !this.segs.length) {
			map.title = "";
			return;
		}
		const [lo, hi] = this.cellRange(d);
		map.title = mmHex(lo) + "-" + mmHex(hi - 1) + " " + this.segName(this.segs[this.cellSeg[d]]);
	},

	announce(d) {
		if (!this.segs.length)
			return;
		const [lo, hi] = this.cellRange(d);
		$("mm_cursor").textContent = mmHex(lo) + "-" + mmHex(hi - 1) + " " + this.segName(this.segs[this.cellSeg[d]]) +
			(this.touched[d] ? ", touched" : "");
	},

	// Arrow keys: the cursor on the square; PageUp/PageDown: the previous or
	// next region along the curve; Home/End: the first and last cell;
	// Enter or Space: the cell's events.
	key(ev) {
		if (!this.segs.length)
			return;
		let d = this.cursor < 0 ? 0 : this.cursor, x = this.cellX[d], y = this.cellY[d];
		const k = ev.key;
		if (k === "ArrowLeft")
			x = Math.max(0, x - 1);
		else if (k === "ArrowRight")
			x = Math.min(MM_N - 1, x + 1);
		else if (k === "ArrowUp")
			y = Math.max(0, y - 1);
		else if (k === "ArrowDown")
			y = Math.min(MM_N - 1, y + 1);
		if (k.startsWith("Arrow"))
			d = this.xyCell[y * MM_N + x];
		else if (k === "PageDown")
			d = this.segs[Math.min(this.segs.length - 1, this.cellSeg[d] + 1)].c0;
		else if (k === "PageUp")
			d = this.segs[Math.max(0, this.cellSeg[d] - (d === this.segs[this.cellSeg[d]].c0 ? 1 : 0))].c0;
		else if (k === "Home")
			d = 0;
		else if (k === "End")
			d = MM_CELLS - 1;
		else if (k === "Enter" || k === " ") {
			this.showCell(d);
			ev.preventDefault();
			return;
		} else
			return;
		ev.preventDefault();
		this.cursor = d;
		this.announce(d);
		this.dirtyAll = true;
	},
};
