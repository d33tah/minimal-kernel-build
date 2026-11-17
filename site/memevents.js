"use strict";

// Memory events (M1): who touched which physical address, when.
//
// v86-det.patch reports, through hooks on its cpu object:
//   - the CPU's accesses to physical addresses outside RAM (memory-mapped
//     I/O), from the interpreter and from compiled code alike, with the
//     exact instruction count, EIP and EIP's physical address
//     (src/rust/mev.rs, mem_event_cpu);
//   - every RAM access of a device (DMA in both directions, the descriptors
//     and rings it reads and writes, the 8237's transfers) and every RAM
//     write of the emulator itself (the BIOS images, the kernel loader, a
//     snapshot's restore, this page's writes) through cpu.js's dma_* and
//     emu_* (mem_event). v86 emulates no MSI.
// Every event: position (the stepper's instruction count before the
// instruction that caused it), initiator (cpu, device, emulator), read or
// write, physical address, size, value (for a bulk access: the byte count
// and a hash of the bytes), the region the address was in, the source (a
// device's PCI address, the 8237 channel, the emulator's reason) and for
// the CPU its EIP.
//
// Region names come only from standards the guest itself uses, and each
// says which: the IBM PC memory map, the BIOS's INT 15h E820h answers (the
// guest's real-mode calls, observed), the PCI configuration mechanism #1 on
// ports 0xCF8/0xCFC (vendor and device, the class code named after the
// PCI class table, BARs and their all-ones size probe), the ACPI MADT and
// HPET tables or the Intel MultiProcessor Specification's table when the
// firmware has them, and IA32_APIC_BASE's power-up value. What the guest
// has not used yet has no name.
//
// The log is columnar and a ring (MEV_CAP events, the oldest go first), in
// position order: MEV.query({ from, to, page, initiator, write, region })
// finds events by time range, page and initiator for the memory map and
// the quests. A step back (reverse.js) or a restored snapshot drops the
// events from the restored position on (not the emulator's that made the
// restored state, such as the loader's); running on records them again,
// the same ones (the test in test_browser.py, suite memevents).
//
// Checking mode (?memcheck=N, MEV.checkStart): every N instructions every
// RAM page is hashed (v86.wasm mev_hash_pages); a page that changed with no
// CPU write (v86.wasm marks the pages the CPU may have written) and no
// device or emulator event is reported in MEV.check.failures.
//
// MEV is one object; its region and observation methods are in
// mevregions.js, the Events tab's in mevview.js, spread into it here.

import { flushSync } from "react-dom";
import { CLOCK } from "./clock.js";
import { api, provide } from "./page.js";
import { dbgFrames } from "./dbginfo.js";
import { pciBdf, mevHex, MEV_REGIONS } from "./mevregions.js";
import { MEV_VIEW } from "./mevview.js";

const MEV_KIND_WRITE = 1, MEV_KIND_BULK = 1 << 8, MEV_KIND_E820 = 1 << 9; // src/rust/mev.rs
const MEV_CPU = 0, MEV_DEVICE = 1, MEV_EMULATOR = 2; // libv86 cpu.js MEV_DEVICE, MEV_EMULATOR
const MEV_INITIATORS = ["cpu", "device", "emulator"];
const MEV_ISA_DMA = 0x10000; // libv86 cpu.js: the 8237's channel n is MEV_ISA_DMA + n
const MEV_CAP = 1 << 18;
const MEV_HASH_MAX = 1 << 20; // bulk data is hashed up to this size
// meta: bits 0-1 initiator, bit 2 write, bit 3 bulk, bit 4 bulk not hashed
const MEV_M_WRITE = 4, MEV_M_BULK = 8, MEV_M_NOHASH = 16;

// FNV-1a, 32 bits
function mevHash(data) {
	let h = 0x811c9dc5;
	for (let i = 0; i < data.length; i++)
		h = Math.imul(h ^ data[i], 0x01000193);
	return h >>> 0;
}

