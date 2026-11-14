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
// events from the restored position on; running on records them again,
// the same ones (the test in test_browser.py, suite memevents).
//
// Checking mode (?memcheck=N, MEV.checkStart): every N instructions every
// RAM page is hashed (v86.wasm mev_hash_pages); a page that changed with no
// CPU write (v86.wasm marks the pages the CPU may have written) and no
// device or emulator event is reported in MEV.check.failures.

const MEV_KIND_WRITE = 1, MEV_KIND_BULK = 1 << 8, MEV_KIND_E820 = 1 << 9; // src/rust/mev.rs
const MEV_CPU = 0, MEV_DEVICE = 1, MEV_EMULATOR = 2; // libv86 cpu.js MEV_DEVICE, MEV_EMULATOR
const MEV_INITIATORS = ["cpu", "device", "emulator"];
const MEV_ISA_DMA = 0x10000; // libv86 cpu.js: the 8237's channel n is MEV_ISA_DMA + n
const MEV_CAP = 1 << 18;
const MEV_HASH_MAX = 1 << 20; // bulk data is hashed up to this size
// meta: bits 0-1 initiator, bit 2 write, bit 3 bulk, bit 4 bulk not hashed
const MEV_M_WRITE = 4, MEV_M_BULK = 8, MEV_M_NOHASH = 16;

// PCI Code and ID Assignment Specification: base classes and the
// subclasses a PC has.
const PCI_CLASSES = {
	0x00: ["unclassified device", {}],
	0x01: ["mass storage controller", { 0x00: "SCSI", 0x01: "IDE", 0x02: "floppy", 0x05: "ATA", 0x06: "SATA", 0x08: "NVM" }],
	0x02: ["network controller", { 0x00: "Ethernet" }],
	0x03: ["display controller", { 0x00: "VGA compatible", 0x01: "XGA", 0x02: "3D" }],
	0x04: ["multimedia controller", { 0x00: "video", 0x01: "audio", 0x03: "HD audio" }],
	0x05: ["memory controller", { 0x00: "RAM" }],
	0x06: ["bridge", { 0x00: "host bridge", 0x01: "ISA bridge", 0x04: "PCI-to-PCI bridge", 0x80: "other bridge" }],
	0x07: ["communication controller", { 0x00: "serial" }],
	0x08: ["base system peripheral", { 0x00: "interrupt controller", 0x01: "DMA controller", 0x02: "timer", 0x03: "RTC" }],
	0x09: ["input device controller", { 0x00: "keyboard" }],
	0x0c: ["serial bus controller", { 0x03: "USB", 0x05: "SMBus" }],
	0xff: ["unassigned class", {}],
};

const E820_TYPES = { 1: "usable RAM", 2: "reserved", 3: "ACPI reclaimable", 4: "ACPI NVS", 5: "unusable" };

const mevHex = (n, w = 0) => "0x" + (n >>> 0).toString(16).padStart(w, "0");

// FNV-1a, 32 bits
function mevHash(data) {
	let h = 0x811c9dc5;
	for (let i = 0; i < data.length; i++)
		h = Math.imul(h ^ data[i], 0x01000193);
	return h >>> 0;
}

function pciBdf(id) {
	return ((id >> 8) & 0xff).toString(16).padStart(2, "0") + ":" + ((id >> 3) & 0x1f).toString(16).padStart(2, "0") + "." + (id & 7);
}

