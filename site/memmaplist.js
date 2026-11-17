"use strict";

// The live memory map's lists (memmap.js): the story of what happened,
// the regions, a cell's events, the pointer and the keys (MMAP's methods,
// spread into MMAP).

import { $ } from "./util.js";
import { dbgFrames } from "./dbginfo.js";
import { srcLinkEl } from "./srclinks.js";
import { MEV } from "./memevents.js";
import { revStepBack } from "./reverse.js";
import { MM_L_CPU, MM_L_RAM, MM_L_DEV, mmNum, mmHex, mmBytes, MM_N, MM_CELLS } from "./memmaplayout.js";

const MM_LISTS = {
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
		if (!MEV.st)
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

export { MM_LISTS };
