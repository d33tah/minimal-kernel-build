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
//
// MMAP is one object; its methods by concern are in memmaplayout.js
// (constants, layout, applying the log), memmapdraw.js (drawing) and
// memmaplist.js (lists, pointer, keys), spread into it here.

import { realNow } from "./clock.js";
import { $ } from "./util.js";
import { provide, state, tab } from "./page.js";
import { MEV } from "./memevents.js";
import { MM_CELLS, MM_LAYERS, MM_TEXT_HI, MM_TEXT_LO, mmHilbert, MM_N, MM_KEY, mmNum,
	MM_LAYOUT } from "./memmaplayout.js";
import { MM_DRAW } from "./memmapdraw.js";
import { MM_LISTS } from "./memmaplist.js";

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
	hl: null,        // a quest's hint: { ranges, label, t0, cells } (hint())
	labelsDrawn: null,
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
		for (const p of ["panel_memory", "panel_quests"])
			new MutationObserver(() => this.schedule()).observe($(p), { attributes: true, attributeFilter: ["hidden"] });
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
		return this.open && !$("mm").closest("[role=tabpanel]").hidden && document.visibilityState !== "hidden";
	},

	// The map is in the Memory tab and goes with the Quests tab while that
	// is shown (its quests are answered on it); tabs.js showTab.
	place(tab) {
		const box = $("mm"), slot = $(tab === "quests" ? "qs_mapslot" : "mm_home");
		if (box.parentElement !== slot)
			slot.append(box);
		this.schedule();
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
	...MM_LAYOUT,

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
		// live or at the log's end: the CPU's writes after the last event too
		if ((keepLive || target >= MEV.n) && MEV.st)
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
		if (hot || this.anims.length || this.dirtyAll || this.hl)
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
	...MM_DRAW,
	...MM_LISTS,
};

// The live map goes with the tab shown: the Quests tab beside its cards,
// else the Memory tab (showTab).
state.subscribe(() => MMAP.place(tab), ["tabShows"]);

provide({ MMAP });

export { MMAP };
