"use strict";

// Quests (M3): challenges about the standards a PC boots with, checked by
// the page itself from the memory events (memevents.js), the machine's
// state and the live memory map (memmap.js). A quest is solved by an
// answer typed into its box or picked from its choices, by a click on the
// right cell of the map, or by stepping back to the right instruction.
// Each one has a hint (its region blinks on the map), a paragraph on the
// standard it teaches with a link to a primary or reputable source, and a
// done state kept per browser and per kernel (localStorage; without
// storage the page keeps it until it is closed).
//
// What a quest needs comes from the guest only: the loader's writes before
// the first instruction, the BIOS's E820 answers, the option ROM header at
// 0xC0000, the CPU's writes to the colour text memory, the PCI
// configuration accesses on ports 0xCF8/0xCFC after the OS started (its
// first E820 call: the BIOS makes none), the MultiProcessor table's I/O
// APIC and the CPU's accesses to it. A quest the kernel cannot give says
// so once the run has ended ("try Debian"); before that it waits.

import { $ } from "./util.js";
import { stepper, runState, image, api } from "./page.js";
import { PCI_CLASSES, MEV, MEV_CPU, MEV_M_WRITE, pciBdf, pciClassName } from "./memevents.js";
import { MMAP } from "./memmap.js";

const QS_KEY = "mkb-quests";           // { kernel name: { quest id: time solved } }
const QS_OPEN_KEY = "mkb-quests-open";
const QS_TICK_MS = 500;
const QS_LOADER_PARTS = ["kernel command line", "kernel real-mode part", "kernel protected-mode part", "initrd"];
const QS_TEXT_LO = 0xb8000, QS_TEXT_HI = 0xc0000;

const qsHex = (n) => "0x" + (n >>> 0).toString(16);
const qsNum = (n) => n.toLocaleString("en-US");

// Text with its key values (addresses, numbers with a unit, register and
// interrupt names, signatures) set off as <code>.
const QS_KEY_RE = /(0x[0-9A-Fa-f]+(?:-0x[0-9A-Fa-f]+)?|\b\d[\d,]*(?: bytes| KiB| MiB)|INT 15h|EAX=E820h|_MP_|0x55 0xAA)/;

function qsRich(el, text) {
	el.replaceChildren(...String(text).split(QS_KEY_RE).map((part, k) => {
		if (!(k & 1))
			return part;
		const c = document.createElement("code");
		c.className = "qs-key";
		c.textContent = part;
		return c;
	}));
}

// A card's state in words.
function qsChip(s) {
	switch (s.state) {
	case "open": return "To do";
	case "solved": return "Solved";
	case "boot": return "Boot again first";
	case "never": return /Debian/.test(s.why || "") ? "Needs Debian" : "Not on this kernel";
	default: return "Waiting for the run";
	}
}

// "187,136", "187136 bytes", "0x2db00", "35.5 KiB", "2 MiB" -> a number;
// anything else NaN.
function qsParse(text) {
	const t = String(text).trim().toLowerCase().replace(/[,_\s]/g, "");
	if (/^0x[0-9a-f]+$/.test(t))
		return parseInt(t.slice(2), 16);
	const m = /^(\d+(?:\.\d+)?)(k|ki|m|mi)?(?:b|bytes?)?$/.exec(t);
	if (!m)
		return NaN;
	return Number(m[1]) * (m[2] ? (m[2][0] === "k" ? 1024 : 1048576) : 1);
}

// A number answer: right, or which way it is off.
function qsJudgeNumber(v, want, what) {
	if (v === want)
		return { ok: true };
	return { ok: false, msg: v < want ? "Too small: the " + what + " is larger." : "Too large: the " + what + " is smaller." };
}

// The position of the OS's first E820 call (the BIOS makes none, so what
// comes after is the OS), or null.
function qsOsStart(run) {
	if (run.os === undefined || run.os === null) {
		const o = MEV.obs.find((x) => x.kind === "e820");
		run.os = o ? o.pos : null;
	}
	return run.os;
}

// The machine has stopped for good (the end banner: halted, panic).
function qsEnded() {
	const b = $("end_banner");
	return !!(b && b.dataset.kind);
}

// Not there (yet): "never" once the run has ended, else "wait".
function qsNotYet(never, wait) {
	return qsEnded() ? { state: "never", why: never } : { state: "wait", why: wait };
}

// The CPU events [run.scan[key], MEV.n) in turn until test(event index)
// says yes; returns that index or -1. Remembers how far it looked, so each
// event is looked at once (a step back that cut the log starts lower).
function qsScan(run, key, test) {
	const from = Math.max(MEV.first(), Math.min(run.scan[key] || 0, MEV.n));
	for (let i = from; i < MEV.n; i++) {
		if (test(i)) {
			run.scan[key] = i;
			return i;
		}
	}
	run.scan[key] = MEV.n;
	return -1;
}