function pciClassName(cls, sub) {
	const c = PCI_CLASSES[cls];
	const code = cls.toString(16).padStart(2, "0") + ":" + sub.toString(16).padStart(2, "0");
	if (!c)
		return "class " + code + " (PCI class table)";
	return c[0] + (c[1][sub] ? ", " + c[1][sub] : "") + " (PCI class " + code + ")";
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
	facts: [],     // derived from obs: { pos, kind, text } (the debug list, quests)
	check: null,
	listTimer: null,

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
		this.setOn(this.on || q.get("memevents") === "1" || every > 0);
	},

	clear() {
		this.n = 0;
		this.cols = null;
		this.cap = 1 << 14;
		this.alloc(this.cap);
		this.counts = [0, 0, 0];
		this.obs = [];
		this.check = null;
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

	// ---- regions ------------------------------------------------------------

	region(lo, hi, name, std, kind) {
		const key = lo + ":" + hi + ":" + name;
		let r = this.regionKey.get(key);
		if (!r) {
			r = { id: this.regions.length, lo, hi, name, std, kind, known: true };
			this.regions.push(r);
			this.regionKey.set(key, r);
		}
		return r;
	},

	nameOf(id) {
		return this.regions[id] || { name: "?", known: false };
	},

	regionOf(addr) {
		const p = addr >>> 12;
		let id = this.pageRegion.get(p);
		if (id === undefined) {
			id = this.findRegion(addr);
			// a page split between two regions is looked up every time
			const r = this.regions[id];
			if (r.lo <= p * 4096 && r.hi >= p * 4096 + 4096)
				this.pageRegion.set(p, id);
		}
		return id;
	},

	findRegion(addr) {
		for (const r of this.active)
			if (addr >= r.lo && addr < r.hi)
				return r.id;
		const ram = this.cpu ? this.cpu.memory_size[0] >>> 0 : 0;
		if (addr < ram)
			return this.region(0, ram, "RAM, before any E820 answer described it", "", "ram").id;
		const r = this.region(ram, 2 ** 32, "no standard the guest used names this address yet", "", "none");
		r.known = false;
		return r.id;
	},

	// The static regions and what the observations say, most specific first.
	rebuild() {
		this.pci = new Map();
		this.pciAddr = 0;
		this.e820 = [];
		this.acpi = null;
		this.facts = [];
		const obs = this.obs;
		this.obs = [];
		for (const o of obs)
			this.observe(o, true);
		this.activate();
	},

	activate() {
		const pc = "IBM PC memory map", act = [];
		for (const [lo, hi, name] of [[0xa0000, 0xb0000, "VGA graphics window"], [0xb0000, 0xb8000, "monochrome text (MDA) video memory"],
			[0xb8000, 0xc0000, "colour text (CGA) video memory"], [0xc0000, 0xc8000, "video BIOS ROM"], [0xc8000, 0xf0000, "option ROM area"],
			[0xf0000, 0x100000, "system BIOS ROM"], [0, 0x400, "real-mode interrupt vector table"], [0x400, 0x500, "BIOS data area"]])
			act.push(this.region(lo, hi, name, pc, lo >= 0xa0000 && lo < 0xc0000 ? "mmio" : lo >= 0xc0000 ? "rom" : "ram"));
		// PCI BARs and ACPI before the IBM PC map (a BAR can sit in the ROM area)
		const specific = [];
		for (const [bdf, d] of this.pci) {
			for (const [off, { size, io }] of d.sizes) {
				const v = d.cfg[off] | d.cfg[off + 1] << 8 | d.cfg[off + 2] << 16 | d.cfg[off + 3] << 24;
				const rom = off === 0x30;
				if (io)
					continue; // an I/O BAR: ports, not memory
				const base = (v & (rom ? ~0x7ff : ~0xf)) >>> 0;
				if (!base)
					continue;
				const what = rom ? "expansion ROM" : "BAR" + (off - 0x10) / 4;
				specific.push(this.region(base, base + size, "PCI " + pciBdf(bdf) + " " + what + ": " + pciClassName(d.cfg[0xb], d.cfg[0xa]),
					"PCI configuration mechanism #1", "mmio"));
			}
		}
		if (this.acpi) {
			const a = this.acpi;
			if (a.lapic)
				specific.push(this.region(a.lapic, a.lapic + 0x1000, "local APIC", a.std, "mmio"));
			for (const io of a.ioapics)
				specific.push(this.region(io.addr, io.addr + 0x1000, "I/O APIC " + io.id + (io.gsi === null ? "" : " (GSI " + io.gsi + " on)"), a.std, "mmio"));
			if (a.hpet)
				specific.push(this.region(a.hpet, a.hpet + 0x400, "HPET", "ACPI HPET table", "mmio"));
		}
		specific.push(this.region(0xfee00000, 0xfee01000, "local APIC", "IA32_APIC_BASE power-up value (Intel SDM)", "mmio"));
		const e820 = this.e820.map((e) => this.region(e.base, e.base + e.length, (E820_TYPES[e.type] || "type " + e.type),
			"E820 type " + e.type, e.type === 1 ? "ram" : "reserved"));
		this.active = [...specific, ...act, ...e820];
		this.pageRegion.clear();
	},

	// ---- observations -------------------------------------------------------

	observe(o, replaying) {
		this.obs.push(o);
		if (o.kind === "e820") {
			const b = o.bytes, u32 = (k) => (b[k] | b[k + 1] << 8 | b[k + 2] << 16 | b[k + 3] << 24) >>> 0;
			const cf = !!(o.info & 0x200), smap = !!(o.info & 0x100);
			if (cf || !smap)
				return;
			const e = { base: u32(0) + u32(4) * 4294967296, length: u32(8) + u32(12) * 4294967296, type: u32(16) };
			if (e.base < 2 ** 32) {
				e.length = Math.min(e.length, 2 ** 32 - e.base);
				this.e820.push(e);
				this.facts.push({ pos: o.pos, kind: "e820", text: "E820: " + mevHex(e.base) + "-" + mevHex(e.base + e.length - 1) + " " + (E820_TYPES[e.type] || "type " + e.type) });
			}
			if (!replaying) {
				this.acpiTry(o.pos);
				this.activate();
			}
		} else if (o.kind === "pci") {
			this.pciApply(o);
		} else if (o.kind === "acpi") {
			this.acpi = o.acpi;
			this.facts.push({ pos: o.pos, kind: "firmware", text: o.text });
			if (!replaying)
				this.activate();
		}
	},

	wrapPci(io) {
		const P = this;
		for (let port = 0xcf8; port <= 0xcff; port++) {
			const e = io.ports[port];
			for (const sz of [8, 16, 32]) {
				const r = e["read" + sz], w = e["write" + sz];
				e["read" + sz] = function (p) {
					const v = r.call(this, p);
					P.pciIo(port, sz, false, v);
					return v;
				};
				e["write" + sz] = function (v) {
					w.call(this, v);
					P.pciIo(port, sz, true, v);
				};
			}
		}
	},

	pciIo(port, size, write, value) {
		if (port === 0xcf8) {
			if (write && size === 32)
				this.pciAddr = value >>> 0;
			return;
		}
		const a = this.pciAddr;
		if (port < 0xcfc || !(a & 0x80000000))
			return;
		this.observe({ pos: this.posOf(this.ex.mev_now()), kind: "pci", bdf: (a >>> 8) & 0xffff, reg: (a & 0xfc) + port - 0xcfc,
			size: size >> 3, write, value: value >>> 0 });
	},

	pciApply(o) {
		let d = this.pci.get(o.bdf);
		if (!d) {
			d = { cfg: new Uint8Array(256), seen: new Uint8Array(256), probe: new Set(), sizes: new Map(), named: false };
			d.cfg.fill(0xff, 0, 4);
			this.pci.set(o.bdf, d);
		}
		const bar = o.reg >= 0x10 && o.reg < 0x28 || o.reg === 0x30 ? o.reg & ~3 : -1;
		if (o.write && bar >= 0 && o.size === 4 && (o.value === 0xffffffff || (bar === 0x30 && (o.value | 0x7ff) >>> 0 === 0xffffffff))) {
			d.probe.add(bar);
			return; // the size probe: what the BAR held stays the address
		}
		if (!o.write && bar >= 0 && o.size === 4 && d.probe.has(bar)) {
			d.probe.delete(bar);
			const mask = bar === 0x30 ? ~0x7ff : o.value & 1 ? ~3 : ~0xf;
			const size = o.value & mask ? ((~(o.value & mask)) >>> 0) + 1 : 0;
			if (size) {
				d.sizes.set(bar, { size, io: bar !== 0x30 && !!(o.value & 1) });
				this.facts.push({ pos: o.pos, kind: "pci-size", bdf: o.bdf, bar, size,
					text: "PCI " + pciBdf(o.bdf) + " " + (bar === 0x30 ? "expansion ROM" : "BAR" + (bar - 0x10) / 4) + " sized: " + size + " bytes of " + (o.value & 1 && bar !== 0x30 ? "I/O ports" : "memory") });
			}
			return;
		}
		if (o.write && bar >= 0)
			d.probe.delete(bar);
		for (let k = 0; k < o.size; k++) {
			d.cfg[o.reg + k] = o.value >>> 8 * k;
			d.seen[o.reg + k] = 1;
		}
		// named once the guest has read the vendor, device and class
		const ids = d.seen[0] & d.seen[1] & d.seen[2] & d.seen[3] & d.seen[0xa] & d.seen[0xb];
		if (!d.named && ids && (d.cfg[0] & d.cfg[1]) !== 0xff) {
			d.named = true;
			const id = (d.cfg[0] | d.cfg[1] << 8).toString(16).padStart(4, "0") + ":" + (d.cfg[2] | d.cfg[3] << 8).toString(16).padStart(4, "0");
			this.facts.push({ pos: o.pos, kind: "pci-class", bdf: o.bdf,
				text: "PCI " + pciBdf(o.bdf) + " " + id + ": " + pciClassName(d.cfg[0xb], d.cfg[0xa]) });
		}
		if (o.write && bar >= 0 && d.sizes.has(bar))
			this.facts.push({ pos: o.pos, kind: "pci-base", bdf: o.bdf, bar, text: "PCI " + pciBdf(o.bdf) + " " + (bar === 0x30 ? "expansion ROM" : "BAR" + (bar - 0x10) / 4) + " at " + mevHex(o.value) });
		if (bar >= 0 || o.reg <= 0xb)
			this.activate();
	},

	// The firmware's tables, read from RAM: ACPI (6.x, 5.2.5.1: the RSDP on
	// a 16-byte boundary in the EBDA's first KiB or in 0xE0000-0xFFFFF, its
	// RSDT, the MADT and HPET tables) and the Intel MultiProcessor
	// Specification 1.4 (4.: the floating pointer "_MP_" in the EBDA's first
	// KiB, the last KiB of base memory or 0xF0000-0xFFFFF, its configuration
	// table with the local APIC's and the I/O APICs' addresses). Tried when
	// an E820 answer comes and at an unnamed address (at most once per
	// million instructions).
	acpiTry(pos) {
		if ((this.acpi && this.acpi.done) || pos < this.acpiTried + 1e6 || !this.cpu)
			return;
		this.acpiTried = pos;
		const m = this.cpu.mem8, n = m.length;
		const u32 = (k) => k >= 0 && k + 4 <= n ? (m[k] | m[k + 1] << 8 | m[k + 2] << 16 | m[k + 3] << 24) >>> 0 : 0;
		const u16 = (k) => k >= 0 && k + 2 <= n ? m[k] | m[k + 1] << 8 : 0;
		const sig = (k, s) => k >= 0 && k + s.length <= n && [...s].every((ch, j) => m[k + j] === ch.charCodeAt(0));
		const ebda = (m[0x40e] | m[0x40f] << 8) << 4;
		const find = (s, areas) => {
			for (const [lo, hi] of areas)
				for (let k = lo; k < hi; k += 16)
					if (sig(k, s))
						return k;
			return -1;
		};
		const a = { lapic: 0, ioapics: [], hpet: 0, std: "", done: false }, found = [];
		const rsdp = find("RSD PTR ", [[ebda, ebda + 1024], [0xe0000, 0x100000]]);
		const rsdt = rsdp >= 0 ? u32(rsdp + 16) : -1;
		if (sig(rsdt, "RSDT")) {
			a.std = "ACPI MADT";
			for (let k = rsdt + 36; k < rsdt + u32(rsdt + 4) && k < n; k += 4) {
				const t = u32(k);
				if (sig(t, "APIC")) {
					a.lapic = u32(t + 36);
					found.push("ACPI MADT");
					for (let e = t + 44; e < t + u32(t + 4) && m[e + 1] >= 2; e += m[e + 1])
						if (m[e] === 1)
							a.ioapics.push({ id: m[e + 2], addr: u32(e + 4), gsi: u32(e + 8) });
				} else if (sig(t, "HPET")) {
					a.hpet = u32(t + 44);
					found.push("ACPI HPET table");
				}
			}
		}
		const mp = find("_MP_", [[ebda, ebda + 1024], [0x9fc00, 0xa0000], [0xf0000, 0x100000]]);
		const pcmp = mp >= 0 ? u32(mp + 4) : -1;
		if (!a.lapic && sig(pcmp, "PCMP")) {
			a.std = "MultiProcessor Specification table";
			a.lapic = u32(pcmp + 36);
			found.push("MultiProcessor Specification table");
			for (let e = pcmp + 44, k = 0; k < u16(pcmp + 34) && e < n; k++) {
				if (m[e] === 2)
					a.ioapics.push({ id: m[e + 1], addr: u32(e + 4), gsi: null });
				e += m[e] === 0 ? 20 : 8;
			}
		}
		if (!found.length)
			return;
		a.done = true;
		const text = found.join(" and ") + (a.lapic ? ": local APIC " + mevHex(a.lapic) : "") +
			a.ioapics.map((x) => "; I/O APIC " + x.id + " " + mevHex(x.addr)).join("") + (a.hpet ? "; HPET " + mevHex(a.hpet) : "");
		this.observe({ pos, kind: "acpi", acpi: a, text });
	},

	// ---- time travel --------------------------------------------------------

	// After a restore (cpustate.js v86Restore): the machine is at the
	// stepper's count; what was recorded from there on is not its past.
	restored() {
		this.restoring = false;
		if (!this.cols || !this.st)
			return;
		const pos = this.st.steps;
		const k = this.lowerBound(pos);
		for (let i = k; i < this.n; i++)
			this.counts[this.cols.meta[i & (this.cap - 1)] & 3]--;
		this.n = k;
		const keep = this.obs.filter((o) => o.pos < pos);
		if (keep.length !== this.obs.length) {
			this.obs = keep;
			this.rebuild();
		}
		this.acpiTried = -Infinity;
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

	// ---- the debug list (Memory tab) ----------------------------------------

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

	render() {
		const box = document.getElementById("mev");
		if (!box || !box.open)
			return;
		const st = document.getElementById("mev_status");
		if (this.missing || !this.cols) {
			st.textContent = this.missing ? "This v86 build reports no memory events." : "Boot a kernel to record its memory events.";
			return;
		}
		const lost = this.first();
		st.textContent = (this.on ? "Recording. " : "Not recording. ") + (this.n - lost).toLocaleString("en-US") + " events kept" +
			(lost ? " (the oldest " + lost.toLocaleString("en-US") + " dropped)" : "") + ": cpu " + this.counts[0].toLocaleString("en-US") +
			", device " + this.counts[1].toLocaleString("en-US") + ", emulator " + this.counts[2].toLocaleString("en-US") +
			(this.check ? "; check every " + this.check.every.toLocaleString("en-US") + " instructions: " + this.check.checks + " done, " +
				this.check.failures.length + " pages changed without an author" : "") + ".";
		const list = document.getElementById("mev_list");
		list.replaceChildren(...this.query({ limit: 100 }).reverse().map((i) => {
			const li = document.createElement("li");
			li.textContent = this.describe(this.event(i));
			return li;
		}));
		const regs = document.getElementById("mev_regions");
		regs.replaceChildren(...this.active.map((r) => {
			const li = document.createElement("li");
			li.textContent = mevHex(r.lo, 8) + "-" + mevHex(r.hi - 1, 8) + " " + r.name + (r.std ? " (" + r.std + ")" : "");
			return li;
		}), ...this.facts.slice(-50).map((f) => {
			const li = document.createElement("li");
			li.textContent = f.pos.toLocaleString("en-US") + " " + f.text;
			return li;
		}));
	},

	panel() {
		const box = document.getElementById("mev");
		if (!box)
			return;
		const q = new URLSearchParams(location.search);
		if (q.get("memevents") === "1" || Number(q.get("memcheck")) > 0)
			box.open = true;
		const sync = () => {
			this.setOn(box.open || !!this.check);
			clearInterval(this.listTimer);
			this.listTimer = box.open ? setInterval(() => this.render(), 500) : null;
			this.render();
		};
		box.addEventListener("toggle", sync);
		sync();
	},
};
