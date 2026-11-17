"use strict";

// Memory events' regions (memevents.js): the PCI classes and E820 types,
// and MEV's methods that keep the regions and observe the firmware's and
// the devices' tables (MEV_REGIONS, spread into MEV).

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

const MEV_REGIONS = {
	// ---- regions ------------------------------------------------------------

	region(lo, hi, name, std, kind) {
		// The standard is part of it: the local APIC at its power-up address
		// named by a firmware table is not the IA32_APIC_BASE default.
		const key = lo + ":" + hi + ":" + name + ":" + std;
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
			a.mp = mp; // the floating pointer "_MP_"
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
};

export { PCI_CLASSES, mevHex, pciBdf, pciClassName, MEV_REGIONS };