// The map's picked cell (there is one) overlaps [lo, hi).
function qsPicked(lo, hi) {
	const [a, b] = MMAP.cellRange(MMAP.pick);
	return a < hi && b > lo;
}

const QUEST_LIST = [
	{
		id: "loader",
		title: "The loader's delivery",
		std: "Linux x86 boot protocol",
		kind: "number",
		task: "Before the CPU runs its first instruction, the emulator plays the boot loader and copies the kernel into RAM. How many bytes did it put there for the kernel: its command line, its real-mode part, its protected-mode part and an initrd if there is one?",
		hint: "Press Replay on the memory map: every drop is labelled with its part and its size, and the parts blink now. Or click their cells (0x8e000, 0x80000, 1 MiB) for the events. The BIOS images do not count: they go to the ROM area.",
		learn: "A bzImage file is two programs in one. Its first (setup_sects + 1) x 512 bytes are the real-mode setup, which the loader puts below 1 MiB; the rest is the protected-mode kernel, loaded at 1 MiB (0x100000). The loader also copies the kernel command line to low memory and stores its address in the boot header (cmd_line_ptr), and puts an initrd high in RAM (ramdisk_image, ramdisk_size). The header at offset 0x1f1 of the file describes all of it; GRUB, SYSLINUX and v86 follow the same rules.",
		link: ["https://www.kernel.org/doc/html/latest/arch/x86/boot.html", "The Linux/x86 Boot Protocol (kernel.org)"],
		probe(run) {
			if (run.loader)
				return { state: "open" };
			if (MEV.missing)
				return { state: "never", why: "This v86 build reports no memory events." };
			const parts = MEV.query({ initiator: "emulator", to: 1 }).map((i) => MEV.event(i)).filter((e) => QS_LOADER_PARTS.includes(e.source));
			if (!parts.length)
				return MEV.cols && MEV.n ? { state: "never", why: "This kernel was not loaded by the Linux boot protocol (or the loader's writes were cut from the log: boot again)." } :
					{ state: "wait", why: "Boot a kernel first." };
			run.loader = { parts, total: parts.reduce((a, e) => a + e.bytes, 0) };
			return { state: "open" };
		},
		ranges(run) {
			return run.loader ? run.loader.parts.map((e) => [e.addr, e.addr + e.bytes]) : [[0x100000, 0x100001]];
		},
		hintLabel: "the loader's drops",
		judge(run, v) {
			return qsJudgeNumber(v, run.loader.total, "loader's delivery");
		},
		won(run) {
			const p = run.loader.parts;
			return p.map((e) => qsNum(e.bytes) + " (" + e.source.replace(/^kernel /, "") + " at " + qsHex(e.addr) + ")").join(" + ") + " = " + qsNum(run.loader.total) + " bytes.";
		},
	},
	{
		id: "rom",
		title: "Read the ROM's signature",
		std: "Option ROM header",
		kind: "number",
		task: "The video BIOS sits at 0xC0000 as an option ROM, and its first three bytes say what it is and how long it is. How many bytes long is it?",
		hint: "Open the page at 0xc0000 (the button below) and read its first bytes: 0x55 0xAA is the signature, the third byte is the length in 512-byte blocks. The ROM blinks on the map.",
		learn: "At boot the system BIOS scans 0xC0000 to 0xDFFFF in 2 KiB steps for the bytes 0x55 0xAA. Each hit is an option ROM: the next byte is its size in 512-byte blocks, all its bytes add up to 0 (mod 256), and at offset 3 is the code the BIOS calls so the ROM can set up its card. The video card's ROM is first, at 0xC0000, so the screen works before anything else. A PCI card brings its ROM in an expansion ROM BAR, and the BIOS copies it down into this area.",
		link: ["https://en.wikipedia.org/wiki/Option_ROM", "Option ROM (Wikipedia)"],
		probe(run) {
			if (run.rom)
				return { state: "open" };
			const m = MEV.cpu.mem8;
			if (m.length < 0xc0003 || m[0xc0000] !== 0x55 || m[0xc0001] !== 0xaa)
				return { state: "never", why: "No option ROM signature (0x55 0xAA) at 0xC0000 on this machine." };
			run.rom = { blocks: m[0xc0002], bytes: m[0xc0002] * 512 };
			return { state: "open" };
		},
		ranges() {
			return [[0xc0000, 0xc8000]];
		},
		hintLabel: "video BIOS ROM",
		page: 0xc0,
		judge(run, v) {
			return qsJudgeNumber(v, run.rom.bytes, "ROM");
		},
		won(run) {
			return "0x55 0xAA, then " + run.rom.blocks + " blocks of 512 bytes: " + qsNum(run.rom.bytes) + " bytes.";
		},
	},
	{
		id: "ramend",
		title: "Where does RAM end?",
		std: "E820 (BIOS memory map)",
		kind: "address",
		task: "Early on, the kernel asks the BIOS for a map of memory (INT 15h, EAX=E820h). Find the last byte of usable RAM the BIOS reported: click its cell on the live memory map, or type its address.",
		hint: "The map's story lists the lines 'the guest learned E820: ...'. Type 1 is usable RAM; where the highest usable entry ends, usable RAM ends. The usable entries blink on the map now.",
		learn: "On a PC the firmware tells the OS where RAM is. The OS calls INT 15h with EAX=0xE820 and 'SMAP' in EDX again and again, and each answer is one entry: a 64-bit base, a 64-bit length and a type (1 usable, 2 reserved, 3 ACPI reclaimable, 4 ACPI NVS). Holes are left for the BIOS, the ROMs and memory-mapped devices, which is why usable RAM can end below the amount of memory the machine has. ACPI took this interface over and specifies it in its chapter on system address map interfaces.",
		link: ["https://uefi.org/specs/ACPI/6.5/15_System_Address_Map_Interfaces.html", "ACPI 6.5, chapter 15: System Address Map Interfaces (UEFI Forum)"],
		probe(run) {
			if (run.ramend)
				return { state: "open" };
			const done = MEV.obs.some((o) => o.kind === "e820" && o.ebx === 0);
			const usable = MEV.e820.filter((e) => e.type === 1);
			if (!done || !usable.length)
				return qsNotYet("This kernel never asks the BIOS for a memory map (no INT 15h E820h call).",
					usable.length ? "The BIOS is still answering the kernel's E820 calls." : "Waiting for the kernel to ask the BIOS for its memory map.");
			const top = usable.reduce((a, e) => (e.base + e.length > a.base + a.length ? e : a));
			run.ramend = { end: top.base + top.length, top, usable: usable.map((e) => [e.base, e.base + e.length]), ram: MEV.cpu.memory_size[0] >>> 0 };
			return { state: "open" };
		},
		ranges(run) {
			return run.ramend ? run.ramend.usable : [];
		},
		hintLabel: "E820 usable RAM",
		judge(run, v) {
			const end = run.ramend.end;
			if (v === end - 1 || v === end)
				return { ok: true };
			return { ok: false, msg: v < end ? "Lower than the end of usable RAM." : "Past the end of usable RAM: what does the BIOS say about it?" };
		},
		picked(run) {
			return qsPicked(run.ramend.end - 1, run.ramend.end);
		},
		won(run) {
			const r = run.ramend;
			return "Usable RAM ends at " + qsHex(r.end - 1) + " (E820 entry " + qsHex(r.top.base) + "-" + qsHex(r.end - 1) + ", type 1)." +
				(r.ram > r.end ? " The machine has " + qsNum(r.ram) + " bytes of RAM: the BIOS keeps the last " + qsNum(r.ram - r.end) + " for itself." : "");
		},
	},
	{
		id: "paint",
		title: "First paint",
		std: "IBM PC text mode memory",
		kind: "act",
		task: "Until the kernel takes over, the video BIOS writes the screen for everyone. Find the first time code running from RAM (the kernel's own code) wrote into the colour text memory at 0xB8000, and stop the machine on that instruction.",
		how: "Solved when the machine stands on that instruction, paused.",
		hint: "The map's story has a line 'the CPU running code in ... RAM first touched colour text (CGA) video memory'; its go there steps back to the kernel's first access there. That can be a read (a driver looks at what is there before it writes): then Step on, one instruction at a time, to the first write. The text memory blinks on the map now.",
		learn: "In the PC's colour text mode the screen is plain memory at 0xB8000: 80 x 25 cells of two bytes, a character (code page 437) and an attribute (the foreground colour in the low four bits, the background in the next three, blink in the top one). A byte written there changes the screen at once, with no call to anything. The video BIOS (INT 10h) writes the same memory; a kernel that writes it itself no longer needs the BIOS to print.",
		link: ["https://en.wikipedia.org/wiki/VGA_text_mode", "VGA text mode (Wikipedia)"],
		probe(run) {
			if (run.paint)
				return { state: "open" };
			if (MEV.missing)
				return { state: "never", why: "This v86 build reports no memory events." };
			if (!MEV.cols || MEV.since !== 0 || MEV.first() !== 0)
				return { state: "boot", why: "This one needs the memory events from the first instruction, and the log starts later (the quests were closed). Boot again with them open." };
			const c = MEV.cols, m = MEV.cap - 1;
			const i = qsScan(run, "paint", (k) => {
				const j = k & m;
				return (c.meta[j] & 3) === MEV_CPU && (c.meta[j] & MEV_M_WRITE) && c.addr[j] >= QS_TEXT_LO && c.addr[j] < QS_TEXT_HI &&
					c.eipPhys[j] >= 0 && MEV.nameOf(MEV.regionOf(c.eipPhys[j] >>> 0)).kind === "ram";
			});
			if (i < 0)
				return qsNotYet("This kernel never writes the text memory itself.", "The kernel has not written the screen itself yet: let it run.");
			run.paint = MEV.event(i);
			return { state: "open" };
		},
		ranges() {
			return [[QS_TEXT_LO, QS_TEXT_HI]];
		},
		hintLabel: "colour text memory",
		done(run) {
			return typeof stepper !== "undefined" && !!stepper && runState === "paused" && stepper.steps === run.paint.pos;
		},
		won(run) {
			const e = run.paint, st = MEV.st;
			const i = st && e.eipPhys >= 0 ? st.symbolOf(e.eipPhys) : -1;
			return "Instruction " + qsNum(e.pos) + ": " + (i >= 0 ? st.symName[i] + " at " : "") + "EIP " + qsHex(e.eip) + " wrote " + qsHex(e.value) + " to " + qsHex(e.addr) + ".";
		},
	},
	{
		id: "pciclass",
		title: "Who is on the bus?",
		std: "PCI class codes",
		kind: "choice",
		task: "The OS asks every PCI device what it is: it reads the class code from the device's configuration space through ports 0xCF8 and 0xCFC. What kind of device did ",
		hint: "The map's story has lines 'the guest learned PCI ...'. The base class is the byte at offset 0x0B of the device's configuration space, the subclass the byte at 0x0A. The device's memory regions blink on the map.",
		learn: "Every PCI function has 256 bytes of configuration space. On a PC the OS reaches it with configuration mechanism #1: it writes the bus, device, function and register number to port 0xCF8 (bit 31 set) and reads the data at 0xCFC. Bytes 0-3 are the vendor and device IDs, bytes 0x09-0x0B the class code: programming interface, subclass, base class. The base class says what kind of device it is (01 mass storage, 02 network, 03 display, 06 bridge), so an OS can pick a driver without knowing the vendor.",
		link: ["https://wiki.osdev.org/PCI", "PCI (OSDev wiki)"],
		probe(run) {
			if (run.pciclass)
				return { state: "open" };
			const os = qsOsStart(run);
			const seen = new Map();
			if (os !== null) {
				for (const o of MEV.obs) {
					if (o.kind !== "pci" || o.write || o.pos <= os || o.reg > 0xb || o.reg + o.size <= 0xb || seen.has(o.bdf))
						continue;
					const d = MEV.pci.get(o.bdf);
					if (d && (d.cfg[0] & d.cfg[1]) !== 0xff)
						seen.set(o.bdf, o.pos);
				}
			}
			if (!seen.size)
				return qsNotYet("This kernel never reads a PCI device's class code: try Debian.", "Waiting for the OS to read a PCI device's class code.");
			// a device with a memory region on the map, not a bridge, if any
			const mem = (bdf) => MEV.active.some((r) => r.name.startsWith("PCI " + pciBdf(bdf) + " "));
			const all = [...seen.keys()], cls = (bdf) => MEV.pci.get(bdf).cfg[0xb];
			const bdf = all.find((b) => cls(b) !== 0x06 && mem(b)) ?? all.find((b) => cls(b) !== 0x06) ?? all[0];
			const d = MEV.pci.get(bdf);
			const id = (k) => (d.cfg[k] | d.cfg[k + 1] << 8).toString(16).padStart(4, "0");
			run.pciclass = { bdf, cls: d.cfg[0xb], sub: d.cfg[0xa], id: id(0) + ":" + id(2), pos: seen.get(bdf),
				regions: MEV.active.filter((r) => r.name.startsWith("PCI " + pciBdf(bdf) + " ")).map((r) => [r.lo, r.hi]) };
			return { state: "open" };
		},
		taskEnd(run) {
			return run.pciclass ? "device " + pciBdf(run.pciclass.bdf) + " say it is?" : "a device say it is?";
		},
		choices: Object.keys(PCI_CLASSES).map(Number).filter((k) => k !== 0xff && k !== 0),
		choiceText: (k) => PCI_CLASSES[k][0],
		ranges(run) {
			return run.pciclass ? run.pciclass.regions : [];
		},
		hintLabel: "the device's regions",
		judge(run, k) {
			if (k === run.pciclass.cls)
				return { ok: true };
			return { ok: false, msg: "No: " + pciBdf(run.pciclass.bdf) + " is not a " + PCI_CLASSES[k][0] + ". Look at its base class, the byte at offset 0x0B." };
		},
		won(run) {
			const p = run.pciclass;
			return pciBdf(p.bdf) + " (" + p.id + ") answered " + pciClassName(p.cls, p.sub) + ", read at instruction " + qsNum(p.pos) + ".";
		},
	},
	{
		id: "barsize",
		title: "Size a BAR",
		std: "PCI BAR sizing",
		kind: "number",
		task: "To learn how much address space a device decodes, the OS writes all ones into a Base Address Register and reads it back. The OS sized one BAR first: how many bytes is it?",
		hint: "In the map's story the first line 'the guest learned PCI ... sized' after the E820 answers is the OS's; the ones before are the BIOS's. The regions the guest named from PCI BARs blink on the map.",
		learn: "A BAR (offsets 0x10-0x24 of configuration space) holds the address where a device's registers or memory appear. Its low bits are fixed: bit 0 says I/O ports or memory, and the address bits below the region's size always read as 0. So the OS saves the BAR, writes 0xFFFFFFFF, reads it back, masks the flag bits and gets the size as the two's complement of what is left (~value + 1). Then it writes an address back. The BIOS did the same earlier, to place the devices.",
		link: ["https://en.wikipedia.org/wiki/PCI_configuration_space#Bus_enumeration", "PCI configuration space: bus enumeration (Wikipedia)"],
		probe(run) {
			if (run.barsize)
				return { state: "open" };
			const os = qsOsStart(run);
			const f = os === null ? null : MEV.facts.find((x) => x.kind === "pci-size" && x.pos > os);
			if (!f)
				return qsNotYet("This kernel never sizes a PCI BAR: try Debian.", "Waiting for the OS to size a PCI BAR.");
			const d = MEV.pci.get(f.bdf), io = f.bar !== 0x30 && !!(d.sizes.get(f.bar) || {}).io;
			run.barsize = { bdf: f.bdf, bar: f.bar, size: f.size, io, pos: f.pos };
			return { state: "open" };
		},
		ranges() {
			return MEV.active.filter((r) => r.std === "PCI configuration mechanism #1").map((r) => [r.lo, r.hi]);
		},
		hintLabel: "regions named by PCI BARs",
		judge(run, v) {
			return qsJudgeNumber(v, run.barsize.size, "BAR");
		},
		won(run) {
			const b = run.barsize, name = b.bar === 0x30 ? "expansion ROM" : "BAR" + (b.bar - 0x10) / 4;
			return "The OS sized " + pciBdf(b.bdf) + " " + name + " first: " + qsNum(b.size) + " bytes of " + (b.io ? "I/O ports" : "memory") +
				" (the address bits below the size read back as 0: " + qsHex(~(b.size - 1)) + ") at instruction " + qsNum(b.pos) + ".";
		},
	},
	{
		id: "ioapic",
		title: "Find the I/O APIC",
		std: "MultiProcessor Specification table",
		kind: "act",
		task: "The OS learns where the I/O APIC is from a table the firmware leaves in memory, then programs it through its memory-mapped registers. Once the OS has touched it, click the I/O APIC on the live memory map.",
		how: "Solved when you click one of its cells.",
		hint: "The firmware's table begins with '_MP_' in the BIOS area; that spot blinks on the map now. The story's line 'the guest learned MultiProcessor Specification table' gives the I/O APIC's address.",
		learn: "The I/O APIC routes device interrupts to the CPUs. Its registers are memory-mapped, usually at 0xFEC00000, but the OS must not guess. The Intel MultiProcessor Specification 1.4 gives it a floating pointer structure, the signature '_MP_' on a 16-byte boundary in the EBDA, in the last KiB of base memory or in 0xF0000-0xFFFFF, which points to a configuration table listing the processors, buses and I/O APICs with their addresses. ACPI's MADT took its place on later machines; v86's SeaBIOS provides the MP table.",
		link: ["https://en.wikipedia.org/wiki/MultiProcessor_Specification", "MultiProcessor Specification (Wikipedia)"],
		probe(run) {
			if (run.ioapic)
				return { state: "open" };
			const regs = MEV.active.filter((r) => r.name.startsWith("I/O APIC"));
			if (!regs.length)
				return qsNotYet("No firmware table named an I/O APIC on this machine.", "No firmware table has named an I/O APIC yet.");
			const c = MEV.cols, m = MEV.cap - 1;
			const i = qsScan(run, "ioapic", (k) => (c.meta[k & m] & 3) === MEV_CPU && regs.some((r) => c.addr[k & m] >= r.lo && c.addr[k & m] < r.hi));
			if (i < 0)
				return qsNotYet("The OS never touched the I/O APIC: try Debian.", "The OS has not touched the I/O APIC yet.");
			const e = MEV.event(i), r = regs.find((x) => e.addr >= x.lo && e.addr < x.hi);
			run.ioapic = { lo: r.lo, hi: r.hi, name: r.name, std: r.std, pos: e.pos, mp: MEV.acpi && MEV.acpi.mp };
			return { state: "open" };
		},
		ranges(run) {
			const mp = MEV.acpi && MEV.acpi.mp;
			return mp ? [[mp, mp + 16]] : [[0xf0000, 0x100000]];
		},
		hintLabel: "_MP_ floating pointer",
		picked(run) {
			return qsPicked(run.ioapic.lo, run.ioapic.hi);
		},
		won(run) {
			const a = run.ioapic;
			return a.name + " at " + qsHex(a.lo) + ", named by the " + a.std + (a.mp ? " (its pointer at " + qsHex(a.mp) + ")" : "") +
				"; the OS first touched it at instruction " + qsNum(a.pos) + ".";
		},
	},
];