const MEV = {
	cpu: null,
	ex: null,
	st: null,
	on: false,
	cap: 0,
	n: 0,        // events recorded (the next one's index); kept: [max(0, n - cap), n)
	counts: [0, 0, 0],
	reasons: [],
	regions: [],   // every region named so far (ids stay valid)
	regionKey: new Map(),
	active: [],    // the regions known now, most specific first
	pageRegion: new Map(),
	obs: [],       // what the guest used: { pos, kind: "pci" | "e820", ... } in position order
	pci: null,     // PCI devices seen: bdf -> { cfg: Uint8Array(256), probe: Set of BAR offsets, sizes: Map }
	pciAddr: 0,
	e820: [],
	acpi: null,
	acpiTried: -Infinity,
	facts: [],     // derived from obs: { pos, kind, text } (the Events tab, quests)
	check: null,
	listTimer: null,
	needs: new Set(), // who wants the events recorded: "list" (the Events tab), "map", "quests"
	since: Infinity,  // recording without a gap from this position on (0: the whole run)

	install(em, st) {
		const cpu = em.v86.cpu;
		this.cpu = cpu;
		this.ex = cpu.wm.exports;
		this.st = st;
		this.missing = !this.ex.mev_set || !cpu.mem_pending;
		if (this.missing)
			return;
		this.clear();
		cpu.mem_event_cpu = (k, a, lo, hi, c, eip, ep) => this.onCpu(k, a, lo, hi, c, eip, ep);
		cpu.mem_event = (i, w, a, s, v, d, src) => this.onJs(i, w, a, s, v, d, src);
		this.wrapPci(cpu.io);
		// The loader's writes, before the first instruction.
		for (const [reason, a, s] of cpu.mem_pending)
			this.onJs(MEV_EMULATOR, 1, a, s, 0, cpu.mem8.subarray(a, a + s), reason);
		cpu.mem_pending.length = 0;
		const q = new URLSearchParams(location.search);
		const every = Number(q.get("memcheck"));
		if (every > 0)
			this.checkStart(every);
		this.setOn(this.needs.size > 0 || every > 0);
		// The open live map samples the CPU's RAM writes from here, not from
		// its first frame: none may come before the run ends (a hidden tab, or
		// frames starved while the JIT runs).
		const map = api.memory.MMAP;
		if (map && map.open)
			map.attach();
	},

	// The Events tab records, the memory map (memmap.js) is open, quests are watched.
	need(who, on) {
		if (on)
			this.needs.add(who);
		else
			this.needs.delete(who);
		this.setOn(this.needs.size > 0 || !!this.check);
	},

	clear() {
		this.gen = (this.gen || 0) + 1; // a new log (memmap.js starts again)
		this.n = 0;
		this.cols = null;
		this.cap = 1 << 14;
		this.alloc(this.cap);
		this.counts = [0, 0, 0];
		this.obs = [];
		this.check = null;
		this.on = false; // a new machine: setOn follows (install)
		this.since = Infinity;
		this.rebuild();
	},

	alloc(cap) {
		const old = this.cols;
		const cols = {
			pos: new Float64Array(cap), addr: new Uint32Array(cap), size: new Uint32Array(cap),
			val: new Uint32Array(cap), valHi: new Uint32Array(cap), eip: new Uint32Array(cap),
			eipPhys: new Int32Array(cap), meta: new Uint8Array(cap), src: new Int32Array(cap), region: new Uint16Array(cap),
		};
		if (old && this.n)
			for (const k of Object.keys(cols))
				cols[k].set(old[k].subarray(0, Math.min(this.n, old[k].length)));
		this.cols = cols;
	},

	setOn(on) {
		if (on && !this.on)
			this.since = this.st ? this.st.steps : 0;
		else if (!on)
			this.since = Infinity;
		this.on = !!on;
		if (this.ex && !this.missing)
			this.ex.mev_set(this.on ? 3 : 2); // bit 0 the CPU's accesses, bit 1 the E820 answers
	},

	// The count before the current instruction (mev_now) as the stepper's
	// count: inside a JIT slice (CLOCK.extraSteps) the stepper's count is
	// the slice's start, when v86's counter was at its start.
	posOf(count) {
		const st = this.st;
		if (!st)
			return count;
		const ctr = new Uint32Array(this.cpu.wasm_memory.buffer, 664, 1)[0];
		const base = CLOCK.extraSteps ? (ctr - CLOCK.extraSteps()) >>> 0 : ctr;
		return st.steps + ((count - base) >>> 0);
	},

	onCpu(kind, addr, lo, hi, count, eip, eipPhys) {
		const pos = this.posOf(count);
		if (kind & MEV_KIND_E820) {
			this.observe({ pos, kind: "e820", buf: addr >>> 0, ebx: lo >>> 0, info: hi, bytes: Array.from(this.cpu.mem8.subarray(addr, addr + 20)) });
			return;
		}
		if (!this.on)
			return;
		const bulk = kind & MEV_KIND_BULK;
		this.push(pos, MEV_CPU | (kind & MEV_KIND_WRITE ? MEV_M_WRITE : 0) | (bulk ? MEV_M_BULK : 0),
			addr >>> 0, bulk ? lo >>> 0 : (kind >> 1) & 127, bulk ? hi : lo, bulk ? 0 : hi, eip, eipPhys, -1);
		if (!this.nameOf(this.cols.region[(this.n - 1) & (this.cap - 1)]).known)
			this.acpiTry(pos);
	},

	onJs(initiator, write, addr, size, value, data, source) {
		if (initiator === MEV_EMULATOR && source === "snapshot restore") {
			this.restoring = true; // MEV.restored() follows (cpustate.js v86Restore)
			return;
		}
		const pos = this.st && this.ex.mev_now ? this.posOf(this.ex.mev_now()) : 0;
		if (this.check && write)
			this.check.touch(addr, size);
		if (!this.on && pos > 0)
			return;
		let meta = initiator | (write ? MEV_M_WRITE : 0), v = value >>> 0;
		if (data) {
			meta |= MEV_M_BULK;
			if (data.length <= MEV_HASH_MAX)
				v = mevHash(data);
			else
				meta |= MEV_M_NOHASH;
		}
		let src = source;
		if (typeof source === "string") {
			src = this.reasons.indexOf(source);
			if (src < 0)
				src = this.reasons.push(source) - 1;
		}
		this.push(pos, meta, addr >>> 0, size >>> 0, v, 0, 0, -1, src);
	},

	push(pos, meta, addr, size, val, valHi, eip, eipPhys, src) {
		if (this.n >= this.cap && this.cap < MEV_CAP) {
			this.cap *= 2;
			this.alloc(this.cap);
		}
		const c = this.cols, i = this.n & (this.cap - 1);
		c.pos[i] = pos;
		c.meta[i] = meta;
		c.addr[i] = addr;
		c.size[i] = size;
		c.val[i] = val;
		c.valHi[i] = valHi;
		c.eip[i] = eip;
		c.eipPhys[i] = eipPhys;
		c.src[i] = src;
		c.region[i] = this.regionOf(addr);
		this.n++;
		this.counts[meta & 3]++;
	},

	first() { return Math.max(0, this.n - this.cap); },

	// The first kept index with position >= pos.
	lowerBound(pos) {
		let lo = this.first(), hi = this.n;
		const p = this.cols.pos, m = this.cap - 1;
		while (lo < hi) {
			const mid = (lo + hi) >>> 1;
			if (p[mid & m] < pos)
				lo = mid + 1;
			else
				hi = mid;
		}
		return lo;
	},

	// Indices (oldest first) of the kept events with from <= position < to,
	// on page (a 4 KiB page number), by initiator ("cpu", "device",
	// "emulator"), write (true/false), in region (an id); limit: the last
	// that many.
	query(f = {}) {
		const out = [];
		if (!this.cols)
			return out;
		const c = this.cols, m = this.cap - 1;
		const a = f.from !== undefined ? this.lowerBound(f.from) : this.first();
		const b = f.to !== undefined ? this.lowerBound(f.to) : this.n;
		const ini = f.initiator !== undefined ? MEV_INITIATORS.indexOf(f.initiator) : -1;
		for (let i = a; i < b; i++) {
			const k = i & m;
			if (ini >= 0 && (c.meta[k] & 3) !== ini)
				continue;
			if (f.write !== undefined && !!(c.meta[k] & MEV_M_WRITE) !== f.write)
				continue;
			if (f.region !== undefined && c.region[k] !== f.region)
				continue;
			if (f.page !== undefined) {
				const lo = c.addr[k] >>> 12, hi = (c.addr[k] + Math.max(1, c.size[k]) - 1) >>> 12;
				if (f.page < lo || f.page > hi)
					continue;
			}
			out.push(i);
		}
		return f.limit ? out.slice(-f.limit) : out;
	},

	event(i) {
		const c = this.cols, k = i & (this.cap - 1), meta = c.meta[k];
		const ini = meta & 3, src = c.src[k];
		const e = {
			i, pos: c.pos[k], initiator: MEV_INITIATORS[ini], write: !!(meta & MEV_M_WRITE), addr: c.addr[k],
			size: c.size[k], region: c.region[k], regionName: this.nameOf(c.region[k]).name,
		};
		if (meta & MEV_M_BULK) {
			e.bytes = c.size[k];
			e.hash = meta & MEV_M_NOHASH ? null : c.val[k];
		} else {
			e.value = c.size[k] === 8 ? c.valHi[k] * 4294967296 + c.val[k] : c.val[k];
		}
		if (ini === MEV_CPU) {
			e.eip = c.eip[k];
			e.eipPhys = c.eipPhys[k];
		} else if (ini === MEV_DEVICE) {
			e.source = src >= MEV_ISA_DMA ? "ISA DMA (8237) channel " + (src - MEV_ISA_DMA) : "PCI " + pciBdf(src);
			e.pci = src < MEV_ISA_DMA ? src : null;
		} else {
			e.source = this.reasons[src];
		}
		return e;
	},
	...MEV_REGIONS,

	// ---- time travel --------------------------------------------------------

	// After a restore (cpustate.js v86Restore): the machine is at the
	// stepper's count; what was recorded from there on is not its past.
	// The emulator's events at that count that came before the machine
	// ran on from it are: they made the restored state (the loader's
	// writes before instruction 0, kept when instruction 0's checkpoint
	// is restored).
	restored() {
		this.restoring = false;
		if (!this.cols || !this.st)
			return;
		const pos = this.st.steps, c = this.cols, m = this.cap - 1;
		let k = this.lowerBound(pos);
		while (k < this.n && c.pos[k & m] === pos && (c.meta[k & m] & 3) === MEV_EMULATOR)
			k++;
		for (let i = k; i < this.n; i++)
			this.counts[this.cols.meta[i & (this.cap - 1)] & 3]--;
		this.n = k;
		const keep = this.obs.filter((o) => o.pos < pos);
		if (keep.length !== this.obs.length) {
			this.obs = keep;
			this.rebuild();
		}
		this.acpiTried = -Infinity;
		// recording on from here: the log has no gap after pos either
		if (this.on)
			this.since = Math.min(this.since, pos);
		if (this.check)
			this.checkBase();
	},

	// ---- checking mode ------------------------------------------------------

	checkStart(every) {
		const pages = this.cpu.memory_size[0] >>> 12;
		this.check = {
			every, base: null, from: 0, next: 0, checks: 0, changed: 0, failures: [], pages,
			touched: new Uint8Array(pages), dirtyPtr: 0,
			touch(addr, size) {
				for (let p = addr >>> 12; p <= (addr + Math.max(1, size) - 1) >>> 12 && p < pages; p++)
					this.touched[p] = 1;
			},
		};
		this.checkBase();
	},

	hashes() {
		const ptr = this.ex.mev_hash_pages();
		return new Uint32Array(this.cpu.wasm_memory.buffer, ptr, this.check.pages).slice();
	},

	checkBase() {
		const c = this.check, pos = this.st ? this.st.steps : 0;
		c.base = this.hashes();
		c.touched.fill(0);
		c.dirtyPtr = this.ex.mev_track_begin();
		c.from = pos;
		c.next = pos + c.every;
	},

	// Called between slices (fastforward.js jitChunk, stepper.js run).
	tick(st) {
		const c = this.check;
		if (!c || st !== this.st || st.steps < c.next)
			return;
		const now = this.hashes();
		const dirty = new Uint8Array(this.cpu.wasm_memory.buffer, c.dirtyPtr, c.pages + 7 >> 3);
		for (let p = 0; p < c.pages; p++) {
			if (now[p] === c.base[p])
				continue;
			c.changed++;
			if (!(dirty[p >> 3] >> (p & 7) & 1) && !c.touched[p] && c.failures.length < 100)
				c.failures.push({ page: p, from: c.from, to: st.steps });
		}
		c.checks++;
		c.base = now;
		c.touched.fill(0);
		c.dirtyPtr = this.ex.mev_track_begin();
		c.from = st.steps;
		c.next = st.steps + c.every;
	},

	// ---- one event in a line (tests, checks) ---------------------------------

	describe(e) {
		const parts = [e.pos.toLocaleString("en-US"), e.initiator, e.write ? "write" : "read", mevHex(e.addr, 8)];
		parts.push(e.bytes !== undefined ? e.bytes + " bytes" + (e.hash !== null ? " #" + mevHex(e.hash, 8).slice(2) : "") :
			e.size + " B = " + (e.size === 8 ? "0x" + e.value.toString(16) : mevHex(e.value)));
		parts.push(e.regionName);
		if (e.initiator === "cpu") {
			let at = "EIP " + mevHex(e.eip);
			if (this.st && e.eipPhys >= 0) {
				const i = this.st.symbolOf(e.eipPhys);
				if (i >= 0)
					at += " " + this.st.symName[i];
				const f = typeof dbgFrames === "function" ? dbgFrames(e.eipPhys, true) : null;
				if (f && f.state === "ok" && f.frames[0].file)
					at += " " + f.frames[0].file + ":" + f.frames[0].line;
			}
			parts.push(at);
		} else {
			parts.push(e.source);
		}
		return parts.join("  ");
	},

	// ---- the Events tab -----------------------------------------------------
	//
	// EventsPanel (events.jsx) draws `view`, which render() makes from the log
	// while the tab is shown, and draws it again at once when render() made a
	// new one (a test reads the table right after MEV.render()).

	listUser: null,  // the Record box as the user last set it (null: not touched)
	tabShown: false,
	drawn: "",       // what the table was drawn from (render skips a redraw of the same)
	filter: { who: "", rw: "", region: "", text: "" },
	grouping: true,  // "group repeats"
	view: null,      // { status, rows, more, regions, facts, chips }
	rev: 0,
	subs: new Set(),
	subscribe(fn) {
		MEV.subs.add(fn);
		return () => MEV.subs.delete(fn);
	},
	changed() {
		this.rev++;
		flushSync(() => {
			for (const fn of this.subs)
				fn();
		});
	},
	...MEV_VIEW,

	// A group's key: who, read or write, bulk, region, source or (CPU) the
	// function of the code.
	groupKey(k) {
		const c = this.cols, meta = c.meta[k];
		let who = c.src[k];
		if ((meta & 3) === MEV_CPU) {
			const st = this.st, pa = c.eipPhys[k];
			who = st && pa >= 0 ? st.symbolOf(pa) : -1;
			if (who < 0)
				who = "r" + this.regionOf(pa >>> 0);
		}
		return (meta & (3 | MEV_M_WRITE | MEV_M_BULK)) + ":" + c.region[k] + ":" + c.size[k] + ":" + who;
	},
};

provide({ MEV });

export { MEV_CPU, MEV_INITIATORS, MEV_ISA_DMA, MEV_M_WRITE, MEV_M_BULK, MEV };