const QUESTS = {
	open: false,
	done: {},         // kernel -> { id: time solved }
	stored: true,     // localStorage works
	kernel: null,     // the kernel the cards are drawn for
	run: null,        // what the quests found in this run: { gen, scan, os, <id>: ... }
	cards: new Map(), // id -> { li, state, ... }
	timer: 0,
	lastPick: -1,
	active: null,     // the id of the quest whose card had the focus last
	wins: 0,          // celebrations shown (the tests read it)

	user: null,       // the "Watch this run" box as the user last set it (null: not touched)

	init() {
		this.load();
		const box = $("qs_on");
		const q = new URLSearchParams(location.search);
		let remembered = false;
		try {
			remembered = localStorage.getItem(QS_OPEN_KEY) === "1";
		} catch (e) { /* no storage: off */ }
		box.addEventListener("change", () => {
			this.user = box.checked;
			this.setOpen(box.checked, true);
		});
		$("qs_reset").addEventListener("click", () => this.reset());
		this.setOpen(q.get("quests") === "1" || (remembered && q.get("quests") !== "0"), false);
		this.timer = setInterval(() => this.tick(), QS_TICK_MS);
	},

	// tabs.js showTab: opening the Quests tab watches the run (remembered),
	// unless the user cleared the box; it goes on while other tabs are shown.
	shown(on) {
		if (on && !this.open && this.user !== false)
			this.setOpen(true, true);
	},

	setOpen(open, remember) {
		this.open = open;
		$("qs_on").checked = open;
		if (remember) {
			try {
				localStorage.setItem(QS_OPEN_KEY, open ? "1" : "0");
			} catch (e) { /* no storage */ }
		}
		MEV.need("quests", open);
		this.tick();
	},

	load() {
		try {
			this.done = JSON.parse(localStorage.getItem(QS_KEY) || "{}") || {};
		} catch (e) {
			this.done = {};
			this.stored = false;
		}
	},

	save() {
		try {
			localStorage.setItem(QS_KEY, JSON.stringify(this.done));
			this.stored = true;
		} catch (e) {
			this.stored = false;
		}
	},

	solvedHere() {
		return this.done[this.kernel] || {};
	},

	reset() {
		delete this.done[this.kernel];
		this.save();
		this.render();
		this.tick();
	},

	tick() {
		const k = typeof image === "object" && image ? image.name : "";
		if (k !== this.kernel) {
			this.kernel = k;
			this.render();
		}
		if (MEV.gen !== (this.run && this.run.gen))
			this.run = { gen: MEV.gen, scan: {}, never: {} };
		// closed too: the badge counts what this kernel can give
		this.update();
	},

	// ---- cards ---------------------------------------------------------------

	render() {
		const ol = $("qs_list");
		this.cards.clear();
		ol.replaceChildren(...QUEST_LIST.map((q, n) => {
			const li = document.createElement("li");
			li.className = "qs-card";
			li.dataset.q = q.id;
			li.dataset.state = "wait";
			// the quest played last: a map pick's "not that one" goes to it only
			li.addEventListener("focusin", () => { this.active = q.id; });
			li.addEventListener("pointerdown", () => { this.active = q.id; });
			const top = document.createElement("div");
			top.className = "qs-top";
			const mark = document.createElement("span");
			mark.className = "qs-mark";
			mark.textContent = String(n + 1);
			const h = document.createElement("h4");
			h.textContent = q.title;
			const std = document.createElement("span");
			std.className = "qs-std";
			std.textContent = q.std;
			const chip = document.createElement("span");
			chip.className = "qs-chip";
			top.append(mark, h, chip, std);
			const task = document.createElement("p");
			task.className = "qs-task";
			qsRich(task, q.task);
			const why = document.createElement("p");
			why.className = "note qs-why";
			const fb = document.createElement("p");
			fb.className = "qs-fb";
			fb.setAttribute("aria-live", "polite");
			const won = document.createElement("p");
			won.className = "qs-wontext";
			won.hidden = true;
			const card = { li, q, mark, chip, task, why, fb, won, state: "" };
			li.append(top, task, why);
			if (q.kind === "number" || q.kind === "address") {
				const form = document.createElement("form");
				form.className = "qs-form";
				const input = document.createElement("input");
				input.type = "text";
				input.inputMode = q.kind === "number" ? "numeric" : "text";
				input.autocomplete = "off";
				input.placeholder = q.kind === "number" ? "bytes" : "0x...";
				input.setAttribute("aria-label", "Answer to " + q.title);
				const go = document.createElement("button");
				go.type = "submit";
				go.textContent = "Check";
				form.append(input, go);
				form.addEventListener("submit", (ev) => {
					ev.preventDefault();
					this.answer(card, qsParse(input.value));
				});
				li.append(form);
				card.input = input;
			} else if (q.kind === "choice") {
				const g = document.createElement("div");
				g.className = "qs-choices";
				g.setAttribute("role", "group");
				g.setAttribute("aria-label", "Answers to " + q.title);
				for (const k of q.choices) {
					const b = document.createElement("button");
					b.type = "button";
					b.textContent = q.choiceText(k);
					b.dataset.choice = String(k);
					b.addEventListener("click", () => this.answer(card, k));
					g.append(b);
				}
				li.append(g);
			} else {
				const how = document.createElement("p");
				how.className = "note qs-how";
				how.textContent = q.how;
				li.append(how);
			}
			const tools = document.createElement("div");
			tools.className = "qs-tools";
			const hb = document.createElement("button");
			hb.type = "button";
			hb.className = "qs-hintbtn";
			hb.textContent = "Show me";
			hb.title = "A hint, and the place blinks on the live memory map";
			hb.addEventListener("click", () => this.hint(card));
			tools.append(hb);
			if (q.page !== undefined) {
				const pb = document.createElement("button");
				pb.type = "button";
				pb.className = "qs-page";
				pb.textContent = "Open " + qsHex(q.page * 4096) + " in the Page tab";
				pb.addEventListener("click", () => api.show.selectPage(q.page));
				tools.append(pb);
			}
			const again = document.createElement("button");
			again.type = "button";
			again.className = "qs-boot";
			again.textContent = "Boot again";
			again.hidden = true;
			again.addEventListener("click", () => api.machine.boot()); // boot.js
			tools.append(again);
			card.boot = again;
			const hint = document.createElement("p");
			hint.className = "qs-hint";
			hint.hidden = true;
			qsRich(hint, q.hint);
			card.hintText = hint;
			const learn = document.createElement("details");
			learn.className = "qs-learn";
			const ls = document.createElement("summary");
			ls.textContent = "The standard: " + q.std;
			const lp = document.createElement("p");
			const a = document.createElement("a");
			a.href = q.link[0];
			a.target = "_blank";
			a.rel = "noopener";
			a.textContent = q.link[1];
			lp.append(q.learn + " Source: ", a, ".");
			learn.append(ls, lp);
			li.append(fb, tools, hint, won, learn);
			this.cards.set(q.id, card);
			return li;
		}));
		this.progress();
	},

	// Each quest's state from the run; a solved one stays solved.
	update() {
		const solved = this.solvedHere(), run = this.run;
		for (const card of this.cards.values()) {
			const q = card.q;
			// what the run's end showed stays after a step back (the run is
			// deterministic: it ends the same way again)
			const s = !MEV.cpu ? { state: "wait", why: "Boot a kernel first." } : run.never[q.id] || q.probe(run);
			if (s.state === "never")
				run.never[q.id] = s;
			if (s.state === "open" && !solved[q.id]) {
				if (q.done && q.done(run))
					this.solve(card);
				else if (q.picked && MMAP.pick !== this.lastPick && MMAP.pick >= 0 && MMAP.segs.length) {
					if (q.picked(run))
						this.solve(card);
					else if (!this.active || this.active === q.id) // not while another quest is played
						this.feedback(card, "Not that one: your pick is " + this.pickText() + ".", true);
				}
			}
			this.show(card, solved[q.id] ? { state: "solved" } : s);
		}
		this.lastPick = MMAP.pick;
		this.progress();
	},

	pickText() {
		const [lo, hi] = MMAP.cellRange(MMAP.pick);
		return qsHex(lo) + "-" + qsHex(hi - 1) + " (" + MMAP.segName(MMAP.segs[MMAP.cellSeg[MMAP.pick]]) + ")";
	},

	show(card, s) {
		const q = card.q, li = card.li;
		if (q.taskEnd) {
			const t = q.task + q.taskEnd(this.run);
			if (card.task.textContent !== t)
				qsRich(card.task, t);
		}
		li.dataset.state = s.state;
		card.chip.textContent = qsChip(s);
		card.why.textContent = s.state === "open" || s.state === "solved" ? "" : s.why || "";
		card.why.hidden = !card.why.textContent;
		card.boot.hidden = s.state !== "boot";
		card.mark.textContent = s.state === "solved" ? "★" : String(QUEST_LIST.indexOf(q) + 1);
		// what was found: from this run once it has it, else when it was solved
		const info = !!(this.run && this.run[q.id]);
		if (s.state === "solved" && (card.state !== "solved" || (info && !card.wonInfo))) {
			qsRich(card.won, info ? q.won(this.run) : "Solved in this browser on " + new Date(this.solvedHere()[q.id]).toLocaleString("en-GB") + ".");
			card.wonInfo = info;
			card.won.hidden = false;
			card.fb.textContent = "";
		} else if (s.state !== "solved") {
			card.won.hidden = true;
		}
		card.state = s.state;
	},

	feedback(card, text, bad) {
		card.fb.textContent = text;
		card.fb.classList.toggle("bad", !!bad);
	},

	answer(card, v) {
		const q = card.q;
		if (card.state !== "open" || !this.run[q.id])
			return;
		if (typeof v === "number" && Number.isNaN(v)) {
			this.feedback(card, q.kind === "address" ? "Type an address, like 0x9fbff or 654335." : "Type a number of bytes, like 4096, 0x1000 or 4 KiB.", true);
			return;
		}
		const r = q.judge(this.run, v);
		if (r.ok)
			this.solve(card);
		else
			this.feedback(card, r.msg, true);
	},

	solve(card) {
		const q = card.q;
		if (!this.done[this.kernel])
			this.done[this.kernel] = {};
		this.done[this.kernel][q.id] = Date.now();
		this.save();
		this.show(card, { state: "solved" });
		this.progress();
		this.celebrate(card);
	},

	// A burst of the map's colours from the card's star, the card pops, and
	// a short note.
	celebrate(card) {
		this.wins++;
		const burst = document.createElement("span");
		burst.className = "qs-burst";
		burst.setAttribute("aria-hidden", "true");
		for (let k = 0; k < 16; k++) {
			const i = document.createElement("i"), a = k / 16 * 2 * Math.PI, r = 40 + (k % 3) * 18;
			i.style.setProperty("--dx", Math.round(Math.cos(a) * r) + "px");
			i.style.setProperty("--dy", Math.round(Math.sin(a) * r) + "px");
			burst.append(i);
		}
		card.mark.append(burst);
		this.burstBits = burst.children.length;
		card.li.classList.add("qs-pop");
		setTimeout(() => {
			burst.remove();
			card.li.classList.remove("qs-pop");
		}, 1500);
		const [n, of] = this.counts();
		const t = $("qs_toast");
		t.textContent = "Quest solved: " + card.q.title + ". " + n + " of " + of + " on this kernel" + (n === of ? ": all of them!" : ".");
		clearTimeout(this.toastTimer);
		this.toastTimer = setTimeout(() => { t.textContent = ""; }, 4000);
	},

	hint(card) {
		card.hintText.hidden = false;
		const q = card.q, run = this.run;
		if (card.state === "open" || card.state === "solved")
			MMAP.hint(q.ranges(run).filter(([lo, hi]) => hi > lo), q.hintLabel);
	},

	// Solved here, and how many this kernel can give (not "never").
	counts() {
		const solved = this.solvedHere();
		let n = 0, of = 0;
		for (const card of this.cards.values()) {
			if (solved[card.q.id])
				n++;
			if (solved[card.q.id] || card.state !== "never")
				of++;
		}
		return [n, of];
	},

	progress() {
		const [n, of] = this.counts();
		const meter = $("qs_meter");
		meter.setAttribute("aria-valuemax", String(of));
		meter.setAttribute("aria-valuenow", String(n));
		$("qs_fill").style.width = of ? (100 * n / of).toFixed(1) + "%" : "0";
		const all = Object.values(this.done).reduce((a, d) => a + Object.keys(d).length, 0);
		$("qs_progress").textContent = this.kernel ? n + " of " + of + " solved on " + this.kernel + (all > n ? " (" + all + " on all kernels)" : "") + "." : "Boot a kernel to start.";
		$("qs_badge").textContent = this.kernel ? "★ " + n + "/" + of : "";
		$("qs_note").textContent = this.stored ? "" : "This browser does not let the page store anything: what you solve is kept until the page is closed.";
	},
};

export { QUESTS };
