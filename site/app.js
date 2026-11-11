"use strict";

// Where v86 0.5.471 puts a bzImage (src/kernel.js, load_kernel): the
// real-mode part at 0x80000, the command line at 0x8e000, the 32-bit part
// at 1 MiB whatever loadflags/code32_start say.
const V86_SETUP_ADDR = 0x80000;
const V86_CMDLINE_ADDR = 0x8e000;
const V86_HEAP_END = 0x8de00;
const V86_KERNEL_ADDR = 0x100000;
const PAGE = 4096;
const DEFAULT_RAM = 2 * 1024 * 1024;
const LIVE_MS = 500;
// v86 0.5.471 src/kernel.js load_kernel: "Put the initrd at the 64 MB
// boundary" (INITRD_ADDRESS = 64 << 20), whatever the RAM size, and sets
// ramdisk_image/ramdisk_size in the header to it.
const V86_INITRD_ADDR = 64 << 20;

// The workflow stamps the script tags with ?v=<commit>; data fetched by the
// page gets the same stamp, so a cached older deploy is never mixed in.
const VERSION = (() => {
	try {
		return new URL(document.currentScript.src).searchParams.get("v");
	} catch (e) {
		return null;
	}
})();
const withV = (url) => (VERSION ? url + (url.includes("?") ? "&" : "?") + "v=" + encodeURIComponent(VERSION) : url);

const $ = (id) => document.getElementById(id);
const hex = (n) => "0x" + n.toString(16);
const kib = (n) => (n < 1024 ? n + " bytes" :
	(n / 1024).toLocaleString("en", { maximumFractionDigits: 1 }) + " KiB");

let kernels = [];
let image = null;      // { name, bytes: Uint8Array, hdr }
let initrd = null;     // { name, bytes: Uint8Array } passed to v86, or null
let emulator = null;
let emuRam = 0;          // RAM size the running emulator was started with
let emuReady = false;    // v86 finished loading (emulator-loaded)
let scanTimer = null;
let symbols = null;      // symbols.json of the current kernel, or null
let disasm = null;       // disasm.json of the current kernel, or null
let focusClass = null;   // page class picked from a legend or bar: highlight those pages
let scanNow = null;      // Uint8Array copy of guest RAM from the latest scan
let scanPrev = null;     // ... and from the scan before it
let usedPages = null;    // per page: 1 when the latest scan saw a non-zero byte
let changedPages = null; // per page: 1 when it differs between the last two scans
let pageClass = [];      // per page: CSS colour token (region or section)
let pageSections = [];   // per page: section classes it overlaps, or null
let grid = { cols: 32, cell: 14, pages: 0 };
let selectedPage = -1;
let liveTimer = null;

function setStatus(text) {
	$("status").textContent = text;
}

// ---- boot header ----------------------------------------------------------

function parseHeader(bytes) {
	const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
	const u8 = (o) => (o < bytes.length ? dv.getUint8(o) : 0);
	const u16 = (o) => (o + 2 <= bytes.length ? dv.getUint16(o, true) : 0);
	const u32 = (o) => (o + 4 <= bytes.length ? dv.getUint32(o, true) : 0);
	const h = {
		size: bytes.length,
		setup_sects: u8(0x1f1) || 4,
		syssize: u32(0x1f4),
		boot_flag: u16(0x1fe),
		magic: u32(0x202),
		version: u16(0x206),
		loadflags: u8(0x211),
		code32_start: u32(0x214),
		pref_address: u32(0x258),
		init_size: u32(0x260),
	};
	h.setup_size = (h.setup_sects + 1) * 512;
	h.payload = Math.max(0, h.size - h.setup_size);
	h.loaded_high = (h.loadflags & 1) !== 0;
	h.has_init_size = h.version >= 0x20a;
	// kernel_version (boot protocol 2.00+): offset from 0x200 of a
	// NUL-terminated string such as "6.1.0-50-686 (debian-kernel@...) #1 SMP ...".
	const kv = u16(0x20e);
	h.kernel_version = "";
	if (kv && kv + 0x200 < Math.min(bytes.length, h.setup_size))
		for (let o = kv + 0x200; o < Math.min(bytes.length, kv + 0x400) && u8(o); o++)
			h.kernel_version += String.fromCharCode(u8(o));
	return h;
}

// Highest physical address (exclusive) the kernel needs, as v86 loads it.
function kernelEnd(h) {
	let end = V86_KERNEL_ADDR + h.payload;
	if (h.has_init_size && h.pref_address >= V86_KERNEL_ADDR)
		end = Math.max(end, h.pref_address + h.init_size);
	return end;
}

function checkImage(h, ram) {
	const problems = [];
	if (h.magic !== 0x53726448)
		problems.push(["error", "No \"HdrS\" signature at 0x202: this is not a bzImage."]);
	if (h.boot_flag !== 0xaa55)
		problems.push(["error", "Boot flag at 0x1fe is " + hex(h.boot_flag) +
			", not 0xaa55: v86 refuses the image (\"Bad checksum1\")."]);
	if (problems.length)
		return problems;
	if (h.version < 0x202)
		problems.push(["warn", "Boot protocol " + verStr(h.version) +
			" is older than 2.02, which v86 assumes."]);
	if (!h.loaded_high)
		problems.push(["error", "LOADED_HIGH is clear: the kernel expects its 32-bit part at " +
			hex(h.code32_start) + ", but v86 ignores loadflags and copies it to 1 MiB, so it " +
			"will not start here. (qemu -kernel honours the flag.)"]);
	if (V86_KERNEL_ADDR + h.payload > ram)
		problems.push(["error", "v86 copies the " + kib(h.payload) + " 32-bit part to 1 MiB, but RAM " +
			"ends at " + hex(ram) + ". The loader cannot place the kernel: pick more RAM."]);
	else if (kernelEnd(h) > ram)
		problems.push(["warn", "The kernel says it needs memory up to " + hex(kernelEnd(h)) +
			" (pref_address + init_size), but RAM ends at " + hex(ram) + " (" + kib(ram) +
			"). init_size covers bss and brk in full; a kernel that does not touch all of them " +
			"can still boot, otherwise it fails early, typically with no output. " +
			kib(kernelEnd(h)) + " is enough."]);
	if (initrd) {
		const end = V86_INITRD_ADDR + initrd.bytes.length;
		if (end > ram)
			problems.push(["error", "v86 puts the initrd (" + kib(initrd.bytes.length) + ") at 64 MiB, so RAM must be " +
				"larger than " + kib(end) + "; it is " + kib(ram) + ". Pick more RAM."]);
		if (h.version >= 0x203) {
			const max = new DataView(image.bytes.buffer, image.bytes.byteOffset).getUint32(0x22c, true);
			if (max && end - 1 > max)
				problems.push(["warn", "The kernel accepts an initrd only below " + hex(max) + " (initrd_addr_max), but v86 " +
					"puts it at " + hex(V86_INITRD_ADDR) + "\u2013" + hex(end - 1) + "."]);
		}
		if (V86_KERNEL_ADDR + h.payload > V86_INITRD_ADDR || kernelEnd(h) > V86_INITRD_ADDR)
			problems.push(["warn", "The kernel's memory (up to " + hex(kernelEnd(h)) + ") reaches the initrd at 64 MiB."]);
	}
	return problems;
}

function verStr(v) {
	return (v >> 8) + "." + (v & 0xff).toString().padStart(2, "0");
}

function showHeader() {
	$("problems").textContent = "";
	$("kinfo").textContent = "";
	if (!image)
		return;
	const h = image.hdr;
	const parts = [h.size.toLocaleString("en") + " B", "proto " + verStr(h.version),
		"setup " + h.setup_size + " B", "32-bit " + (h.syssize * 16).toLocaleString("en") + " B @ " + hex(h.code32_start),
		h.loaded_high ? "LOADED_HIGH" : "low"];
	if (h.has_init_size)
		parts.push("init_size " + kib(h.init_size));
	$("kinfo").textContent = parts.join(" \u00b7 ");
	for (const [level, text] of checkImage(h, ram())) {
		const div = document.createElement("div");
		div.className = level;
		div.textContent = text;
		$("problems").append(div);
	}
}

// ---- memory map -----------------------------------------------------------

const REGION_NAMES = {
	"r-bios": "IVT, BDA, EBDA",
	"r-conv": "conventional RAM",
	"r-setup": "real-mode setup, heap, command line",
	"r-vga": "VGA window",
	"r-rom": "VGA BIOS, option ROMs, BIOS",
	"r-kernel": "32-bit kernel image",
	"r-init": "kernel init_size (runtime footprint)",
	"r-ext": "free extended RAM",
	"r-initrd": "initrd",
	"r-hole": "beyond RAM",
};

const SECTION_NAMES = {
	"s-text": "text",
	"s-rodata": "rodata",
	"s-data": "data",
	"s-bss": "bss",
	"s-brk": "brk",
	"s-other": "other section",
};

function ram() {
	return Number($("memory").value);
}

// Tapping a legend entry or a bar segment highlights every page of that class.
function setFocus(cls) {
	focusClass = focusClass === cls ? null : cls;
	drawGrid();
	const legend = $("legend");
	for (const li of legend.children)
		li.setAttribute("aria-pressed", li.dataset.cls === focusClass);
	const info = $("focus_info");
	if (!focusClass) {
		info.textContent = "";
		return;
	}
	let n = 0, used = 0, first = -1, last = -1;
	for (let p = 0; p < pageClass.length; p++)
		if (pageClass[p] === focusClass) {
			n++;
			if (usedPages && usedPages[p])
				used++;
			if (first < 0)
				first = p;
			last = p;
		}
	const name = REGION_NAMES[focusClass] || ("kernel " + (SECTION_NAMES[focusClass] || focusClass));
	info.textContent = n ? name + ": " + n + " pages (" + kib(n * PAGE) + "), " +
		(usedPages ? used + " non-zero, " : "") + hex(first * PAGE) + "–" + hex(last * PAGE + PAGE - 1) +
		". Tap again to clear." : name + ": no pages in RAM.";
}

function legendItem(cls, label, style) {
	const li = document.createElement("li");
	const sw = document.createElement("span");
	Object.assign(sw.style, style || { background: "var(--" + cls + ")" });
	li.append(sw, label);
	if (cls) {
		li.dataset.cls = cls;
		li.className = "pick";
		li.tabIndex = 0;
		li.setAttribute("role", "button");
		li.setAttribute("aria-pressed", cls === focusClass);
		li.title = "Highlight every page of this type";
		li.addEventListener("click", () => setFocus(cls));
		li.addEventListener("keydown", (e) => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); setFocus(cls); } });
	}
	return li;
}

function drawBar(el, regions, start, end) {
	el.textContent = "";
	el.dataset.lo = start;
	el.dataset.hi = end;
	memEipKey = "";
	for (const [a, b, cls, label] of regions) {
		const lo = Math.max(a, start), hi = Math.min(b, end);
		if (hi <= lo)
			continue;
		const div = document.createElement("div");
		div.style.flex = (hi - lo) + " 0 0";
		div.style.background = "var(--" + cls + ")";
		div.dataset.lo = lo;
		div.dataset.hi = hi;
		div.title = label + ": " + hex(lo) + "–" + hex(hi) + " (" + kib(hi - lo) + ")";
		div.textContent = label;
		if (label !== "first MiB")
			div.addEventListener("click", () => setFocus(cls));
		el.append(div);
	}
}

function drawMaps() {
	const r = ram();
	$("ram_label").textContent = kib(r);
	const low = [];
	const top = Math.min(r, 0x9fc00);
	low.push([0, 0x500, "r-bios", "IVT+BDA"]);
	if (image) {
		const setupEnd = V86_SETUP_ADDR + image.hdr.setup_size;
		low.push([0x500, Math.min(top, V86_SETUP_ADDR), "r-conv", "conventional"]);
		low.push([V86_SETUP_ADDR, Math.min(top, setupEnd), "r-setup", "setup"]);
		low.push([setupEnd, Math.min(top, V86_CMDLINE_ADDR + 0x100), "r-setup", "heap+cmdline"]);
		low.push([V86_CMDLINE_ADDR + 0x100, top, "r-conv", ""]);
	} else {
		low.push([0x500, top, "r-conv", "conventional"]);
	}
	low.push([0x9fc00, 0xa0000, "r-bios", "EBDA"]);
	low.push([0xa0000, 0xc0000, "r-vga", "VGA"]);
	low.push([0xc0000, 0xc8000, "r-rom", "VGA BIOS"]);
	low.push([0xc8000, 0xe0000, "r-rom", "option ROMs"]);
	low.push([0xe0000, 0x100000, "r-rom", "BIOS"]);
	drawBar($("map_low"), low, 0, 0x100000);

	const all = [[0, 0x100000, "r-bios", "first MiB"]];
	let end = r;
	if (image) {
		const h = image.hdr;
		const kEnd = V86_KERNEL_ADDR + h.payload;
		const need = kernelEnd(h);
		all.push([V86_KERNEL_ADDR, kEnd, "r-kernel", "kernel"]);
		all.push([kEnd, need, "r-init", "init_size"]);
		if (initrd) {
			const ia = V86_INITRD_ADDR, ib = V86_INITRD_ADDR + initrd.bytes.length;
			all.push([need, Math.min(r, ia), "r-ext", "free"]);
			all.push([ia, ib, "r-initrd", "initrd"]);
			all.push([ib, r, "r-ext", "free"]);
			end = Math.max(r, need, ib);
		} else {
			all.push([need, r, "r-ext", "free"]);
			end = Math.max(r, need);
		}
		all.push([r, end, "r-hole", "beyond RAM"]);
	} else {
		all.push([0x100000, r, "r-ext", "free"]);
	}
	drawBar($("map_all"), all, 0, end);
	computePageClasses();
	drawGrid();

	const legend = $("legend");
	legend.textContent = "";
	for (const [cls, name] of Object.entries(REGION_NAMES))
		legend.append(legendItem(cls, name));
}

// ---- page grid ------------------------------------------------------------

// RAM the grid describes: the running machine's, else the menu's.
function gridRam() {
	return emulator ? emuRam : ram();
}

// Colour token of every 4 KiB page: the region the boot header and v86's
// loader imply, refined by ELF section when symbols.json is loaded.
function computePageClasses() {
	const r = gridRam();
	const pages = r / PAGE;
	const cls = new Array(pages);
	let kEnd = 0, need = 0;
	if (image) {
		kEnd = V86_KERNEL_ADDR + image.hdr.payload;
		need = kernelEnd(image.hdr);
	}
	for (let p = 0; p < pages; p++) {
		const a = p * PAGE;
		let c;
		if (a < 0x1000 || (a >= 0x9f000 && a < 0xa0000))
			c = "r-bios";
		else if (a < 0x9f000)
			c = image && a + PAGE > V86_SETUP_ADDR && a < V86_CMDLINE_ADDR + 0x100 ? "r-setup" : "r-conv";
		else if (a < 0xc0000)
			c = "r-vga";
		else if (a < 0x100000)
			c = "r-rom";
		else if (image && a < kEnd)
			c = "r-kernel";
		else if (image && a < need)
			c = "r-init";
		else if (initrd && a + PAGE > V86_INITRD_ADDR && a < V86_INITRD_ADDR + initrd.bytes.length)
			c = "r-initrd";
		else
			c = "r-ext";
		cls[p] = c;
	}
	pageSections = new Array(pages).fill(null);
	if (symbols) {
		const best = new Float64Array(pages);
		for (const s of symbols.sections) {
			const lo = s.paddr, hi = s.paddr + s.size;
			for (let p = Math.floor(lo / PAGE); p < pages && p * PAGE < hi; p++) {
				const n = Math.min(hi, (p + 1) * PAGE) - Math.max(lo, p * PAGE);
				pageSections[p] = pageSections[p] || [];
				if (!pageSections[p].includes(s.class))
					pageSections[p].push(s.class);
				if (n > best[p]) {
					best[p] = n;
					cls[p] = "s-" + s.class;
				}
			}
		}
	}
	pageClass = cls;
}

function drawGrid() {
	const pages = gridRam() / PAGE;
	if (pageClass.length !== pages)
		computePageClasses();
	const cols = pages <= 1024 ? 32 : 64;
	const cell = pages <= 1024 ? 14 : 9;
	grid = { cols, cell, pages };
	const canvas = $("pages");
	canvas.width = cols * cell;
	canvas.height = Math.ceil(pages / cols) * cell;
	canvas.dataset.cols = cols;
	canvas.dataset.cell = cell;
	canvas.dataset.pages = pages;
	canvas.dataset.draws = Number(canvas.dataset.draws || 0) + 1;
	const ctx = canvas.getContext("2d");
	const css = getComputedStyle(document.documentElement);
	const colour = {};
	const col = (k) => colour[k] || (colour[k] = css.getPropertyValue("--" + k).trim());
	const used = usedPages && usedPages.length === pages ? usedPages : null;
	const changed = changedPages && changedPages.length === pages ? changedPages : null;
	ctx.clearRect(0, 0, canvas.width, canvas.height);
	for (let p = 0; p < pages; p++) {
		const x = (p % cols) * cell, y = Math.floor(p / cols) * cell;
		ctx.globalAlpha = used ? 1 : 0.6;
		if (focusClass && pageClass[p] !== focusClass)
			ctx.globalAlpha = 0.12;
		// Pages with data are filled, all-zero pages are hollow.
		if (used && !used[p]) {
			ctx.strokeStyle = col(pageClass[p]);
			ctx.lineWidth = 1;
			ctx.strokeRect(x + 0.5, y + 0.5, cell - 2, cell - 2);
		} else {
			ctx.fillStyle = col(pageClass[p]);
			ctx.fillRect(x, y, cell - 1, cell - 1);
		}
		ctx.globalAlpha = 1;
		if (changed && changed[p]) {
			const d = Math.max(2, Math.floor(cell / 3));
			ctx.fillStyle = col("changed");
			ctx.fillRect(x + (cell - 1 - d) / 2, y + (cell - 1 - d) / 2, d, d);
		}
	}
	if (focusClass) {
		ctx.strokeStyle = col("fg");
		ctx.lineWidth = 1;
		for (let p = 0; p < pages; p++)
			if (pageClass[p] === focusClass)
				ctx.strokeRect((p % cols) * cell + 0.5, Math.floor(p / cols) * cell + 0.5, cell - 2, cell - 2);
	}
	if (selectedPage >= 0 && selectedPage < pages) {
		ctx.strokeStyle = col("fg");
		ctx.lineWidth = 2;
		ctx.strokeRect((selectedPage % cols) * cell, Math.floor(selectedPage / cols) * cell,
			cell - 1, cell - 1);
	}
	drawGridLegend();
	renderMemEip();
	bmRenderPages();
}

// ---- EIP in the Memory tab (M1) --------------------------------------------
// The page square of the next instruction's physical address (the one the
// Debugger shows, through the page tables) gets a box, the stack's page
// (SS base + ESP) a dashed one, each labelled left of its row; the region
// bars a frame around the region holding EIP and a tick where it is; the
// line above the grid says where EIP is and opens the Page tab there. The
// boxes are elements over the canvas: following EIP moves them and redraws
// nothing else. Drawn with the Debugger (drawDebugUi, its redraw budget)
// and with the grid, only while the Memory tab is shown.
let memEipKey = "";

function memEipWhere() {
	if (!stepper || !emulator || runState === "off")
		return { text: "EIP: no kernel is running." };
	if (runState === "jit" || ffActive)
		return { text: "EIP: the CPU runs at full speed (JIT); its page is marked when it stops." };
	const r = stepper.regs(), c = stepper.cpu;
	const phys = stepper.nextPhys();
	const esp = (c.segment_offsets[2] + ((r.cr0 & 1) ? r.esp : r.esp & 0xffff)) >>> 0;
	const sp = pageWalk(c, esp).phys;
	return { eip: r.eip, phys, esp: r.esp, sphys: sp === null ? -1 : sp, steps: stepper.steps };
}

// Left, top, width and height of page p's square in #pages_box (the
// canvas, its 1 px border inside).
function memCellStyle(el, p, rows) {
	const col = p % grid.cols, row = Math.floor(p / grid.cols);
	el.style.left = "calc(1px + (100% - 2px) * " + col / grid.cols + ")";
	el.style.top = "calc(1px + (100% - 2px) * " + row / rows + ")";
	el.style.width = "calc((100% - 2px) / " + grid.cols + ")";
	el.style.height = "calc((100% - 2px) / " + rows + ")";
}

function memMarkBars(phys) {
	for (const id of ["map_low", "map_all"]) {
		const bar = $(id);
		for (const d of bar.querySelectorAll(".eiphere, .eiptick"))
			d.classList.contains("eiptick") ? d.remove() : d.classList.remove("eiphere");
		if (phys < 0)
			continue;
		let hit = null;
		for (const d of bar.children)
			if (Number(d.dataset.lo) <= phys && phys < Number(d.dataset.hi))
				hit = d;
		if (!hit)
			continue;
		hit.classList.add("eiphere");
		const t = document.createElement("span");
		t.className = "eiptick";
		t.style.left = (100 * (phys - Number(bar.dataset.lo)) / (Number(bar.dataset.hi) - Number(bar.dataset.lo))) + "%";
		bar.append(t);
	}
}

function renderMemEip() {
	if (tab !== "memory")
		return;
	const w = memEipWhere();
	const pages = grid.pages, rows = Math.ceil(pages / grid.cols);
	const inGrid = (a) => a >= 0 && a < pages * PAGE;
	const key = [w.text || "", w.steps, w.phys, w.sphys, pages, grid.cols].join(" ");
	if (key === memEipKey)
		return;
	memEipKey = key;
	const line = $("mem_eip"), mark = $("pages_eip"), tag = $("pages_eip_tag");
	const smark = $("pages_esp"), stag = $("pages_esp_tag");
	if (w.text) {
		line.textContent = w.text;
		mark.hidden = tag.hidden = smark.hidden = stag.hidden = true;
		delete mark.dataset.page;
		memMarkBars(-1);
		return;
	}
	const p = inGrid(w.phys) ? Math.floor(w.phys / PAGE) : -1;
	const sp = inGrid(w.sphys) ? Math.floor(w.sphys / PAGE) : -1;
	line.textContent = "";
	line.append("EIP 0x" + h8(w.eip) + " (" + (w.phys < 0 ? "not mapped: the next fetch faults" :
		"phys 0x" + h8(w.phys) + (p < 0 ? ", beyond RAM" : "")) + ") ");
	if (p >= 0) {
		const i = stepper.symbolOf(w.phys);
		const off = i >= 0 ? w.phys - stepper.symStart[i] : 0;
		if (i < 0 && (symbols || w.phys < V86_KERNEL_ADDR))
			line.append(symDesc(stepper, i, w.phys) + " ");
		const go = document.createElement("button");
		go.type = "button";
		go.id = "mem_eip_go";
		go.className = "symlink";
		go.textContent = (i >= 0 ? stepper.symName[i] + (off ? "+" + hex(off) : "") + ", " : "") + "page " + hex(p);
		go.title = "Open the Page tab at EIP";
		go.addEventListener("click", () => jumpTo(w.phys));
		line.append(go);
	}
	line.append(" \u00b7 ESP 0x" + h8(w.esp) + (w.sphys < 0 ? " (not mapped)" : " (phys 0x" + h8(w.sphys) + ") "));
	if (sp >= 0) {
		const go = document.createElement("button");
		go.type = "button";
		go.id = "mem_esp_go";
		go.className = "symlink";
		go.textContent = "stack, page " + hex(sp);
		go.title = "Open the Page tab at ESP";
		go.addEventListener("click", () => jumpTo(w.sphys));
		line.append(go);
	}
	mark.hidden = tag.hidden = p < 0;
	if (p >= 0) {
		memCellStyle(mark, p, rows);
		tag.style.top = "calc(1px + (100% - 2px) * " + (Math.floor(p / grid.cols) + 0.5) / rows + ")";
	}
	smark.hidden = stag.hidden = sp < 0;
	if (sp >= 0) {
		memCellStyle(smark, sp, rows);
		stag.style.top = "calc(1px + (100% - 2px) * " + (Math.floor(sp / grid.cols) + 0.5) / rows + ")";
		const same = p >= 0 && Math.floor(p / grid.cols) === Math.floor(sp / grid.cols);
		stag.classList.toggle("low", same);
		tag.classList.toggle("up", same);
	} else
		tag.classList.remove("up");
	mark.dataset.page = p;
	mark.dataset.phys = w.phys;
	smark.dataset.page = sp;
	// Last: the test's observer checks the CPU when this changes.
	mark.dataset.steps = w.steps;
	memMarkBars(p >= 0 ? w.phys : -1);
}

function drawGridLegend() {
	const legend = $("grid_legend");
	legend.textContent = "";
	const present = new Set(pageClass);
	const add = (cls, label, style) => legend.append(legendItem(cls, label, style));
	for (const [cls, name] of Object.entries(REGION_NAMES))
		if (present.has(cls))
			add(cls, name);
	for (const [cls, name] of Object.entries(SECTION_NAMES))
		if (present.has(cls))
			add(cls, "kernel " + name);
	add("", "page with data (filled)", { background: "var(--r-kernel)" });
	add("", "all-zero page (hollow)", { border: "1px solid var(--r-kernel)", boxSizing: "border-box" });
	add("", "changed since the previous scan", { background: "var(--changed)", transform: "scale(0.5)" });
	add("", "selected", { outline: "2px solid var(--fg)", outlineOffset: "-2px" });
}

// The About tab's legend: the swatches drawn from the page's own tables
// (memory regions and sections as in the Memory tab, the Graph's
// subsystems), and the Legend links of Code at EIP and the Page listing.
function initLegend() {
	const item = (key, label, style) => {
		const li = document.createElement("li");
		const sw = document.createElement("span");
		Object.assign(sw.style, style);
		li.dataset.key = key;
		li.append(sw, label);
		return li;
	};
	const mem = $("legend_mem");
	for (const [cls, name] of Object.entries(REGION_NAMES))
		mem.append(item(cls, name, { background: "var(--" + cls + ")" }));
	for (const [cls, name] of Object.entries(SECTION_NAMES))
		mem.append(item(cls, "kernel " + name, { background: "var(--" + cls + ")" }));
	mem.append(item("", "page with data (filled)", { background: "var(--r-kernel)" }),
		item("", "all-zero page (hollow)", { border: "1px solid var(--r-kernel)", boxSizing: "border-box" }),
		item("changed", "changed since the previous scan", { background: "var(--changed)", transform: "scale(0.5)" }),
		item("", "selected page", { outline: "2px solid var(--fg)", outlineOffset: "-2px" }));
	const graph = $("legend_graph");
	for (const [dir, colour] of Object.entries(PALETTE))
		graph.append(item(dir, dir === "other" ? "other or unknown" : dir + "/", { background: colour }));
	graph.append(item("irq", "interrupt and exception handlers (timeline)", { background: IRQ_COLOUR }),
		item("switch", "task switch (timeline line)", { background: SWITCH, height: "2px" }));
	for (const b of document.querySelectorAll(".legendlink"))
		b.addEventListener("click", () => navJump(() => {
			showTab("about");
			// Below the header, which stays on top on a wide screen.
			const top = document.querySelector("header.top");
			const over = getComputedStyle(top).position === "sticky" ? top.getBoundingClientRect().height : 0;
			window.scrollTo(0, window.scrollY + $("about_legend").getBoundingClientRect().top - over - 8);
		}));
}

function pageAt(ev) {
	const canvas = $("pages");
	const rect = canvas.getBoundingClientRect();
	const x = (ev.clientX - rect.left) * canvas.width / rect.width;
	const y = (ev.clientY - rect.top) * canvas.height / rect.height;
	const c = Math.floor(x / grid.cell), row = Math.floor(y / grid.cell);
	if (c < 0 || c >= grid.cols || row < 0)
		return -1;
	const p = row * grid.cols + c;
	return p < grid.pages ? p : -1;
}

function pageLabel(p) {
	const cls = pageClass[p] || "";
	if (pageSections[p])
		return "kernel " + pageSections[p].join("+");
	return REGION_NAMES[cls] || (SECTION_NAMES[cls] ? "kernel " + SECTION_NAMES[cls] : cls);
}

// Symbols whose [paddr, paddr+size) overlaps page p (size 0 counts as 1
// byte), ordered by address.
function symbolsInPage(p) {
	if (!symbols)
		return [];
	const lo = p * PAGE, hi = lo + PAGE;
	return symbols.symbols.filter((s) => s.paddr < hi && s.paddr + Math.max(s.size, 1) > lo);
}

const symRange = (s) => hex(s.paddr) + (s.size > 1 ? "\u2013" + hex(s.paddr + s.size - 1) : "");

// Symbol (with a size) containing physical address a, from a list ordered
// by address; the innermost (latest-starting) one wins.
function symbolAt(list, a) {
	let best = null;
	for (const s of list) {
		if (s.paddr > a)
			break;
		if (s.size > 0 && a < s.paddr + s.size)
			best = s;
	}
	return best;
}

function showHover(p) {
	const el = $("hover_info");
	if (p < 0) {
		el.textContent = "";
		return;
	}
	const syms = symbolsInPage(p);
	let text = "page " + hex(p * PAGE) + ", " + pageLabel(p);
	if (usedPages && usedPages.length === grid.pages)
		text += usedPages[p] ? ", data" : ", all zero";
	if (syms.length)
		text += ": " + syms[0].name + (syms.length > 1 ? " and " + (syms.length - 1) + " more symbols" : "");
	if (!$("pages_eip").hidden && Number($("pages_eip").dataset.page) === p)
		text += "; EIP is here";
	if (!$("pages_esp").hidden && Number($("pages_esp").dataset.page) === p)
		text += "; ESP (the stack) is here";
	el.textContent = text;
}

// ---- page listing ---------------------------------------------------------

function readPage(p) {
	return emulator.read_memory(p * PAGE, PAGE).slice();
}

const asciiOf = (b) => (b >= 0x20 && b < 0x7f ? String.fromCharCode(b) : ".");
const esc = (c) => (c === "<" ? "&lt;" : c === ">" ? "&gt;" : c === "&" ? "&amp;" : c);
const escHtml = (t) => t.replace(/[<>&"]/g, (c) => ({ "<": "&lt;", ">": "&gt;", "&": "&amp;", '"': "&quot;" })[c]);

// Characters of the listing's monospace font that fit in one line.
let charWidth = 0, charFont = "";
function lineChars(el) {
	const cs = getComputedStyle(el);
	const font = cs.fontSize + " " + cs.fontFamily;
	if (!charWidth || charFont !== font) {
		const ctx = document.createElement("canvas").getContext("2d");
		ctx.font = font;
		charFont = font;
		charWidth = ctx.measureText("0123456789abcdef").width / 16 || 7;
	}
	const w = el.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight);
	return Math.max(20, Math.floor(w / charWidth) - 1);
}

// Bytes per hex row: the most of 16, 8, 4 whose row fits.
function hexPer(chars) {
	for (const per of [16, 8, 4])
		if (8 + 1 + per * 3 + (per === 16 ? 1 : 0) + 2 + per + 1 <= chars)
			return per;
	return 4;
}

// Index of the first instruction at or after physical address a.
function insnIndex(a) {
	const ins = disasm.insns;
	let lo = 0, hi = ins.length;
	while (lo < hi) { const m = (lo + hi) >> 1; if (ins[m][0] < a) lo = m + 1; else hi = m; }
	return lo;
}

// Whether the listing of the page at lo can tint the instructions that
// have not executed: only what the stepper stepped is recorded (Stepper.seen,
// by physical address), not what v86's JIT ran.
function coverageFor(lo) {
	if (!stepper || !stepper.unique)
		return { tint: false, why: "nothing has been stepped yet" };
	if (stepper.jitBeforeEntry && (stepper.sinceEntry < 0 || lo < stepper.kernelLo))
		return { tint: false, why: "the code before the kernel entry ran on v86's JIT (fast-forward), which records nothing" };
	return { tint: true, partial: stepper.jitAfterEntry };
}

// One listing of page p in address order: instructions (disasm.json) as
// "addr bytes insn symbol+off", everything else as hexdump -C rows, symbol
// starts as "name:" headers. base: bytes from the last scan (changed bytes
// are marked; how: since when, for their popover); cur: physical address of
// the next instruction, or -1. Instructions that have not executed in this
// boot are tinted (coverageFor); instructions whose bytes differ from the
// build are yellow and say why (diffTip).
function listPage(p, bytes, base, cur, chars, how) {
	const lo = p * PAGE, hi = lo + PAGE;
	const cov = coverageFor(lo);
	const per = hexPer(chars);
	const wide = chars >= 100;
	const syms = symbolsInPage(p).filter((s) => s.paddr >= lo);
	const sized = symbolsInPage(p).filter((s) => s.size > 0);
	let insns = new Map();
	let source = "none", misaligned = new Set(), bits = 32;
	if (disasm && !liveDecode())
		for (let i = insnIndex(lo); i < disasm.insns.length && disasm.insns[i][0] < hi; i++)
			insns.set(disasm.insns[i][0], disasm.insns[i]);
	if (insns.size)
		source = "build";
	else {
		// No disasm.json (or "live decode"): decode guest memory with iced-x86.
		const live = liveInsnsForPage(p);
		if (live) {
			insns = live.insns;
			misaligned = live.misaligned;
			bits = live.bits;
			source = "live";
		} else if (!icedError)
			loadIced();
	}
	// Jump paths: the build's (whole kernel) or, decoded live, this page's.
	const jS = !insns.size ? null : source === "build" ? jumpsFor(disasm.insns) : jumpsFor([...insns.values()].sort((x, y) => x[0] - y[0]));
	const paths = new Map();
	const inView = (x) => insns.has(x), imax = jumpInlineMax($("listing"));
	const out = [];
	let si = 0, nIns = 0, nBad = 0, nNx = 0, nRows = 0, nonzero = 0, changed = 0, curLine = -1;
	let prevRow = null, starred = false;
	const srcState = { key: null };
	const ops = insns.size && bits !== 16 ? opEnv() : null, opStat = {}; // real-mode addresses are segment-relative
	const mark = (off, text) => (base && base[off] !== bytes[off] ? "<mark>" + text + "</mark>" : text);
	const scanTip = "Yellow: the marked bytes changed " + (how === "since the previous scan" ?
		"between the previous memory scan and the last one (Memory tab, Scan)." :
		"since the last memory scan. The page scans memory at every stop, every few frames while the CPU runs, and on Scan.");
	for (let off = 0; off < PAGE; off++)
		if (bytes[off]) nonzero++;
	if (base)
		for (let off = 0; off < PAGE; off++)
			if (base[off] !== bytes[off]) changed++;
	let a = lo;
	while (a < hi) {
		while (si < syms.length && syms[si].paddr <= a) {
			const s = syms[si++];
			if (s.paddr === a || !insns.has(s.paddr)) {
				const ty = varTypeOf(s);
				// A function's label line: its prototype, and its facts in the title.
				const fl = ty ? null : fnLabelOf(s);
				out.push('<span class="lbl" data-sym="' + escHtml(s.name) + '" data-addr="' + s.paddr + '"' + (ty ? ' data-type="' + escHtml(ty) + '"' : "") +
					(fl ? ' data-proto="' + escHtml(fl.proto) + '" title="' + escHtml(fl.title) + '"' : "") + ">" +
					escHtml(s.name) + ":" + (s.size || ty ? ' <span class="dim">' + (ty ? escHtml(ty) + (s.size ? ", " : "") : "") +
						(s.size ? s.size + " bytes, " + s.section : "") + "</span>" : "") + (fl ? ' <span class="srctext">' + escHtml(fl.proto) + "</span>" : "") + bmFnStar(s.paddr) + "</span>");
			}
			prevRow = null;
		}
		const nextSym = si < syms.length ? syms[si].paddr : hi;
		const ins = insns.get(a);
		if (ins) {
			const [pa, raw, text] = ins;
			const n = raw.length / 2;
			let ok = true, rawHtml = "", scanned = false;
			for (let k = 0; k < n && pa + k < hi; k++) {
				const want = parseInt(raw.substr(k * 2, 2), 16);
				if (bytes[pa - lo + k] !== want)
					ok = false;
				if (base && base[pa - lo + k] !== bytes[pa - lo + k])
					scanned = true;
				if (k < 7)
					rawHtml += mark(pa - lo + k, bytes[pa - lo + k].toString(16).padStart(2, "0"));
			}
			const m = Math.min(n, hi - pa);
			const tip = !ok ? diffTip(pa, raw.substr(0, m * 2), bytes.subarray(pa - lo, pa - lo + m)) : scanned ? scanTip : "";
			if (tip)
				rawHtml = diffBytesHtml(rawHtml, tip);
			rawHtml += n > 7 ? "…" : "  ".repeat(7 - n) + " ";
			const s = symbolAt(sized, pa);
			const where = s ? s.name + "+0x" + (pa - s.paddr).toString(16) : "";
			let cls = ok ? "ins" : "ins bad";
			if (misaligned.has(pa))
				cls += " maybe";
			if (cov.tint && !stepper.executed(pa)) {
				cls += " nx";
				nNx++;
			}
			// EIP inside this instruction: the bytes there are not the
			// build's (the relocation stub at 0x100000 runs over vmlinux's
			// startup_32); the line holding EIP gets the mark.
			const inside = cur > pa && cur < pa + n;
			if (pa === cur || inside) {
				cls += inside ? " cur mid tipmark" : " cur";
				curLine = out.length;
			}
			const srcLine = srcLineHtml(pa, srcState);
			if (srcLine)
				out.push(srcLine);
			if (pa === cur || inside)
				curLine = out.length;
			if (pa === jumpHit)
				cls += " jhit";
			const P = jumpPaths(jS, stepper, pa);
			paths.set(pa, P);
			const J = jumpInlineHtml(jS, P, pa, inView, imax);
			out.push(J.src + '<span class="' + cls + '" data-addr="' + pa + '"' + (inside ? ' data-eip="' + cur + '"' : "") +
				(inside || tip ? ' data-tip="' + escHtml((tip ? tip + (inside ? "\n\n" : "") : "") + (inside ? midTitle(cur) : "")) + '"' : "") +
				jumpStubAttrs(J) + ">" + pa.toString(16).padStart(8, "0") +
				" " + rawHtml + " " + insnHtml(pa, text, ops, opStat) +
				(wide && where ? ' <span class="dim">' + escHtml(where) + "</span>" : "") + bmCodeStar(pa) + "</span>" + J.dst + jumpBadgesHtml(P, pa));
			nIns++;
			if (!ok)
				nBad++;
			a += Math.max(1, n);
			prevRow = null;
			continue;
		}
		// Hex row up to the row boundary, the next instruction or symbol.
		let end = Math.min(hi, (Math.floor((a - lo) / per) + 1) * per + lo, nextSym);
		for (let b = a + 1; b < end; b++)
			if (insns.has(b)) { end = b; break; }
		const full = end - a === per && (a - lo) % per === 0;
		const row = bytes.subarray(a - lo, end - lo);
		const dirty = base && row.some((v, i) => base[a - lo + i] !== v);
		const hasCur = cur >= a && cur < end;
		if (full && !dirty && !hasCur && prevRow && row.every((v, i) => v === prevRow[i])) {
			if (!starred)
				out.push("*");
			starred = true;
			a = end;
			continue;
		}
		starred = false;
		prevRow = full ? row : null;
		const pad = (a - lo) % per;
		let hx = "", asc = "";
		for (let i = 0; i < per; i++) {
			const sep = i === 8 && per === 16 ? "  " : i ? " " : "";
			const b = a - pad + i;
			if (b < a || b >= end) {
				hx += sep + "  ";
				asc += " ";
				continue;
			}
			const v = bytes[b - lo];
			hx += sep + mark(b - lo, v.toString(16).padStart(2, "0"));
			asc += mark(b - lo, esc(asciiOf(v)));
		}
		let cls = "hex";
		if (hasCur) {
			cls += " cur";
			curLine = out.length;
		}
		out.push('<span class="' + cls + '" data-addr="' + a + '" data-row="' + (a - pad) + '" data-per="' + per + '" data-end="' + end + '"' +
			(dirty ? ' data-tip="' + escHtml(scanTip) + '"' : "") + ">" +
			(a - pad).toString(16).padStart(8, "0") +
			" " + hx + " |" + asc + "|" + bmCodeStar(a) + "</span>");
		nRows++;
		a = end;
	}
	return { html: out.join("\n"), nIns, nBad, nNx, cov, nRows, nonzero, changed, curLine, source, bits, misaligned: misaligned.size, jS, paths,
		opsNamed: opStat.named || 0, opsWhy: !ops ? "" : ops.loading ? "operand names: " + ops.loading : !ops.T ? "operands named from ELF symbols only (no DWARF variables)" : "" };
}

function renderSymbolIndex(p) {
	const el = $("symbol_index");
	el.textContent = "";
	const syms = symbolsInPage(p);
	el.dataset.count = syms.length;
	if (!symbols) {
		el.textContent = "No symbols.json for this kernel.";
		return;
	}
	if (!syms.length) {
		el.textContent = "No symbols in this page.";
		return;
	}
	for (const s of syms) {
		const b = document.createElement("button");
		b.type = "button";
		b.textContent = s.name;
		b.title = symRange(s) + (s.size ? ", " + s.size + " bytes" : "") + ", " + s.section +
			" (" + s.type + "), virtual " + hex(s.vaddr) + (s.paddr < p * PAGE ? ", starts in an earlier page" : "");
		b.addEventListener("click", () => {
			const target = $("listing").querySelector('[data-addr="' + Math.max(s.paddr, p * PAGE) + '"]');
			if (target)
				navJump(() => scrollListingTo(target));
		});
		el.append(b);
	}
}

function scrollListingTo(line) {
	const box = $("listing");
	// An instruction's sources line above it comes into view with it.
	const prev = line.previousElementSibling;
	if (prev && prev.matches(".jsrc"))
		line = prev;
	// Measured, not offsetTop: the listing is the lines' offsetParent (it
	// holds the jump arcs, position: relative). A third of the way down
	// the listing, or of the part of it on screen above the Source panel
	// where the panel is a sheet over the screen (a phone).
	const b = box.getBoundingClientRect(), band = sheetBand(b);
	const top = band ? band[0] : b.top, h = band ? band[1] - band[0] : box.clientHeight;
	box.scrollTop += line.getBoundingClientRect().top - top - h / 3;
}

// [top, bottom] of rect r on screen between the sticky top bar and the
// Source panel when the panel is open as a sheet (narrow screens), else null.
function sheetBand(r) {
	const sp = $("src_panel");
	if (sp.hidden)
		return null;
	const pr = sp.getBoundingClientRect();
	if (pr.left > 0 || pr.top <= 0)
		return null;
	const top = Math.max(r.top, document.querySelector(".top").getBoundingClientRect().bottom), bottom = Math.min(r.bottom, pr.top);
	return bottom - top > 40 ? [top, bottom] : null;
}

// The Page tab's line on the jump paths of the listing.
function renderJumpInfo(r, dropped) {
	const el = $("jump_info");
	if (!r.nIns) {
		el.textContent = "";
		return;
	}
	let arcs = 0, none = 0, into = 0, outs = 0, seen = 0;
	for (const P of r.paths.values()) {
		if (P.noDirect)
			none++;
		if (P.into.length)
			into++;
		if (P.out.length)
			outs++;
		for (const e of P.out)
			seen += e.count;
	}
	arcs = Number($("listing").dataset.arcs || 0);
	const jit = jumpsJitNote();
	el.textContent = "Jump paths: " + arcs + " jumps inside the page drawn as arcs" + (dropped ? " (" + dropped + " more not drawn: the gutter is full)" : "") +
		"; " + into + " instructions with paths in (\u2190), " + outs + " with paths out (\u2192), " + seen.toLocaleString("en") +
		" transfers from here observed while stepping, the sources named above each instruction and targets out of view after it; " + none + " without a direct path in (\u2298)" +
		(r.source === "live" ? " from this page's live decode (static paths only from the " + r.jS.local.toLocaleString("en") +
			" instructions decoded here: jumps and calls from other code are not known)" : "") + (jit ? "; " + jit : "") + ".";
}

// Draw the selected page; base: bytes of the last scan (changes are marked).
function renderPage(p, bytes, base, how, curAt) {
	const t0 = realNow();
	const box = $("listing");
	const cur = curAt === undefined ? currentPhys() : curAt;
	const r = listPage(p, bytes, base, cur >= p * PAGE && cur < p * PAGE + PAGE ? cur : -1, lineChars(box), how);
	box.innerHTML = r.html;
	box.dataset.jlocal = r.source === "live" ? "page's live decode" : "";
	const dropped = jumpsDrawn(box, r.jS, r.paths);
	renderJumpInfo(r, dropped);
	Object.assign(box.dataset, { page: p, insns: r.nIns, hexrows: r.nRows, nonzero: r.nonzero,
		changed: r.changed, reads: Number(box.dataset.reads || 0) + 1, draws: Number(box.dataset.draws || 0) + 1 });
	const hit = jumpHitScroll ? box.querySelector(".jhit") : null;
	const back = navScrollWant && navScrollWant.page === p && realNow() < navScrollWant.until ? navScrollWant : null;
	const backLine = back && back.addr !== undefined ? listingLineAt(back.addr) : null;
	if (backLine)
		scrollListingTo(backLine);
	else if (back && back.addr === undefined)
		box.scrollTop = back.top;
	else if (hit) {
		jumpHitScroll = false;
		scrollListingTo(hit);
	} else if (r.curLine >= 0) {
		const line = box.querySelector(".cur");
		if (line)
			scrollListingTo(line);
	}
	box.dataset.source = r.source;
	box.dataset.opsNamed = r.opsNamed;
	box.dataset.coverage = !r.nIns ? "" : r.cov.tint ? (r.cov.partial ? "partial" : "stepped") : "none";
	renderCovInfo(r);
	$("dump_info").textContent = (r.source === "build" ? r.nIns + " instructions (disasm.json)" :
		r.source === "live" ? r.nIns + " instructions decoded live from guest memory (iced-x86, " + r.bits + "-bit" +
			(r.misaligned ? "; the first lines outside a symbol may be misaligned" : "") + ")" :
		icedError ? "hex only: no disassembler (" + icedError + ")" : !iced ? "loading the disassembler..." : "no code in this page") +
		(r.nBad ? " (" + r.nBad + " differ from the build)" : "") +
		(r.nIns ? ", " + r.opsNamed + " memory operands named" + (r.opsWhy ? " (" + r.opsWhy + ")" : "") : "") +
		", " + r.nonzero + " non-zero bytes" + (base ? ", " + r.changed + " changed " + how : "") +
		", read " + new Date().toLocaleTimeString("en-GB");
	box._drawn = { page: p, stepper, bytes: bytes.slice(), base: base ? base.slice() : null, r, lines: null,
		at: realNow(), cost: realNow() - t0 };
}

// The listing's coverage key: how many of its instructions are tinted.
function renderCovInfo(r) {
	$("listing").dataset.unexecuted = r.cov.tint ? r.nNx : "";
	const key = $("cov_info");
	key.textContent = "";
	if (r.nIns && r.cov.tint) {
		const sw = document.createElement("span");
		sw.className = "nxkey tipmark";
		sw.textContent = "tinted";
		sw.tabIndex = 0;
		sw.setAttribute("role", "button");
		sw.dataset.tip = "Tinted lines: instructions that have not executed in this boot. The page records the address of every " +
			"instruction it steps; lines without the tint have executed. Code v86's JIT runs (the fast-forward to the kernel entry, " +
			"full speed) is not recorded" + (r.cov.partial ? ", and the JIT has run since the kernel entry, so tinted code may have run." : ".");
		key.append(sw, ": " + r.nNx + " of " + r.nIns + " instructions have not executed in this boot (recorded while stepping)" +
			(r.cov.partial ? "; v86's JIT has run since the kernel entry and records nothing, so tinted code may have run" : ""));
	} else if (r.nIns)
		key.textContent = "No execution coverage: " + r.cov.why + ".";
}

// ---- yellow lines: guest memory that differs from the build ---------------

// How far above its link address the bzImage loads vmlinux's first bytes:
// the minified Linux's bzImage carries a relocation stub of this many bytes
// at 0x100000 with vmlinux after it, and the stub moves vmlinux down right
// after the kernel entry. 0 when the payload is vmlinux itself (hello) or
// not found. From the image file and disasm.json, once per pair.
let stubShiftOf = { image: null, disasm: null, shift: 0 };
function stubShift() {
	if (!image || !disasm || !disasm.insns.length)
		return 0;
	const c = stubShiftOf;
	if (c.image === image && c.disasm === disasm)
		return c.shift;
	const ins = disasm.insns, lo = ins[0][0], want = [];
	for (const [pa, raw] of ins) {
		if (pa !== lo + want.length || want.length >= 48)
			break;
		for (let k = 0; k < raw.length; k += 2)
			want.push(parseInt(raw.substr(k, 2), 16));
	}
	const pay = image.bytes.subarray(image.hdr.setup_size, image.hdr.setup_size + 0x10000 + want.length);
	let shift = 0;
	for (let i = 0; i + want.length <= pay.length && want.length; i++)
		if (want.every((b, k) => pay[i + k] === b)) {
			shift = i - (lo - V86_KERNEL_ADDR);
			break;
		}
	stubShiftOf = { image, disasm, shift };
	return shift;
}

// The popover text of a yellow line: the instruction at pa (build bytes
// raw, hex) holds got in guest memory. Says which case it is: a Run to
// breakpoint, vmlinux not moved into place yet (the relocation stub), zero
// memory, or a change at run time.
function diffTip(pa, raw, got) {
	const n = raw.length / 2;
	let k = 0;
	for (let i = 0; i < n; i++)
		if (got[i] !== parseInt(raw.substr(i * 2, 2), 16))
			k++;
	const head = "Yellow: these bytes in guest memory differ from the build's disassembly (" + k + " of " + n + " bytes).\n";
	const sameAt = (a) => {
		if (a < 0 || a + n > emuRam)
			return false;
		const m = emulator.read_memory(a, n);
		return m.every((b, i) => b === parseInt(raw.substr(i * 2, 2), 16));
	};
	if (typeof ffBreaks !== "undefined" && ffBreaks && got.some((b, i) => b === 0xf4 && ffBreaks.has(pa + i)))
		return head + "Run to has put HLT bytes (f4) over this instruction as breakpoints while the JIT runs to it; " +
			"they are put back when it stops.";
	const d = stubShift();
	if (d > 0 && sameAt(pa + d))
		return head + "vmlinux is not in place yet. The bzImage put a " + d + "-byte relocation stub at " + hex(V86_KERNEL_ADDR) +
			" and vmlinux right after it, so the build's bytes for this address are still " + hex(d) + " bytes higher, at " + hex(pa + d) + ". " +
			(pa < V86_KERNEL_ADDR + d ? "The bytes here are the relocation stub's own code, which runs first. " : "") +
			"The stub moves vmlinux down over itself right after the kernel entry; from then on these lines are plain.";
	if (got.every((b) => b === 0))
		return head + "The memory here is zero: nothing has been loaded here yet, or it was cleared.";
	const sec = symbols && symbols.sections.find((x) => pa >= x.paddr && pa < x.paddr + x.size);
	return head + "The code here changed at run time: the kernel patched it (alternatives, jump labels, static calls) or reused the memory" +
		(sec && /^\.init/.test(sec.name) ? " (" + sec.name + " is freed once the kernel has booted)" : "") + ", or something wrote here.";
}

// The marked bytes of a line that differ from the build: a mark with the
// line's popover (tap, Enter, hover).
const diffBytesHtml = (bytes, tip) => '<span class="difb tipmark" tabindex="0" role="button" aria-label="Why yellow" data-tip="' +
	escHtml(tip) + '">' + bytes + "</span>";

// The title of a listing line with EIP inside it (not at its start).
const midTitle = (cur) => "EIP " + hex(cur) + " is inside this instruction of the build's disassembly: the bytes here differ from the build " +
	"(the bzImage's relocation stub at 0x100000 right after the entry, or code changed at run time), so the CPU runs other code. " +
	"Code at EIP in the Debugger tab decodes it live.";

// Slow motion inside one page: move the EIP mark and untint what has
// executed instead of drawing the page's ~1,400 lines again (150 ms on a
// desktop, 0.6-1 s on a phone-class CPU). The whole listing, with the
// observed counts, is drawn again every LISTING_REFRESH_MS (or six times
// as long as its drawing took), and whenever the page or its bytes change.
const LISTING_REFRESH_MS = 4000;
function listingFollow() {
	const box = $("listing"), d = box._drawn, p = selectedPage, cur = currentPhys();
	if (!d || d.page !== p || d.stepper !== stepper || !emuReady || jumpHitScroll || cur < p * PAGE || cur >= p * PAGE + PAGE ||
		realNow() - d.at >= Math.max(LISTING_REFRESH_MS, 6 * d.cost))
		return false;
	const same = (x, y) => x === y || (!!x && !!y && x.length === y.length && x.every((v, i) => v === y[i]));
	const base = scanNow ? scanNow.subarray(p * PAGE, p * PAGE + PAGE) : null;
	if (!same(d.bytes, readPage(p)) || !same(d.base, base))
		return false;
	if (!d.lines) {
		d.lines = new Map();
		for (const el of box.querySelectorAll(".ins[data-addr]"))
			d.lines.set(Number(el.dataset.addr), el);
	}
	let line = d.lines.get(cur), mid = false;
	if (!line && d.r.source === "build") {
		const prev = disasm.insns[insnIndex(cur) - 1];
		mid = !!prev && cur < prev[0] + prev[1].length / 2;
		line = mid ? d.lines.get(prev[0]) : null;
	}
	if (!line || !line.isConnected)
		return false;
	for (const el of box.querySelectorAll(".cur")) {
		el.classList.remove("cur", "mid", "tipmark");
		if (el.dataset.eip !== undefined) {
			delete el.dataset.eip;
			const b = el.querySelector(".difb"); // a yellow line keeps its own text
			if (b)
				el.dataset.tip = b.dataset.tip;
			else
				delete el.dataset.tip;
		}
	}
	line.classList.add("cur");
	if (mid) {
		const b = line.querySelector(".difb");
		line.classList.add("mid", "tipmark");
		line.dataset.eip = cur;
		line.dataset.tip = (b ? b.dataset.tip + "\n\n" : "") + midTitle(cur);
	}
	if (d.r.cov.tint) {
		for (const el of box.querySelectorAll(".ins.nx"))
			if (stepper.executed(Number(el.dataset.addr))) {
				el.classList.remove("nx");
				d.r.nNx--;
			}
		renderCovInfo(d.r);
	}
	// .ins.cur is a little taller: the arcs are measured again.
	drawJumpArcs(box, box._jarcs || []);
	scrollListingTo(line);
	box.dataset.draws = Number(box.dataset.draws || 0) + 1;
	box.dataset.moves = Number(box.dataset.moves || 0) + 1;
	return true;
}

function inspect() {
	const p = selectedPage;
	if (p < 0)
		return;
	if (p >= gridRam() / PAGE) {
		selectedPage = -1;
		return;
	}
	$("page_title").textContent = "Page " + hex(p * PAGE) + "–" + hex(p * PAGE + PAGE - 1) +
		" (" + pageLabel(p) + ")";
	if ($("symbol_index").dataset.page !== String(p) || $("symbol_index").dataset.kernel !== (image && image.name)) {
		renderSymbolIndex(p);
		$("symbol_index").dataset.page = p;
		$("symbol_index").dataset.kernel = image && image.name;
	}
	const snap = pageSnapView && pageSnapView.page === p ? pageSnapView : null;
	pageSnapBanner(!!snap);
	if (snap) {
		renderPage(p, snap.bytes, snap.base, "against " + snap.against, snap.cur);
		return;
	}
	if (!emulator || !emuReady) {
		$("listing").textContent = emulator ? "Booting..." : "Boot a kernel to read this page.";
		return;
	}
	renderPage(p, readPage(p), scanNow ? scanNow.subarray(p * PAGE, p * PAGE + PAGE) : null, "since the last scan");
}

function selectPage(p, show = true) {
	navJump(() => {
		selectedPage = p;
		drawGrid();
		inspect();
		setLive();
		if (show)
			showTab("page");
		else
			writeHash();
	});
}

function setLive() {
	clearInterval(liveTimer);
	liveTimer = null;
	if ($("live").checked && emulator && selectedPage >= 0)
		liveTimer = setInterval(() => {
			if (emulator && (runState === "jit" || runState === "stepped"))
				inspect();
		}, LIVE_MS);
}

// ---- tabs -----------------------------------------------------------------

const TABS = ["machine", "memory", "page", "debugger", "calls", "source", "snapshots", "settings", "about"];
// The names of renamed tabs in old links (#graph, ?tab=coverage).
const TAB_ALIASES = { graph: "calls", coverage: "source" };
// The Machine tab first: it holds the VM's screen and run controls.
let tab = "machine";

// Hash: #memory, #page/0x100000, #debugger, ...; with the Source panel open
// its file and line after ~src= (#debugger~src=init/main.c:1000), so a
// reload opens that file again.
const navHashNow = () => "#" + tab + (tab === "page" && selectedPage >= 0 ? "/" + hex(selectedPage * PAGE) : "") + srcHash();

function srcHash() {
	if (!srcPanel || $("src_panel").hidden)
		return "";
	const l = srcPanel.pick || srcPanel.line;
	return "~src=" + encodeURI(srcPanel.path) + (l ? ":" + l : "");
}

function writeHash() {
	const h = navHashNow();
	if (location.hash !== h)
		history.replaceState(history.state, "", h); // keeps the entry's place in the jump history
}

function showTab(name) {
	name = TAB_ALIASES[name] || name;
	tab = TABS.includes(name) ? name : "machine";
	for (const t of TABS) {
		$("panel_" + t).hidden = t !== tab;
		$("tab_" + t).setAttribute("aria-selected", t === tab);
	}
	vmKeys();
	writeHash();
	if (tab === "page")
		inspect();
	if (tab === "memory")
		drawGrid();
	if (tab === "debugger")
		renderContext();
	if (tab === "settings")
		renderSettings();
	graphShow(tab === "calls");
	covShow(tab === "source");
}

// The VM's screen is in the Machine tab and is hidden (it keeps changing)
// on the others: keys go to the VM only while it is shown, and Alt+Left/Right
// go to the jump history until the screen is clicked again.
function vmKeys() {
	if (tab !== "machine")
		vmHasKeys = false;
	if (emulator)
		emulator.keyboard_set_status(tab === "machine");
}

// Compact tabs (Settings tab): the tab buttons show only their emoji, the
// name is their tooltip and stays their accessible name.
const COMPACT_KEY = "mkb-compact-tabs";
// Default on a phone: at 400 px and less the names take three rows.
const compactDefault = () => window.matchMedia("(max-width: 400px)").matches;

function setCompactTabs(on) {
	document.body.classList.toggle("compact-tabs", on);
	$("compact_tabs").checked = on;
	for (const t of TABS) {
		const b = $("tab_" + t);
		if (on)
			b.dataset.tip = b.getAttribute("aria-label");
		else
			delete b.dataset.tip;
	}
}

function initCompactTabs() {
	let saved = null;
	try {
		saved = localStorage.getItem(COMPACT_KEY);
	} catch (e) {
		// no storage: the default
	}
	setCompactTabs(saved === null ? compactDefault() : saved === "1");
	$("compact_tabs").addEventListener("change", () => {
		setCompactTabs($("compact_tabs").checked);
		try {
			localStorage.setItem(COMPACT_KEY, $("compact_tabs").checked ? "1" : "0");
		} catch (e) {
			// no storage: until reload
		}
	});
}

// A hash change (typed, or a link) opens that tab and page. Going back
// through the jump history changes the hash too: navRestore has drawn that
// view already, with its scroll positions, and the hash says what is shown.
function readHash() {
	bmFromHash(); // a link's #bm=: the bookmark is opened when its kernel runs
	if (navBuilt && location.hash === navHashNow())
		return;
	let h = location.hash.slice(1), src = null;
	const at = h.indexOf("~src=");
	if (at >= 0) {
		try {
			src = /^(.+?)(?::(\d+))?$/.exec(decodeURI(h.slice(at + 5)));
		} catch (e) {
			src = null; // a malformed hash: no file
		}
		h = h.slice(0, at);
	}
	let [name, addr] = h.split("/");
	if (!name && !navBuilt)
		name = new URLSearchParams(location.search).get("tab"); // ?tab=calls when the URL has no hash
	if (addr) {
		const a = parseInt(addr, 16);
		if (Number.isFinite(a))
			selectedPage = Math.floor(a / PAGE);
	}
	showTab(name || "machine");
	// The file of the hash, without a history entry (a reload, a link).
	if (src && (!srcPanel || $("src_panel").hidden || srcPanel.path !== src[1] || (srcPanel.pick || srcPanel.line || 0) !== Number(src[2] || 0)))
		srcShowFile(src[1], Number(src[2] || 0));
}

// ---- jump history ---------------------------------------------------------

// Every jump the user makes (a link to code or to a file:line, a page of
// the grid, a list's "go there") is an entry of the browser's history:
// the Back and Forward buttons, Alt+Left/Right, the mouse's back button
// and a phone's back gesture all go through popstate and stay in this
// document, so the VM keeps running. navList holds the views (navCapture);
// the entry left is captured again when the user leaves it, so Forward
// returns to it as it was left. Following EIP while stepping only replaces
// the hash (writeHash) and adds nothing.
const NAV_ID = Math.random().toString(36).slice(2); // this load's entries, not an earlier load's
// A listing drawn again within this time after Back (the tab's listing
// gets its width and is drawn again on the next frame) keeps the
// restored scroll instead of scrolling to EIP.
const NAV_SCROLL_MS = 1000;
let navList = [];
let navAt = 0;
let navDepth = 0;          // inside navJump: nested jumps (jumpGo -> jumpTo) are one entry
let navBuilt = false;
let navScrollWant = null;  // { page, top or addr, until }: the listing's scroll for renderPage after Back or jumpTo
let vmHasKeys = true;      // the VM screen was clicked last: Alt+Left/Right are its keys

// The instruction near the top third of the Page listing (where a jump
// puts its target), else -1.
function navListingAnchor() {
	const box = $("listing");
	const y = box.getBoundingClientRect().top + box.clientHeight / 3;
	let at = -1;
	for (const el of box.querySelectorAll(".ins[data-addr]")) {
		if (el.getBoundingClientRect().top > y)
			break;
		at = Number(el.dataset.addr);
	}
	return at;
}

function navLabel(v) {
	let where = "the " + $("tab_" + v.tab).getAttribute("aria-label") + " tab";
	if (v.tab === "page") {
		const at = v.hit >= 0 && Math.floor(v.hit / PAGE) === v.page ? v.hit : navListingAnchor();
		const name = at >= 0 && symbols ? asmTargetName(at, kernelSymLookup()) : "";
		where = name || (at >= 0 ? hex(at) : v.page >= 0 ? "page " + hex(v.page * PAGE) : where);
	}
	if (v.snap)
		where += ", snapshot " + v.snap.name;
	if (v.tab === "calls" && v.graph && graphSelName(v.graph))
		where += ", " + graphSelName(v.graph);
	if (v.src)
		where += ", " + v.src.path.replace(/^.*\//, "") + (v.src.pick || v.src.line ? ":" + (v.src.pick || v.src.line) : "");
	return where;
}

function navCapture() {
	const open = !!srcPanel && !$("src_panel").hidden;
	const v = { tab, page: selectedPage, top: $("listing").scrollTop, hit: jumpHit, y: window.scrollY,
		src: open ? { pa: srcPanel.pa, fn: srcPanel.fn, path: srcPanel.path, line: srcPanel.line, pick: srcPanel.pick, whole: srcPanel.whole } : null,
		srcTop: open ? $("src_body").scrollTop : 0, graph: tab === "calls" ? graphCapture() : null,
		snap: tab === "page" && pageSnapView && pageSnapView.page === selectedPage ? pageSnapView : null };
	v.label = navLabel(v);
	return v;
}

function navSame(a, b) {
	const src = (s) => (s ? [s.path, s.line, s.pick, s.whole, s.pa].join("\0") : "");
	return a.tab === b.tab && a.page === b.page && a.hit === b.hit && src(a.src) === src(b.src) && a.snap === b.snap &&
		Math.abs(a.top - b.top) < 2 && Math.abs(a.srcTop - b.srcTop) < 2;
}

// Run a jump the user asked for; a new entry when the view changed.
function navJump(jump) {
	if (navDepth || !navBuilt)
		return jump();
	const before = navCapture(), url = location.href;
	navDepth++;
	try {
		return jump();
	} finally {
		navDepth--;
		const after = navCapture();
		if (!navSame(before, after)) {
			// The first jump also turns the entry the page was loaded in
			// into a guard: going back past the first view lands there
			// and shows the first view again instead of leaving the page.
			if (!navList.length) {
				history.replaceState({ nav: -1, id: NAV_ID }, "", url);
				history.pushState({ nav: 0, id: NAV_ID }, "", url);
			} else
				history.replaceState({ nav: navAt, id: NAV_ID }, "", url); // the hash it had: the jump replaced it
			navList.length = navAt + 1;
			navList[navAt] = before;
			navList.push(after);
			navAt++;
			history.pushState({ nav: navAt, id: NAV_ID }, "", navHashNow());
			navRender();
		}
	}
}

function navRestore(v) {
	jumpPopClose(false);
	pageSnapView = v.snap || null;
	jumpHit = v.hit;
	jumpHitScroll = false;
	if (v.page !== selectedPage) {
		selectedPage = v.page;
		drawGrid();
		setLive();
	}
	navScrollWant = v.tab === "page" ? { page: v.page, top: v.top, until: realNow() + NAV_SCROLL_MS } : null;
	openFnWant = null;
	if (v.graph)
		graphRestore(v.graph);
	showTab(v.tab);
	if (v.tab === "page")
		$("listing").scrollTop = v.top;
	srcSwClose(false);
	if (v.src) {
		// Its scroll once the file is drawn (now, or when the file or DWARF arrives).
		srcPanel = { ...v.src, scrollPx: v.srcTop };
		$("src_panel").hidden = false;
		renderSourcePanel();
	} else if (!$("src_panel").hidden) {
		$("src_panel").hidden = true;
		srcPanel = null;
	}
	writeHash(); // showTab wrote it before the panel was restored
	window.scrollTo(window.scrollX, v.y);
}

function navOnPop(ev) {
	const s = ev.state;
	// An entry of an earlier load of the page: its views are gone.
	if (!s || s.id !== NAV_ID || !navList.length)
		return;
	const to = Math.min(Math.max(s.nav, 0), navList.length - 1);
	navList[navAt] = navCapture();
	navAt = to;
	navRestore(navList[to]);
	navRender();
}

function navRender() {
	const box = $("nav_hist"), back = $("nav_back"), fwd = $("nav_fwd");
	box.dataset.count = navList.length;
	box.dataset.at = navAt;
	back.disabled = navAt <= 0;
	fwd.disabled = navAt >= navList.length - 1;
	const say = (b, dir, v, key) => {
		const t = v ? dir + " to " + v.label + " (" + key + ")" : dir + ": no " + (dir === "Back" ? "earlier" : "later") + " view";
		b.title = t;
		b.setAttribute("aria-label", t);
	};
	say(back, "Back", navList[navAt - 1], "Alt+Left");
	say(fwd, "Forward", navList[navAt + 1], "Alt+Right");
}

function initNav() {
	navBuilt = true;
	// navRestore sets the window's scroll with the rest of the view; the
	// browser's own restore would come after it.
	history.scrollRestoration = "manual";
	window.addEventListener("popstate", navOnPop);
	$("nav_back").addEventListener("click", () => navAt > 0 && history.back());
	$("nav_fwd").addEventListener("click", () => navAt < navList.length - 1 && history.forward());
	// v86 takes every key on the window for the VM (preventDefault), Alt+Left
	// too: these go to the history unless the VM screen was clicked last
	// (or the focus is in a text field). Enter and Space click the buttons.
	const vmFocus = (ev) => {
		vmHasKeys = !!ev.target.closest && !!ev.target.closest("#screen_container");
	};
	document.addEventListener("pointerdown", vmFocus, true);
	document.addEventListener("focusin", vmFocus, true);
	// Tab moves the focus through the page unless the VM screen has the keys.
	for (const type of ["keydown", "keyup"])
		document.addEventListener(type, (ev) => {
			if (ev.key === "Tab" && !vmHasKeys && !ev.altKey && !ev.ctrlKey && !ev.metaKey)
				ev.stopPropagation();
		});
	document.addEventListener("keydown", (ev) => {
		const nav = ev.target.closest ? ev.target.closest("#nav_hist button") : null;
		if (nav && (ev.key === "Enter" || ev.key === " ")) {
			ev.preventDefault();
			ev.stopPropagation();
			nav.click();
			return;
		}
		if (!ev.altKey || ev.ctrlKey || ev.metaKey || ev.shiftKey || (ev.key !== "ArrowLeft" && ev.key !== "ArrowRight"))
			return;
		const a = document.activeElement;
		if (vmHasKeys || (a && a.matches("input, textarea, select")))
			return;
		ev.preventDefault();
		ev.stopPropagation();
		$(ev.key === "ArrowLeft" ? "nav_back" : "nav_fwd").click();
	}, true);
	navRender();
}

// ---- page scan ------------------------------------------------------------

// render: also redraw the page listing.
function scan(render = true) {
	if (!emulator || !emuReady)
		return;
	const r = emuRam;
	const mem = emulator.read_memory(0, r).slice();
	scanPrev = scanNow && scanNow.length === r ? scanNow : null;
	scanNow = mem;
	const words = new Uint32Array(mem.buffer);
	const prev = scanPrev ? new Uint32Array(scanPrev.buffer) : null;
	const pages = r / PAGE;
	const per = PAGE / 4;
	usedPages = new Uint8Array(pages);
	changedPages = prev ? new Uint8Array(pages) : null;
	let count = 0, low = 0, changed = 0;
	for (let p = 0; p < pages; p++) {
		const base = p * per;
		for (let i = 0; i < per; i++) {
			if (words[base + i]) {
				usedPages[p] = 1;
				count++;
				if (p < 256)
					low++;
				break;
			}
		}
		if (prev) {
			for (let i = 0; i < per; i++) {
				if (words[base + i] !== prev[base + i]) {
					changedPages[p] = 1;
					changed++;
					break;
				}
			}
		}
	}
	drawGrid();
	const fp = $("footprint");
	fp.dataset.nonzero = count;
	fp.dataset.total = pages;
	fp.dataset.changed = prev ? changed : "";
	fp.textContent = count + " of " + pages + " pages non-zero = " + kib(count * PAGE) +
		" (" + low + " below 1 MiB, " + (count - low) + " above)" +
		(prev ? "; " + changed + " changed since the previous scan" : "");
	if (render && selectedPage >= 0 && tab === "page") {
		const p = selectedPage;
		renderPage(p, scanNow.subarray(p * PAGE, p * PAGE + PAGE),
			scanPrev ? scanPrev.subarray(p * PAGE, p * PAGE + PAGE) : null,
			"since the previous scan");
	}
}

function setAutoscan() {
	clearInterval(scanTimer);
	scanTimer = null;
	if ($("autoscan").checked && emulator)
		scanTimer = setInterval(() => scan(), 1000);
}

// ---- snapshots ------------------------------------------------------------

// saveSnapshot, restoreSnapshot and the rest: snapshots.js.

// ---- boot -----------------------------------------------------------------

// v86.wasm with two internal functions exported for stepping (stepper.js).
// If the patch fails the page still boots, without the debugger.
let wasmPatchError = null;
async function loadWasm(env) {
	const resp = await fetch(withV("v86/v86.wasm"));
	const bytes = await resp.arrayBuffer();
	let code = bytes;
	try {
		code = patchV86Wasm(bytes).bytes;
		wasmPatchError = null;
	} catch (e) {
		wasmPatchError = String(e);
	}
	const { instance } = await WebAssembly.instantiate(code, env);
	return instance.exports;
}

// Boot can be pressed again before the last one is done: each call waits
// for every older machine to be destroyed, and only the newest call goes
// on (two at once left two machines and a fast-forward of the old one
// running on the new one's state: "fast-forwarding", nothing running).
let bootGen = 0;
let destroying = Promise.resolve();
async function boot() {
	if (!image)
		return;
	const gen = ++bootGen;
	stopDriver();
	// A fast-forward of the old machine stops at its next check (st !== stepper).
	stepper = null;
	emuReady = false;
	runState = "off";
	ffActive = false;
	if (emulator) {
		const old = emulator;
		emulator = null;
		destroying = destroying.then(() => old.destroy()).catch((e) => console.warn("destroy:", e));
	}
	updateDebugUi();
	await destroying;
	if (gen !== bootGen)
		return;
	$("scan").disabled = true;
	$("snap_save").disabled = true;
	$("footprint").textContent = "";
	scanNow = scanPrev = usedPages = changedPages = null;
	// A snapshot's page or diff belongs to the machine it was read from.
	pageSnapView = null;
	snapDiff = null;
	renderSnapDiff();
	emuRam = ram();
	$("screen_text").textContent = "";
	resetCounters();
	updateDebugUi();
	const fatal = checkImage(image.hdr, ram()).some(([level]) => level === "error");
	setStatus("Booting " + image.name + " with " + kib(ram()) + "...");
	try {
		// A new machine starts at virtual time 0 (or real time).
		CLOCK.reset(clockMips() || 10);
		GUEST.reset(new URLSearchParams(location.search));
		if (!clockMips())
			CLOCK.set(false, null);
		emulator = new V86({
			memory_size: emuRam,
			vga_memory_size: 256 * 1024,
			screen_container: $("screen_container"),
			wasm_fn: loadWasm,
			bios: { url: withV("v86/seabios.bin") },
			vga_bios: { url: withV("v86/vgabios.bin") },
			bzimage: { buffer: image.bytes.buffer.slice(0) },
			cmdline: $("cmdline").value,
			initrd: initrd ? { buffer: initrd.bytes.buffer.slice(initrd.bytes.byteOffset, initrd.bytes.byteOffset + initrd.bytes.length) } : undefined,
			autostart: false,
			disable_mouse: true,
			// The kernels make no sound on purpose: the minified Linux only touches
			// port 0x61 to gate PIT channel 2 for TSC calibration (speaker data
			// bit cleared), but v86's speaker emulation still clicks while that
			// channel runs. ?sound=1 turns the emulated PC speaker back on.
			disable_speaker: new URLSearchParams(location.search).get("sound") !== "1",
		});
	} catch (e) {
		setStatus("v86 refused to start: " + e);
		return;
	}
	const em = emulator;
	em.add_listener("emulator-loaded", () => {
		if (em !== emulator)
			return;
		GUEST.install(em.v86.cpu);
		emuReady = true;
		vmKeys();
		setStatus("Running " + image.name + " with " + kib(ram()) +
			(fatal ? ". The header check predicts this will not work." : "."));
		$("scan").disabled = false;
		$("snap_save").disabled = false;
		renderSnapshots();
		try {
			stepper = new Stepper(em, V86_KERNEL_ADDR, kernelEnd(image.hdr));
			stepper.milestones = MILESTONES.map((text) => ({ text, found: null }));
			stepper.setSymbols(codeSymbols());
			stepper.onTaskLeave = varsTaskNow;
			// ?unwind=0: the old reconstruction from symbol transitions only.
			stepper.espUnwind = new URLSearchParams(location.search).get("unwind") !== "0";
			// ?panicbudget=N: instructions after panic() entry before giving up.
			stepper.panicBudget = Number(new URLSearchParams(location.search).get("panicbudget")) || 50e6;
			// Before vmlinux is in place (the bzImage's relocation stub also runs
			// at 1 MiB) the address of EIP belongs to a vmlinux symbol only by
			// coincidence: name a symbol only when the bytes at EIP are the
			// build's instruction bytes.
			const symbolOf = stepper.symbolOf.bind(stepper);
			const mem = em.v86.cpu.mem8;
			stepper.symbolOf = (phys) => {
				const i = symbolOf(phys);
				if (i < 0 || !disasm)
					return i;
				const k = insnIndex(phys), ins = disasm.insns;
				if (k >= ins.length || ins[k][0] !== phys)
					return -1;
				const raw = ins[k][1];
				for (let b = 0; b < raw.length / 2; b++)
					if (mem[phys + b] !== parseInt(raw.substr(b * 2, 2), 16))
						return -1;
				return i;
			};
		} catch (e) {
			stepper = null;
			$("dbg_status").textContent = "No debugger: " + (wasmPatchError || e);
		}
		graphReset(stepper);
		if (stepper) {
			CLOCK.set(!!clockMips(), stepper);
			jitDetInstall(stepper, em);
		}
		// Default: step BIOS and setup at full stepping speed to the kernel
		// entry, then continue at prefSpeed (1k/s) and count live. With
		// "full", count the whole boot in the background, then run the JIT.
		$("dbg_speed").value = prefSpeed;
		entryPhase = !!stepper && prefSpeed !== "full";
		counting = !!stepper && (entryPhase || $("count").checked);
		initrdNote();
		$("entry_info").textContent = entryPhase ? "Stepping BIOS and setup to the kernel entry..." : "";
		delete $("entry_info").dataset.steps;
		countStart = realNow();
		countMs = 0;
		countSteps = 0;
		setAutoscan();
		setLive();
		computePageClasses();
		drawGrid();
		inspect();
		if (startPaused && stepper) {
			startPaused = false;
			runState = "paused";
			$("dbg_status").textContent = "Paused at the reset vector.";
			afterStop();
		} else if (stepper && ffEnabled() && ffTarget()) {
			// BIOS, setup and the decompressor at JIT speed, then exactly to
			// the kernel's link entry; from there as before.
			const t = ffTarget();
			runState = "paused";
			fastForward(t.phys, t.name).then((ok) => {
				if (em !== emulator)
					return;
				if (ffAbort) {
					afterStop();
					return;
				}
				if (!ok)
					$("dbg_status").textContent = "Fast-forward did not reach " + t.name + "; stepping from here." +
						(ffInfo && ffInfo.error ? " " + ffInfo.error : "");
				else
					finishEntry();
				resume();
			});
		} else {
			resume();
		}
	});
}

// ---- debugger -------------------------------------------------------------

const MILESTONES = ["Hello, World!", "Still alive"];
const COUNT_LIMIT_MS = 12000000; // give up counting after this much stepping (200 min)
const SLICE_MS = 25;            // fast stepping: JS time per slice

let stepper = null;
let runState = "off";    // "off", "jit" (v86's own loop), "stepped" (our loop), "paused"
let counting = false;    // stepping from reset until every milestone is on screen
let countStart = 0;      // realNow() when the count began
let countMs = 0;         // stepping time spent counting
let countSteps = 0;      // instructions stepped in that time
let countNote = "";
let startPaused = false; // ?paused=1: stop at the reset vector
let runTo = null;        // Set of physical addresses "Run to" stops at, or null
let runToLabel = "";     // what they are ("0x1234", "init/main.c:1000")
let runToFrom = -1;      // where the Run to started (it steps past a target there once)
let runToMoved = false;  // an instruction has run since
let entryPhase = false;  // stepping at full stepping speed to the kernel entry, then slow
let prefSpeed = "1000";  // speed after the entry: ?speed=, else the remembered choice, else 1k/s
const SPEED_KEY = "mkb-speed";
const COUNT_KEY = "mkb-count";
const REF_KEY = "mkb-ref-";
let refSaved = null;     // the "Still alive" milestone whose count is remembered
// Instructions from the entry to "Still alive" when no run on this browser
// measured it yet (for the ETA only).
// Instructions from the entry to "Still alive" (the same at every clock rate).
const REF_STEPS = { hello: { any: 432 }, "hello-os": { any: 154 },
	"minified-linux": { any: 723158 }, "minified-linux-os": { any: 132006 } };
let driverTimer = null;
let driverChannel = null;
let lastUi = 0;
let uiSteps = -1;        // stepper.steps when the driver last drew the Debugger's state
let lastFrame = 0;
let stepCredit = 0;
let prevRegs = null;
let flashes = 0;

const speed = () => $("dbg_speed").value;  // "full" or instructions per second
// Fast stepping (no animation): full speed while counting, Run to, or the
// way from reset to the kernel entry.
const fastStepping = () => speed() === "full" || runTo !== null || entryPhase;

function resetCounters() {
	counting = false;
	countMs = 0;
	countSteps = 0;
	countNote = "";
	runTo = null;
	prevRegs = null;
	flashes = 0;
	$("pages").dataset.flashes = 0;
}

// Counting ends at the kernel's end: panic, or hlt with interrupts off.
function countDone() {
	return !stepper || !!stepper.end;
}

function wantStepped() {
	return !!stepper && (speed() !== "full" || counting || runTo !== null || entryPhase);
}

function stopDriver() {
	clearTimeout(driverTimer);
	driverTimer = null;
	if (driverChannel)
		driverChannel.port1.onmessage = null;
	driverChannel = null;
	if (lastFrame)
		cancelAnimationFrame(lastFrame);
	lastFrame = 0;
}

// Run the VM in whichever way the controls ask for. now: run the first
// instruction at once (Continue, a speed picked: at 1/s it came a second
// later, which looked like a stall); not when the page itself goes on (the
// kernel entry, where the counts from the entry start).
async function resume(now = false) {
	if (!emulator || !emuReady)
		return;
	jitRunId++; // a running jitLoop stops at its next check
	stopDriver();
	if (wantStepped()) {
		if (emulator.is_running())
			await emulator.stop();
		runState = "stepped";
		stepCredit = now ? 1 : 0;
		prevTick = 0;
		CLOCK.set(!!clockMips());
		scheduleDriver();
	} else {
		if (stepper && runState !== "off")
			stepper.markGap();
		if (stepper)
			stepper.noteJit(false);
		if (stepper && clockMips()) {
			// The virtual clock: the page runs the JIT (jitLoop), the same
			// run every time.
			const em = emulator;
			if (em.is_running())
				await em.stop();
			if (em !== emulator)
				return;
			CLOCK.set(true);
			CLOCK.mips = clockMips(); // from here on (the menu changed while the JIT ran on real time)
			runState = "jit";
			jitLoop();
		} else {
			// Real time: v86's own loop, with v86's own compiling.
			const em = emulator;
			if (stepper && jitPending(stepper))
				await jitFlush(stepper);
			if (em !== emulator)
				return;
			CLOCK.set(false);
			runState = "jit";
			emulator.run();
		}
	}
	speedReset();
	updateDebugUi();
}

// Full speed on the virtual clock (J1): v86's JIT driven from the page, one
// main_loop() (about 100,000 instructions) at a time, with the clock from
// the instruction count (jitChunk) and compiled code going live only between
// two of them (jitFlush), so a run repeats exactly: same slice ends, same
// timer interrupts, same state at the same count (D1). It yields to the
// page after JIT.yieldMs or JIT.yieldEvery slices, and draws the screen and
// the Debugger meanwhile. A guest that has been idle for a while (halted
// for three quarters of the virtual time over 64 such groups in a row)
// gets a 10 ms sleep after each group: the virtual clock would otherwise
// race through its timer ticks at full CPU (the initramfs shell). Pacing
// and yields take real time only, never guest time.
let jitRunId = 0;  // bumped by resume and pause: a running loop stops
let jitLoopId = 0; // the loop that runs (draws the screen), 0 for none
async function jitLoop() {
	const st = stepper, em = emulator, id = ++jitRunId;
	jitLoopId = id;
	const live = () => st === stepper && em === emulator && runState === "jit" && id === jitRunId;
	const ch = new MessageChannel();
	const yieldNow = () => new Promise((r) => {
		ch.port1.onmessage = () => r();
		ch.port2.postMessage(0);
	});
	const sa = em.screen_adapter; // v86 draws the screen only while it runs
	if (sa)
		sa.continue();
	let idleGroups = 0, drawn = realNow(), screenAt = drawn;
	const end0 = st.end;
	try {
		while (live()) {
			const now = realNow();
			if (now - drawn > 500) { // the count, registers, clock while it runs
				drawn = now;
				updateDebugUi();
			}
			// v86 draws the screen's changed rows in requestAnimationFrame,
			// which this loop's back-to-back yields can starve (the shell's
			// echo stayed off the screen until a pause): draw them here too.
			if (now - screenAt > 33) {
				screenAt = now;
				flushScreen();
			}
			const t0 = realNow(), clock0 = CLOCK.now(), idle0 = st.idleMs;
			for (let n = 0; n < JIT.yieldEvery; n++) {
				if (jitPending(st)) {
					await jitFlush(st);
					if (!live())
						return;
				}
				jitChunk(st);
				if (st.dead()) {
					watchJitEnd();
					return;
				}
				const stop = await jitExactStop(st, live, end0);
				if (stop === "gone")
					return;
				if (stop) {
					counting = false;
					flushScreen(); // v86 draws the screen only while it runs
					pause(stop === "prompt" ? "at the (initramfs) prompt" : endText());
					return;
				}
				if (realNow() - t0 > JIT.yieldMs)
					break;
			}
			const span = CLOCK.now() - clock0;
			idleGroups = span > 0 && st.idleMs - idle0 > 0.75 * span ? idleGroups + 1 : 0;
			if (idleGroups > 64)
				await new Promise((r) => setTimeout(r, 10));
			else
				await yieldNow();
		}
	} finally {
		ch.port1.onmessage = null;
		if (jitLoopId === id) {
			jitLoopId = 0;
			if (sa && !em.is_running())
				sa.pause();
		}
	}
}

// Debian's stops under the JIT (J3), exactly as stepping finds them: the
// panic's last line is written about 250,000 instructions or more after
// "Kernel panic - not syncing" (D0), so from the slice in which that
// appears the stepper runs to the very write (Stepper.run "end"); the
// initramfs prompt stops at the first halt after it, which is a slice's end
// when the slice ended halted, else the stepper runs to the next hlt.
// Returns null (go on), "end", "prompt", or "gone" (paused or replaced).
// end0: the end the loop started with (Continue after it runs on).
async function jitExactStop(st, live, end0) {
	if (st.end)
		return st.end !== end0 ? "end" : null;
	const prompt = (st.textSeen & TEXT_PROMPT) && !st.prompt;
	if (!prompt && !(st.textSeen & TEXT_PANIC))
		return null;
	if (prompt && st.halted()) {
		st.prompt = { exact: true, ...st.snapshot(st.nextPhys() - 1) }; // the hlt
		return "prompt";
	}
	// The stepper takes over until one of them. A panic without its last
	// line (another kernel's) ends after panicBudget instructions.
	if (st.panicTextFrom === undefined)
		st.panicTextFrom = st.steps;
	// panic() was entered under the JIT: the stepper would take its first
	// return into panic() for the entry.
	if (!prompt && !st.panicEntered)
		st.panicEntered = { jit: true };
	let n = 0;
	for (;;) {
		const r = st.run(1e6, -1, 20);
		if (r === "end" || r === "prompt")
			return r;
		if (r === "hlt" && st.dead()) {
			st.markHalt();
			return "end";
		}
		if (!prompt && st.steps - st.panicTextFrom >= st.panicBudget) {
			st.end = { kind: "panic-budget", ...st.snapshot() };
			return "end";
		}
		if (++n % 4 === 0) {
			flushScreen();
			await new Promise((r) => setTimeout(r, 0));
			if (!live())
				return "gone";
		}
	}
}

async function pause(note) {
	if (!emulator || !emuReady)
		return;
	if (ffActive)
		ffAbort = true;
	jitRunId++;
	stopDriver();
	if (emulator.is_running())
		await emulator.stop();
	runState = "paused";
	runTo = null;
	speedReset();
	if (entryPhase && stepper && stepper.sinceEntry >= 0)
		finishEntry(); // a Run to (stepped) went past the kernel entry
	// note: text, or nodes (runToStopped's file:line link).
	if (note !== undefined)
		$("dbg_status").replaceChildren(...[].concat(note));
	afterStop();
}

// High stepped speeds (1M/s and up) run in the message loop like full
// stepping, limited by the step credit, and redraw at most 10 times a second.
const highRate = () => !fastStepping() && Number(speed()) >= 1e6;

function scheduleDriver() {
	if (fastStepping() || highRate()) {
		// As fast as possible: zero-delay message loop, works in background tabs.
		if (!driverChannel) {
			driverChannel = new MessageChannel();
			driverChannel.port1.onmessage = driverTick;
		}
		driverChannel.port2.postMessage(0);
	} else if (document.hidden) {
		// No animation frames in a hidden tab: a timer (the browser runs it
		// at most once a second there), with the step credit of the time
		// that passed.
		driverTimer = setTimeout(driverTick, 100);
	} else {
		lastFrame = requestAnimationFrame((t) => { lastFrame = 0; driverTick(t); });
	}
}

// The tab hidden or shown, or the page frozen and resumed (phones freeze
// background tabs): the pending frame or timer may never come, so schedule
// anew; measure the speed from here.
function onVisibility() {
	prevTick = 0;
	speedReset();
	if (runState === "stepped") {
		stopDriver();
		scheduleDriver();
	}
}

let prevTick = 0;
function driverTick() {
	if (runState !== "stepped" || !stepper)
		return;
	const now = realNow();
	let reason;
	const t0 = now, s0 = stepper.steps;
	if (entryPhase && runTo === null) {
		const at = stepper.sinceEntry >= 0 || stepper.nextPhys() === V86_KERNEL_ADDR;
		reason = at ? "addr" : stepper.run(Infinity, V86_KERNEL_ADDR, SLICE_MS);
		if (reason === "addr") {
			finishEntry();
			scheduleDriver();
			return;
		}
	} else if (runTo !== null) {
		// Each slice checks its first instruction too, except at the
		// start of the Run to (a slice ended by the time budget or a new
		// line on the screen can stop right before a target).
		const before = stepper.steps;
		reason = stepper.run(Infinity, runTo, SLICE_MS, runToMoved ? -1 : runToFrom);
		runToMoved = runToMoved || stepper.steps !== before;
	} else if (speed() === "full") {
		reason = stepper.run(Infinity, -1, SLICE_MS);
	} else {
		// Credit for the real time since the last tick, however long a
		// drawing took (a cap on the time made a 1 s drawing of the listing
		// cost 0.75 s of credit: 1 instruction in 4 s at 1/s); the burst
		// after a long tick is capped at a second's worth.
		const rate = Number(speed());
		const dt = prevTick ? now - prevTick : 16;
		stepCredit = Math.min(stepCredit + rate * dt / 1000, rate + 1);
		const n = Math.floor(stepCredit);
		stepCredit -= n;
		reason = n ? stepper.run(n, -1, SLICE_MS) : "count";
	}
	prevTick = now;
	if (counting) {
		countMs += realNow() - t0;
		countSteps += stepper.steps - s0;
	}
	if (reason === "addr") {
		pause(runToStopped(runToLabel, runTo.size, "stepped"));
		return;
	}
	if (reason === "prompt") {
		counting = false;
		flushScreen(); // v86 draws the screen only while it runs
		pause((runTo !== null ? "Run to " + runToLabel + ": not reached before the prompt. " : "") + "at the (initramfs) prompt");
		return;
	}
	if (reason === "hlt" && stepper.dead())
		stepper.markHalt();
	if (reason === "end" || (reason === "hlt" && stepper.dead())) {
		counting = false;
		pause((runTo !== null ? "Run to " + runToLabel + ": not reached before the end. " : "") + endText());
		return;
	}
	if (counting && fastStepping() && countMs > COUNT_LIMIT_MS) {
		counting = false;
		countNote = "Gave up after " + Math.round(countMs / 1000) + " s of stepping.";
		if (speed() === "full" && runTo === null) {
			updateDebugUi();
			resume();
			return;
		}
	}
	if (reason === "hlt" && stepper.dead()) {
		pause("The CPU executed hlt with interrupts off: nothing more to run.");
		return;
	}
	// Slow motion: the Debugger's state when an instruction has executed
	// (or every 250 ms for the clock), the views that follow EIP when
	// frameUpdate says so; not on every animation frame.
	const slow = !fastStepping();
	const ran = stepper.steps !== uiSteps;
	if (slow && !highRate() ? ran || now - lastUi > 250 : now - lastUi > (highRate() ? 100 : 200)) {
		lastUi = now;
		uiSteps = stepper.steps;
		updateDebugUi();
	}
	// Fast stepping (Run to, counting, the way to the entry) follows EIP
	// too, a few times a second: otherwise Code at EIP and the listing
	// box the instruction where the run started while the count goes on.
	frameUpdate(!slow);
	if (reason === "hlt")
		driverTimer = setTimeout(scheduleDriver, slow ? 16 : 1);
	else
		scheduleDriver();
}

// Live view while stepping: grid flashes, asm view follows EIP. Drawn
// only when an instruction has executed since the last drawing, and after
// a pause at least as long as that drawing took (VIEW_GAP_MS or more): a
// listing drawn on every animation frame kept a phone's main thread busy
// all the time. The step credit counts real time, so a slow drawing
// shows fewer of the instructions, never slows the run. fast: stepping as
// fast as possible (Run to, counting): every FAST_VIEW_GAP_MS, or after
// twice the drawing's time, so the drawing takes a third of the time at
// most.
const VIEW_GAP_MS = 200, FAST_VIEW_GAP_MS = 300;
let viewStepper = null, viewSteps = -1, viewAt = 0, viewCost = 0;
let lastFrameScan = 0;
function frameUpdate(fast = false) {
	if (stepper === viewStepper && stepper.steps === viewSteps)
		return;
	const t0 = realNow();
	if (stepper === viewStepper && t0 - viewAt < (fast ? Math.max(FAST_VIEW_GAP_MS, 2 * viewCost) : Math.max(VIEW_GAP_MS, viewCost)))
		return;
	viewStepper = stepper;
	viewSteps = stepper.steps;
	frameDraw(t0);
	viewAt = realNow();
	viewCost = viewAt - t0;
}

// The listing is out of date (DWARF arrived): in slow motion it is drawn
// whole at the next frameUpdate (several changes in a row, one drawing),
// otherwise now.
function listingStale() {
	if (runState !== "stepped" || fastStepping()) {
		inspect();
		return;
	}
	$("listing")._drawn = null;
	viewSteps = -1;
}

function frameDraw(now) {
	if (now - lastFrameScan >= (emuRam > 32 * 1024 * 1024 ? 2000 : emuRam > 4 * 1024 * 1024 ? 250 : 0)) {
		lastFrameScan = now;
		scan(false);
		const n = Number($("footprint").dataset.changed || 0);
		$("pages").dataset.flashed = n;
		if (n)
			$("pages").dataset.flashes = ++flashes;
	}
	followEip(true);
}

function afterStop() {
	// Baseline for the grid: pages written after this flash.
	scan(false);
	updateDebugUi();
	followEip();
}

// While paused or stepping, the Page tab shows the page holding EIP.
// moving: in slow motion (listingFollow may only move the mark).
function followEip(moving = false) {
	if (!stepper || runState === "jit")
		return;
	const phys = stepper.nextPhys();
	if (phys >= 0 && phys < emuRam) {
		const p = Math.floor(phys / PAGE);
		if (p !== selectedPage) {
			selectedPage = p;
			writeHash();
			if (tab === "memory")
				drawGrid();
		}
		if (tab === "page" && !(moving && listingFollow()))
			inspect();
	}
	renderContext();
}

// Code symbols for the stepper's symbol history: sized, in .text*, one per
// start address, sorted.
function codeSymbols() {
	if (!symbols)
		return [];
	// Several names often share an address (irq_entries_start and the section
	// marker __irqentry_text_start): name the code by its function, not by a
	// linker marker, so markers sort after real symbols at the same address.
	const marker = (n) => /^__\w*_(start|end)$|^_(s|e)?(text|data|inittext)$/.test(n) ? 1 : 0;
	const list = symbols.symbols.filter((s) => s.size > 0 && /^\.text|^\.init\.text|^\.head\.text/.test(s.section))
		.slice().sort((x, y) => x.paddr - y.paddr || marker(x.name) - marker(y.name));
	const seen = new Set();
	return list.filter((s) => !seen.has(s.paddr) && seen.add(s.paddr));
}

// Open the Page tab at physical address a and scroll its line into view.
function jumpTo(a) {
	if (a < 0 || a >= emuRam)
		return;
	navJump(() => {
		selectedPage = Math.floor(a / PAGE);
		drawGrid();
		showTab("page");
		// The listing may be drawn again soon (the Source panel opening
		// narrows it): it keeps this line in view, not EIP's.
		navScrollWant = { page: selectedPage, addr: a, until: realNow() + NAV_SCROLL_MS };
		const best = listingLineAt(a);
		if (best)
			scrollListingTo(best);
	});
}

// The listing's last line at or before address a.
function listingLineAt(a) {
	let best = null;
	for (const line of $("listing").querySelectorAll("[data-addr]"))
		if (Number(line.dataset.addr) <= a)
			best = line;
	return best;
}

// Open a function in the code browser (the Calls tab's double click, G1):
// the Page tab at its first instruction (the symbol's header line is just
// above it) and the Source panel at the function's line, the frame of its
// inline chain that is the function itself (else the outermost). One
// history entry. While its DWARF shard loads, the panel opens when it
// arrives, if the user is still on that view.
let openFnWant = null;

function openFnSource(pa, name) {
	const r = dbgFrames(pa);
	if (r.state !== "ok")
		return r.state;
	let k = r.frames.findIndex((f) => f.func === name && f.file && f.line);
	if (k < 0)
		k = r.frames.length - 1;
	openSource(pa, k);
	return "ok";
}

// On a phone the Source panel is a sheet over the lower part of the
// screen: scroll the window so the function's header line and first
// instruction sit in the strip between the top bar and the sheet.
function fnHeaderInView(pa) {
	const sp = $("src_panel");
	if (sp.hidden || tab !== "page")
		return;
	const pr = sp.getBoundingClientRect();
	if (pr.left > 0 || pr.top <= 0)
		return;
	// The listing's top under the top bar (as far as the page scrolls),
	// then the line a third of the way down the strip.
	const top = document.querySelector(".top").getBoundingClientRect().bottom;
	window.scrollBy(0, $("listing").getBoundingClientRect().top - top - 4);
	const line = listingLineAt(pa);
	if (line)
		scrollListingTo(line);
	// The symbol's header line (two rows when it wraps) stays in view too.
	const hdr = $("listing").querySelector('.lbl[data-addr="' + pa + '"]'), band = sheetBand($("listing").getBoundingClientRect());
	if (hdr && band && hdr.getBoundingClientRect().top < band[0])
		$("listing").scrollTop -= band[0] - hdr.getBoundingClientRect().top + 4;
}

// outline: the first instruction gets the jump outline (a bookmark, K1).
function openFunction(pa, name, outline = false) {
	openFnWant = null;
	navJump(() => {
		if (outline) {
			jumpHit = pa;
			jumpHitScroll = true;
		}
		jumpTo(pa);
		if (openFnSource(pa, name) === "loading")
			openFnWant = { pa, name, page: selectedPage, until: realNow() + 30000 };
		fnHeaderInView(pa);
	});
	if (openFnWant)
		openFnWant.at = navAt;
}

dbgOnChange(() => {
	const w = openFnWant;
	if (!w)
		return;
	if (tab !== "page" || selectedPage !== w.page || navAt !== w.at || realNow() > w.until) {
		openFnWant = null;
		return;
	}
	// Part of the jump that asked for it: no entry of its own.
	navDepth++;
	try {
		if (openFnSource(w.pa, w.name) !== "loading") {
			openFnWant = null;
			fnHeaderInView(w.pa);
		}
	} finally {
		navDepth--;
	}
});

const symDesc = (st, i, a) => {
	if (i < 0)
		return a >= 0 && a < V86_KERNEL_ADDR ? "(outside the kernel: BIOS, option ROM or setup)" :
			a >= V86_KERNEL_ADDR && image && a < kernelEnd(image.hdr) && i === -1 ?
				"(not vmlinux code: relocation stub or not loaded yet)" : st.symLabel(i);
	return st.symName[i];
};

function symButton(text, addr, title) {
	const b = document.createElement("button");
	b.type = "button";
	b.className = "symlink";
	b.textContent = text;
	if (title)
		b.title = title;
	if (addr >= 0)
		b.addEventListener("click", () => jumpTo(addr));
	else
		b.disabled = true;
	return b;
}

// Debugger tab: current and previous symbol, the last few symbols, and
// the disassembly around EIP.
function renderSymbols(phys) {
	const box = $("sym_info");
	box.textContent = "";
	const st = stepper;
	const cur = phys >= 0 ? st.symbolOf(phys) : -1;
	const row = (label, ...nodes) => {
		const d = document.createElement("div");
		const b = document.createElement("b");
		b.textContent = label;
		d.append(b, " ", ...nodes);
		box.append(d);
		return d;
	};
	const c = row("current");
	c.id = "cur_sym";
	c.title = "The symbol (symbols.json or the imported map) that holds EIP: its address range and EIP's offset in it";
	if (cur >= 0) {
		const lo = st.symStart[cur], hi = st.symEnd[cur];
		c.append(symButton(st.symName[cur], lo), " " + hex(lo) + "–" + hex(hi - 1) + ", " + (hi - lo) +
			" bytes, EIP at +0x" + (phys - lo).toString(16));
		Object.assign(c.dataset, { name: st.symName[cur], lo, hi });
	} else {
		c.append(symDesc(st, -1, phys));
		Object.assign(c.dataset, { name: "", lo: "", hi: "" });
	}
	// History entries are symbols the CPU left; the newest one that is not
	// the current symbol is "previous".
	const hist = st.history.filter((h) => !(h.sym === cur && h.sym >= 0));
	const prev = hist.length ? hist[hist.length - 1] : null;
	renderSourceRows(box, phys, cur >= 0 ? st.symName[cur] : null);
	if (dbgHas())
		renderFnRows(box, phys);
	renderTaskRow(box);
	const uw = renderCallStack(box, phys);
	const p = row("previous");
	p.id = "prev_sym";
	p.title = "The symbol the CPU was in before the current one, recorded while stepping (not while v86's JIT runs)";
	if (prev && prev.sym >= 0) {
		p.append(symButton(st.symName[prev.sym], prev.last, "Jump to the last instruction it ran\n" + dbgTitle(prev.last, st.symName[prev.sym])),
			" left at " + hex(prev.last));
		p.dataset.name = st.symName[prev.sym];
	} else {
		p.append(prev ? symDesc(st, prev.sym, prev.last) : "none recorded yet");
		p.dataset.name = "";
	}
	const crumbs = row("path");
	crumbs.className = "crumbs";
	const names = [cur >= 0 ? st.symName[cur] : symDesc(st, -1, phys)];
	const items = [symButton(names[0], cur >= 0 ? st.symStart[cur] : -1, cur >= 0 ? dbgTitle(phys, names[0]) : "")];
	for (let k = hist.length - 1; k >= 0 && items.length < 6; k--) {
		const h = hist[k];
		items.push(symButton(symDesc(st, h.sym, h.last), h.sym >= 0 ? h.last : -1, h.sym >= 0 ? "left at " + hex(h.last) + "\n" + dbgTitle(h.last, st.symName[h.sym]) : ""));
	}
	items.forEach((it, k) => crumbs.append(k ? " ← " : "", it));
	crumbs.dataset.count = items.length;
	return uw;
}

// The call stack, innermost first. With DWARF the backtrace is unwound
// from the registers and the stack (dbginfo.js dbgUnwind: the CFI of each
// function, entry code's saved pt_regs, else the EBP chain); where that
// stops, the frames below come from the call stack the stepper
// reconstructed while stepping (stepper.js stackEnter, the ESP
// heuristic), and without DWARF all of them do. Each frame names the
// method that found it, and carries the DWARF file:line and inlined-at
// chain of its instruction (EIP for the top one, the call for the others)
// in its title.
const FRAME_METHODS = {
	regs: ["EIP", "the CPU's registers: where it is now"],
	cfi: ["CFI", "unwound through the call frame information (.debug_frame) of the function it called: exact"],
	pt_regs: ["pt_regs", "the context an interrupt, exception or system call interrupted, from the registers its entry code saved: exact"],
	entry: ["entry ESP", "the return address at the ESP the stepper saw this function's caller call it with, where the function inside had no call frame information: exact when the function (assembly) keeps EBX, ESI, EDI and EBP"],
	fp: ["EBP chain", "frame-pointer chain ([EBP] and [EBP+4]) where the function inside had no call frame information: a function without a frame of its own may be missing"],
	recorded: ["recorded", "the call stack recorded while stepping (calls and returns seen, ESP heuristic): only as complete as the recording"],
};

// Registers and memory for dbgUnwind: guest memory through the page tables
// (cpustate.js pageWalk: read only, no TLB or accessed-bit side effects).
function unwindHere() {
	const st = stepper, cpu = st.cpu;
	const read32 = (va) => {
		const w = pageWalk(cpu, va), w2 = (va & 0xfff) > 0xffc ? pageWalk(cpu, va + 3) : w;
		if (w.phys === null || w2.phys === null)
			return undefined;
		if (w2 === w) {
			const v = phys32(cpu, w.phys);
			return v === null ? undefined : v;
		}
		let v = 0;
		for (let k = 3; k >= 0; k--) {
			const q = pageWalk(cpu, va + k).phys;
			if (q === null || q >= cpu.mem8.length)
				return undefined;
			v = v * 256 + cpu.mem8[q];
		}
		return v;
	};
	// Code of a symbol, or just past one: the return address of a call
	// that does not return can be the end of its function (startup_32's
	// call of hello_main in the hello kernel).
	const toPhys = (va) => {
		const pa = pageWalk(cpu, va).phys;
		return pa !== null && (st.symbolOf(insnStart(pa)) >= 0 || st.symbolOf(insnStart(pa - 1)) >= 0) ? pa : null;
	};
	const symAt = (pa) => {
		const i = st.symbolOf(insnStart(pa));
		return i >= 0 ? [st.symName[i], st.symStart[i]] : null;
	};
	// An open frame the stepper recorded for this function, entered at or
	// above esp on the same stack.
	const entryEsp = (pa, esp) => {
		// The instruction that ran last was a call to here (EIP has not
		// entered the function yet): the return address is at ESP.
		const m = cpu.mem8, lp = st.lastPhys;
		if (esp === st.regs().esp && lp >= 0 && lp + 2 < m.length && ((st.espBefore - 4) >>> 0) === esp &&
		    (m[lp] === 0xe8 || (m[lp] === 0xff && (m[lp + 1] >> 3 & 7) === 2)))
			return esp;
		const i = st.symbolOf(insnStart(pa));
		for (const k of st.allOpen())
			if (st.fNode[k] === i && st.fEsp[k] >= esp && st.fEsp[k] >>> 13 === esp >>> 13 && !st.fCont[k])
				return st.fEsp[k];
		return undefined;
	};
	return dbgUnwind(st.regs(), read32, toPhys, symAt, entryEsp);
}

// The instruction holding pa (a caller's frame is at its return address - 1:
// inside the call), from the build's disassembly; pa without one.
function insnStart(pa) {
	if (!disasm)
		return pa;
	const k = insnIndex(pa), ins = disasm.insns;
	return k < ins.length && ins[k][0] === pa ? pa : k > 0 && ins[k - 1][0] < pa && pa - ins[k - 1][0] < 16 ? ins[k - 1][0] : pa;
}

function renderCallStack(box, phys) {
	const st = stepper;
	const d = document.createElement("div");
	d.id = "call_stack";
	const b = document.createElement("b");
	b.textContent = "call stack";
	d.append(b, " ");
	const top = st.stack.length ? st.stack[st.stack.length - 1] : -1;
	const rec = [];
	let at = phys;
	// EIP on the first byte of a symbol the stack has not entered yet (a
	// call about to run, e.g. after Run to): that symbol on top, its caller
	// at the instruction that ran last.
	const cur = phys >= 0 ? st.symbolOf(phys) : -1;
	if (cur >= 0 && phys === st.symStart[cur] && (top < 0 || st.fNode[top] !== cur)) {
		rec.push([-1, phys, cur]);
		at = st.lastPhys;
	}
	for (let k = top; k >= 0 && rec.length < 24; k = st.fParent[k]) {
		rec.push([k, at, st.fNode[k]]);
		at = st.fFrom[k];
	}
	// [pc, node, method, extra title, cfa, registers known in the frame,
	// virtual EIP or return address, label of a frame that is no code here]
	let frames = rec.map(([k, pc, node]) => [pc, node, "recorded", k >= 0 && st.fIrq[k] ? ", in an interrupt" : "", undefined]);
	const uw = runState === "paused" || runState === "stepped" ? unwindHere() : { frames: [], state: "none", why: "" };
	if (uw.frames.length) {
		frames = uw.frames.map((f) => f.user ? [-1, -1, f.method, "user space: EIP 0x" + (f.pc >>> 0).toString(16) +
			", ESP 0x" + (f.regs[4] >>> 0).toString(16), undefined, f.regs, f.pc, "user space"] :
			[insnStart(f.lookup), st.symbolOf(insnStart(f.lookup)), f.method,
			(f.cfa !== undefined ? ", CFA 0x" + (f.cfa >>> 0).toString(16) : "") + (f.row ? "\nCFI row: " + cfiRowText(f.row) : "") +
			(f.fallbackWhy ? "\nno CFI step here: " + f.fallbackWhy : "") + (f.ptRegs !== undefined ? "\npt_regs at 0x" + f.ptRegs.toString(16) : ""), f.cfa, f.regs, f.pc]);
		if (uw.state !== "ok") {
			// The recorded frames below the last unwound one: those that
			// entered at or above its ESP on the same stack, minus itself.
			const last = uw.frames[uw.frames.length - 1];
			const esp = last.regs[4];
			let j = esp === undefined ? -1 : rec.findIndex(([k]) => k >= 0 && st.fEsp[k] >>> 13 === esp >>> 13 && st.fEsp[k] >= esp);
			if (j >= 0 && rec[j][2] === st.symbolOf(last.lookup))
				j++;
			if (j >= 0)
				for (const [k, pc, node] of rec.slice(j))
					frames.push([pc, node, "recorded", st.fIrq[k] ? ", in an interrupt" : "", undefined]);
		}
	}
	if (!frames.length)
		d.append(runState === "jit" ? "not recorded at full speed" : "none recorded yet (calls are seen while stepping)");
	const list = [];
	frames.forEach(([pc, node, method, extra, , , , label], n) => {
		const name = label || (node >= 0 ? st.nodeName(node) : "0x" + pc.toString(16));
		const r = pc >= 0 ? dbgFrames(pc) : { state: "none" };
		const f0 = r.state === "ok" && r.frames[0].file && r.frames[0].line ? r.frames[0] : null;
		const inl = r.state === "ok" && r.frames.length > 1 ? " [" + r.frames.length + " inlined]" : "";
		const m = FRAME_METHODS[method];
		const btn = symButton(name, pc, label ? extra + "\n" + m[0] + ": " + m[1] :
			(n ? "calls from " : "at ") + hex(pc) + extra + "\n" + m[0] + ": " + m[1] + "\n" + dbgTitle(pc, name));
		const tag = document.createElement("span");
		tag.className = "fmeth";
		tag.textContent = m[0];
		btn.append(inl, " ", tag);
		btn.dataset.pc = pc;
		btn.dataset.method = method;
		// The frame's name opens its code in the Page tab, its file:line the Source panel.
		const item = document.createElement("span");
		item.className = "cframe";
		item.append(btn);
		// A function bookmark of the frame's function (K1): its ☆, and ★ once bookmarked.
		const fnOf = !label && node >= 0 && node < st.symName.length ? { pa: st.symStart[node], name: st.symName[node] } : null;
		if (fnOf) {
			item.dataset.fnpa = fnOf.pa;
			item.dataset.fn = fnOf.name;
			const names = bmFnNames(fnOf.pa);
			if (names)
				item.append(bmStarEl(names));
			item.append(bmAddButton(() => bmDraftFn(fnOf.pa, fnOf.name), "Bookmark " + fnOf.name + " (a function bookmark)"));
		}
		if (f0)
			item.append(" ", srcLinkEl(f0.file, f0.line, "", pc));
		list.push(item);
	});
	list.forEach((it, n) => d.append(n ? " ← " : "", it));
	if (uw.state === "ok" && uw.why || uw.state === "stopped" || uw.state === "loading") {
		const note = document.createElement("span");
		note.className = "dim";
		note.textContent = uw.state === "ok" ? " (" + uw.why + ")" : uw.state === "loading" ? " (" + uw.why + ")" :
			" (unwinding stopped: " + uw.why + (frames.length > uw.frames.length ? "; the frames below were recorded while stepping" : "") + ")";
		d.append(note);
	}
	d.title = dbgCfiInfo() ? "Unwound with the call frame information: " + dbgCfiInfo() : "";
	d.dataset.count = frames.length;
	d.dataset.unwind = uw.state;
	d.dataset.why = uw.why || "";
	d.dataset.frames = JSON.stringify(frames.map(([pc, node, method, , cfa, regs, va, label]) =>
		({ name: label || (node >= 0 ? st.nodeName(node) : ""), pc, method, cfa: cfa === undefined ? null : cfa, regs, va, user: !!label })));
	box.append(d);
	return uw;
}

// The "C source" toggle is there when the kernel has DWARF.
function syncSrcToggle() {
	$("src_toggle").hidden = !dbgHas();
}

// The Debugger tab drawn again (its file:line links keep the keyboard focus).
// The mark of the EIP line in Code at EIP, with its popover.
const EIP_MARK = '<span class="eipmark tipmark" tabindex="0" role="button" aria-label="EIP" data-tip="EIP: the next instruction the CPU ' +
	'runs (the boxed line). Tap the line itself to open it in the Page tab.">▶</span>';

function renderContext() {
	srcFocusKeep(drawContext);
}

function drawContext() {
	renderWatches();
	const el = $("context");
	if (!stepper || tab !== "debugger" || runState === "jit" || runState === "off") {
		el.textContent = runState === "jit" ? "Running at full speed (JIT). Pause to see the code at EIP; the symbol history is not recorded at full speed." : "";
		$("sym_info").textContent = "";
		$("eip_jumps").textContent = "";
		if (tab === "debugger")
			renderLocals(null);
		return;
	}
	const phys = stepper.nextPhys();
	if (phys < 0) {
		el.textContent = "EIP is not mapped.";
		renderLocals(null);
		$("eip_jumps").textContent = "";
		return;
	}
	renderLocals(renderSymbols(phys));
	const lines = [];
	let ins = disasm ? disasm.insns : [];
	let i = disasm ? insnIndex(phys) : 0;
	let exact = i < ins.length && ins[i][0] === phys;
	const inKernel = image && phys >= V86_KERNEL_ADDR && phys < kernelEnd(image.hdr);
	let live = false;
	if ((!exact || liveDecode()) && iced) {
		// Decode guest memory from the containing symbol's start (aligned)
		// through EIP and ~10 instructions after it; without a symbol, from EIP.
		const sym = symbols ? symbols.symbols.find((x) => x.size > 0 && x.paddr <= phys && phys < x.paddr + x.size &&
			phys - x.paddr < 0x10000) : null;
		const start = sym ? sym.paddr : phys;
		const stop = Math.min(emuRam, phys + 160);
		if (stop > start) {
			const list = decodeLive(emulator.read_memory(start, stop - start), start, (start + virtOffset(start)) >>> 0, bitnessAt(phys));
			const k = list.findIndex((e) => e[0] === phys);
			if (k >= 0) {
				ins = list;
				i = k;
				exact = true;
				live = true;
				el.dataset.bits = bitnessAt(phys);
			}
		}
	} else if (!exact && !iced && !icedError)
		loadIced();
	if (!exact) {
		const n = Math.max(0, Math.min(16, emuRam - phys));
		const b = n > 0 ? emulator.read_memory(phys, n) : [];
		lines.push('<span class="cur" data-addr="' + phys + '">' + phys.toString(16).padStart(8, "0") + " " +
			Array.from(b, (v) => v.toString(16).padStart(2, "0")).join(" ") + "</span>");
		lines.push('<span class="dim">' + (!inKernel ?
			"EIP is outside the kernel image (BIOS, option ROM or real-mode setup): disasm.json has no code here, raw bytes at EIP." :
			"EIP is not an instruction start in the build's disassembly (the code here is not vmlinux's yet, e.g. the bzImage's relocation stub): raw bytes at EIP." +
			(disasm && i < ins.length && ins[i][0] < phys + 64 ? " Nearest instructions from the build:" : "")) + "</span>");
	}
	el.dataset.source = live ? "live" : exact ? "disasm" : "bytes";
	let jS = null;
	const paths = new Map();
	if (live)
		lines.push('<span class="dim">decoded live from guest memory (iced-x86, ' + bitnessAt(phys) + "-bit)</span>");
	if ((disasm && inKernel) || live) {
		const from = Math.max(0, exact ? i - 6 : i - 3), to = Math.min(ins.length, i + 10);
		if (to > from) {
			// The end of the furthest instruction: llvm-objdump decodes the
			// zero padding before an aligned function as instructions that
			// can run into the function (-O0: "0050c7" at 0x100d4f over
			// 0x100d50), so the last one listed need not end last.
			const lo = ins[from][0];
			let hi = lo;
			for (let k = from; k < to; k++)
				hi = Math.max(hi, ins[k][0] + ins[k][1].length / 2);
			const mem = hi <= emuRam ? emulator.read_memory(lo, hi - lo) : null;
			const sized = symbols ? symbols.symbols.filter((s) => s.size > 0 && s.paddr < hi && s.paddr + s.size > lo) : [];
			const srcState = { key: null };
			const ops = live && bitnessAt(phys) === 16 ? null : opEnv(), opStat = {};
			jS = jumpsFor(ins);
			const shown = new Set(ins.slice(from, to).map((x) => x[0]));
			const inView = (x) => shown.has(x), imax = jumpInlineMax(el);
			for (let k = from; k < to; k++) {
				const [pa, raw, text] = ins[k];
				const srcLine = srcLineHtml(pa, srcState);
				if (srcLine)
					lines.push(srcLine);
				let bytes = "", ok = true;
				for (let j = 0; j < raw.length / 2; j++) {
					const want = raw.substr(j * 2, 2);
					const got = mem ? mem[pa - lo + j].toString(16).padStart(2, "0") : want;
					if (got !== want)
						ok = false;
					if (j < 7)
						bytes += got !== want ? "<mark>" + got + "</mark>" : got;
				}
				const tip = ok || !mem ? "" : diffTip(pa, raw, mem.subarray(pa - lo, pa - lo + raw.length / 2));
				if (tip)
					bytes = diffBytesHtml(bytes, tip);
				bytes += raw.length / 2 > 7 ? "…" : "  ".repeat(7 - raw.length / 2);
				const sym = symbolAt(sized, pa);
				const cls = "ins" + (pa === phys ? " cur" : "") + (ok ? "" : " bad");
				const P = jumpPaths(jS, stepper, pa);
				paths.set(pa, P);
				const J = jumpInlineHtml(jS, P, pa, inView, imax);
				lines.push(J.src + '<span class="' + cls + '" data-addr="' + pa + '"' + (tip ? ' data-tip="' + escHtml(tip) + '"' : "") +
					jumpStubAttrs(J) + ">" + (pa === phys ? EIP_MARK : " ") +
					pa.toString(16).padStart(8, "0") + " " + bytes + " " + insnHtml(pa, text, ops, opStat) +
					(sym ? ' <span class="dim">' + escHtml(sym.name) + "+0x" + (pa - sym.paddr).toString(16) + "</span>" : "") +
					bmCodeStar(pa) + "</span>" + J.dst + jumpBadgesHtml(P, pa));
			}
		}
	}
	el.innerHTML = lines.join("\n");
	el.dataset.draws = Number(el.dataset.draws || 0) + 1;
	el.dataset.jlocal = live ? "instructions decoded live around EIP" : "";
	jumpsDrawn(el, jS, paths);
	renderEipJumps(exact ? jS : null, phys);
	el.dataset.phys = phys;
	el.dataset.opsNamed = el.querySelectorAll(".opn").length;
	const cur = el.querySelector(".ins.cur");
	el.dataset.cur = cur ? cur.dataset.addr : "";
}

// EIP to highlight in the inspector, or -1.
function currentPhys() {
	return stepper && runState !== "jit" && runState !== "off" ? stepper.nextPhys() : -1;
}

const h8 = (n) => (n >>> 0).toString(16).padStart(8, "0");
const h4 = (n) => n.toString(16).padStart(4, "0");
const FLAG_BITS = [[0, "CF"], [2, "PF"], [4, "AF"], [6, "ZF"], [7, "SF"], [8, "TF"], [9, "IF"], [10, "DF"], [11, "OF"], [14, "NT"], [16, "RF"], [17, "VM"]];

function renderRegs() {
	const el = $("regs");
	if (!stepper) {
		el.textContent = "";
		return;
	}
	const r = stepper.regs();
	const flags = FLAG_BITS.filter(([b]) => r.eflags & (1 << b)).map(([, n]) => n);
	flags.push("IOPL" + ((r.eflags >> 12) & 3));
	const mode = !(r.cr0 & 1) ? "real mode" : (r.eflags & 0x20000 ? "vm86" : "protected") +
		(r.cr0 & 0x80000000 ? ", paging" : "");
	const phys = stepper.nextPhys();
	const cells = [
		["EIP", h8(r.eip), "Instruction pointer (offset in CS). Linear " + hex(r.linear) + ", physical " + (phys < 0 ? "unmapped" : hex(phys))],
		["ESP", h8(r.esp)], ["EBP", h8(r.ebp)],
		["EAX", h8(r.eax)], ["EBX", h8(r.ebx)], ["ECX", h8(r.ecx)], ["EDX", h8(r.edx)],
		["ESI", h8(r.esi)], ["EDI", h8(r.edi)],
		["EFLAGS", h8(r.eflags) + " " + flags.join(" "), "Set flags of EFLAGS"],
		["CS", h4(r.cs)], ["DS", h4(r.ds)], ["SS", h4(r.ss)], ["ES", h4(r.es)],
		["CR0", h8(r.cr0), "PE=" + (r.cr0 & 1) + " PG=" + (r.cr0 >>> 31)], ["CR3", h8(r.cr3), "Page directory base"],
		["CPL", String(r.cpl), "Current privilege level"],
		["mode", mode + ", " + (r.is32 ? "32" : "16") + "-bit" + (r.hlt ? ", hlt" : "")],
		["phys", phys < 0 ? "unmapped" : h8(phys), "Physical address of the next instruction"],
	];
	el.textContent = "";
	for (const [name, value, title] of cells) {
		const d = document.createElement("div");
		d.dataset.reg = name;
		const chg = !!prevRegs && prevRegs[name] !== undefined && prevRegs[name] !== value;
		if (chg)
			d.className = "chg";
		const b = document.createElement("b");
		b.textContent = name;
		d.append(b, " " + value);
		if (title || chg)
			d.title = (title ? title + ". " : "") + (chg ? "Yellow: changed since the registers were last shown" : "");
		if (name === "EFLAGS" || name === "mode")
			d.classList.add("wide");
		el.append(d);
	}
	prevRegs = Object.fromEntries(cells.map(([n, v]) => [n, v]));
	el.dataset.eip = r.eip;
	el.dataset.phys = phys;
	renderCpuState();
}

const num = (n) => (n === null || n === undefined ? "-" : n.toLocaleString("en"));

function renderCounters() {
	const tbody = $("counters").tBodies[0];
	tbody.textContent = "";
	if (!stepper) {
		$("count_text").textContent = !emulator ? "" : "Not counted: " + (wasmPatchError || "no debugger") + ".";
		return;
	}
	const rows = stepper.milestones.map((m) => [m.text, m.found, m.text]);
	rows.push([stepper.exit ? "exit(" + stepper.exit.code + ")" : "exit()", stepper.exit, "exit"]);
	if (stepper.panicSym >= 0)
		rows.push(["panic() entered", stepper.panicEntered, "panic_entry"]);
	if (stepper.prompt)
		rows.push(["(initramfs) prompt", stepper.prompt, "prompt"]);
	rows.push([stepper.end ? (/panic/.test(stepper.end.kind) ? "panic, stopped" : "halted") : "end", stepper.end, "end"]);
	rows.push(["now", stepper.snapshot(), "now"]);
	for (const [label, c, key] of rows) {
		const tr = tbody.insertRow();
		tr.dataset.milestone = key;
		const cells = c && c.jit ? [label, "run by the JIT", "", ""] :
			c ? [label, num(c.sinceEntry), num(c.uniqueKernel) + "/" + num(c.unique - c.uniqueKernel), num(c.steps)] :
			[label, counting ? "counting..." : "not reached", "", ""];
		for (const v of cells)
			tr.insertCell().textContent = v;
		if (c && !c.jit) {
			tr.dataset.steps = c.steps;
			tr.dataset.entry = c.sinceEntry === null ? "" : c.sinceEntry;
			tr.dataset.unique = c.unique;
			tr.dataset.kernel = c.uniqueKernel;
			tr.cells[0].title = "Reached by the instruction at physical " + hex(c.phys);
		}
	}
	const alive = stepper.milestones.find((m) => m.text === "Still alive");
	// Once per measurement: after a Reset in the Settings tab the next redraw
	// does not write it back.
	if (alive && alive.found && alive.found.sinceEntry && image && refSaved !== alive.found)
		try {
			refSaved = alive.found;
			localStorage.setItem(REF_KEY + image.name + "-" + (clockMips() || "real"), alive.found.sinceEntry);
		} catch (e) {
			// no storage: the built-in estimate stays
		}
	// What the stepping did in its time: not the instructions the JIT ran
	// (the fast-forward to the entry: 2.3e9 for Debian, J2 saw "3918091.6
	// M/s") nor the BIOS that the fast-forward steps.
	const rate = countMs > 0 ? countSteps / countMs / 1000 : 0;
	const info = $("count_text");
	$("count_info").dataset.ms = Math.round(countMs);
	if (runState === "stepped" && !fastStepping() && !countDone()) {
		info.textContent = "Counting from the kernel entry: " + num(stepper.steps - Math.max(0, stepper.sinceEntry)) +
			" instructions so far. " + eta() + countNote;
		return;
	}
	info.textContent = (counting ? (runState === "paused" ? "Counting (paused): " : "Counting: ") : stepper.steps ? "Counted " : "") +
		(stepper.steps ? num(stepper.steps) + (stepper.jitBeforeEntry || stepper.jitAfterEntry ? " instructions (stepped and run by the JIT)" : " instructions stepped") +
			(countMs ? ", " + (countMs / 1000).toFixed(2) + " s of JavaScript stepping time (" + rate.toFixed(1) + " M/s)" : "") + ". " : "") +
		(!$("count").checked && !stepper.steps ? "Counting is off: the boot ran at full speed. " : "") +
		(runState === "jit" && !countDone() && stepper.steps ? (CLOCK.virtual ? "Running at full speed on the virtual clock: counted, the same run every time. " :
			"Running at full speed on real time now: not counting. ") : "") +
		countNote;
}

// Virtual clock rate in instructions per microsecond, or 0 for real time.
const MIPS_KEY = "mkb-mips";
const clockMips = () => ($("clock_mips").value === "real" ? 0 : Number($("clock_mips").value));

// Real speed: v86's instruction counter (wasm memory, offset 664) counts
// what the JIT runs and what the stepper steps; sampled 4 times a second.
// The rate is taken over a window (4 s in slow motion, 1 s otherwise),
// counted from the last change (Boot, Pause, Continue, a speed, the entry,
// the tab shown again): a one-second window at 1/s read 0/s whenever it
// held no instruction, and a window reaching back into a pause or another
// speed showed that one. At 10/s a one- or two-second window read 1/s to
// 4/s after one drawing of a new page's listing held a phone's main
// thread for 1.5 to 2.2 s, which the step credit makes up right after.
// Until half the window has passed it says "measuring".
const SPEED_SAMPLE_MS = 250;
let speedHist = [];      // [{ counter, at }] since the last change, oldest first
let realSpeed = null;    // instructions per second, null while measuring
function speedWindowMs() {
	if (runState !== "stepped" || fastStepping() || highRate())
		return 1000;
	return 4000;
}
// Paused or off, unless a fast-forward runs (the boot's to the entry sets
// "paused" under it): that one says "measuring" until it has a rate.
const speedStopped = () => !ffActive && (runState === "paused" || runState === "off");
function speedReset() {
	speedHist = [];
	realSpeed = speedStopped() ? 0 : null;
	if (stepper)
		renderClock();
}
function sampleSpeed() {
	if (!stepper || !emuReady) {
		speedHist = [];
		realSpeed = null;
		return;
	}
	const c = new Uint32Array(stepper.cpu.wasm_memory.buffer, 664, 1)[0];
	const now = realNow();
	speedHist.push({ counter: c, at: now });
	const w = speedWindowMs();
	while (speedHist.length > 2 && now - speedHist[1].at >= w)
		speedHist.shift();
	const first = speedHist[0];
	if (speedStopped())
		realSpeed = 0;
	else if (now - first.at >= w / 2)
		realSpeed = ((c - first.counter) >>> 0) / ((now - first.at) / 1000);
	else
		realSpeed = null;
	renderClock();
}

const fmtRate = (r) => (r >= 1e6 ? (r / 1e6).toFixed(r >= 1e7 ? 0 : 1) + "M" : r >= 1e3 ? (r / 1e3).toFixed(r >= 1e4 ? 0 : 1) + "k" :
	Math.round(r)) + "/s";

function renderClock() {
	const el = $("vclock");
	if (!stepper) {
		el.textContent = "";
		return;
	}
	const t = CLOCK.now();
	const behind = behindRate();
	el.textContent = (realSpeed === null ? "speed: measuring" : behind !== null ? "speed " + behind + " (set " +
		$("dbg_speed").selectedOptions[0].text + ": too fast for this device)" : "speed " + fmtRate(realSpeed)) + " \u00b7 " + (CLOCK.virtual ? "virtual " : "real ") + "time " + (t / 1000).toFixed(4) +
		" s \u00b7 IRQ0 \u00d7" + num(stepper.irq0) + (stepper.idleMs ? " \u00b7 idle " + (stepper.idleMs / 1000).toFixed(3) + " s" : "");
	el.dataset.speed = realSpeed === null ? "" : Math.round(realSpeed);
	el.title = "Instructions per second actually executed (v86's instruction counter: JIT and stepping), over the last " +
		(speedWindowMs() / 1000) + " s since the last Boot, Pause, Continue or speed change (\"measuring\" for the first half of that); " +
		"the speed menu sets the target, which JavaScript stepping may not reach.";
	el.dataset.irq0 = stepper.irq0;
	el.dataset.ms = t;
	el.dataset.virtual = CLOCK.virtual;
	renderGuestInputs();
}

// The guest's date and random numbers (clock.js GUEST): from the start
// date and the seed on the virtual clock, the browser's on real time.
function renderGuestInputs() {
	const el = $("guest_in_text");
	if (!stepper) {
		el.textContent = "";
		return;
	}
	const iso = (ms) => new Date(ms).toISOString().replace("T", " ").replace(/\.\d+Z$/, " UTC");
	el.textContent = GUEST.why + (CLOCK.virtual ?
		"RTC " + iso(GUEST.dateNow()) + " (from " + iso(GUEST.rtcStart) + ") \u00b7 RDRAND seed " + GUEST.seed :
		"RTC and RDRAND: the browser's date and random numbers (real time)");
}

// At the kernel's 32-bit entry (or past it): from here on at the chosen speed.
function finishEntry() {
	entryPhase = false;
	speedReset(); // the fast part before the entry is not this speed
	const n = stepper.sinceEntry >= 0 ? stepper.sinceEntry : stepper.steps;
	const ff = ffInfo && ffInfo.stopPhys !== undefined && !ffInfo.runTo;
	const at = ff ? ffInfo.target : V86_KERNEL_ADDR;
	$("entry_info").textContent = "Entered the kernel at " + hex(at) + (ff ? " (" + ffInfo.name + ")" : "") + " after " +
		num(n) + " instructions from reset" +
		(ff ? " (fast-forwarded with the JIT in " + (ffInfo.ms / 1000).toFixed(1) + " s: BIOS, setup, decompressor; counted from here)" : "") +
		"; now " + $("dbg_speed").selectedOptions[0].text + ".";
	$("entry_info").dataset.ffMs = ff ? Math.round(ffInfo.ms) : "";
	$("entry_info").dataset.ffTarget = ff ? ffInfo.target : "";
	$("entry_info").dataset.steps = n;
	updateDebugUi();
}

// Time to "Still alive" at the current speed, from the count this browser
// last measured for this kernel or a built-in estimate.
function eta() {
	if (!image || !stepper || stepper.sinceEntry < 0)
		return "";
	let ref = null, measured = false;
	try {
		ref = Number(localStorage.getItem(REF_KEY + image.name + "-" + (clockMips() || "real"))) || null;
		measured = !!ref;
	} catch (e) {
		ref = null;
	}
	const known = REF_STEPS[image.name];
	ref = ref || (known && (known.any || known[clockMips() || "real"]));
	if (!ref)
		return "No estimate of how long this kernel needs. ";
	const left = Math.max(0, ref - (stepper.steps - stepper.sinceEntry));
	const sec = Math.round(left / Number(speed()));
	const t = sec >= 3600 ? Math.floor(sec / 3600) + " h " + Math.floor(sec % 3600 / 60) + " min" :
		sec >= 60 ? Math.floor(sec / 60) + " min " + (sec % 60) + " s" : sec + " s";
	$("count_info").dataset.eta = sec;
	return "\"Still alive\" in about " + t + " at this speed (" + (measured ? "this browser measured " : "estimate: ") +
		"~" + num(ref) + " instructions from the entry). ";
}

// What the end of the kernel looked like, for the banner and the status.
function endText() {
	const e = stepper && stepper.end;
	if (!e)
		return "";
	// Nothing was stepped (full JIT speed, count off): no count to report.
	const after = !e.steps ? "" : " after " + num(e.sinceEntry !== null ? e.sinceEntry : e.steps) + " instructions from the kernel entry";
	if (e.kind === "panic-budget")
		return "panic() entered, no end-of-panic text after " + num(stepper.panicBudget) + " instructions" + after + ". Stopped.";
	if (e.kind === "panic-text")
		return "Kernel panic: " + e.message + (e.exact ? " (stopped where its last line was written)" : " (stopped by the page)") + after + ".";
	if (e.kind === "panic")
		return "Kernel panic" + (stepper.exit ? " (init exited with code " + stepper.exit.code + ")" : "") + after +
			". Machine stopped here.";
	return "Halted: hlt with interrupts off" + after + ". The machine has stopped.";
}

// version.json, written by the Pages workflow: which commit is deployed.
async function loadVersion() {
	const el = $("build_info");
	let v = null;
	try {
		const resp = await fetch(withV("version.json"));
		v = resp.ok ? await resp.json() : null;
	} catch (e) {
		v = null;
	}
	if (!v || !v.sha) {
		el.textContent = "local build";
		return;
	}
	const repo = "https://github.com/" + (v.repo || "d33tah/minimal-kernel-build");
	const a = document.createElement("a");
	a.href = repo + "/commit/" + v.sha;
	a.textContent = "build " + v.sha.slice(0, 12);
	const date = new Date(v.date);
	const when = isNaN(date) ? v.date : date.toISOString().slice(0, 16).replace("T", " ") + " UTC";
	const run = document.createElement("a");
	run.href = repo + "/actions/runs/" + v.run_id;
	run.textContent = "run #" + v.run_number;
	el.textContent = "";
	el.append(a, " \u00b7 " + when + " \u00b7 ", run);
	el.dataset.sha = v.sha;
	$("build_subject").textContent = "Deployed: " + v.subject + " (" + v.sha.slice(0, 12) + ", " + when + ").";
}

// Distro kernels end with "---[ end Kernel panic - not syncing: ... ]---"
// and then spin; Debian's initramfs shell prompt pauses the machine once.
// The stepper's VGA hook sees them written (Stepper.textSeen), and the
// page's own loops stop exactly there (Stepper.run, jitExactStop, J3).
// v86's own loop (the "real time" clock at full speed) cannot be stopped
// inside: check 4 times a second and stop there, where it has got to.
function watchPanicText() {
	if (!stepper || stepper.end || runState !== "jit" || jitLoopId || ffActive)
		return;
	const st = stepper;
	if ((st.textSeen & TEXT_PROMPT) && !st.prompt) {
		st.prompt = { exact: false, ...st.snapshot() };
		counting = false;
		// v86 redraws the screen only while the VM runs: let it show the
		// prompt before pausing.
		setTimeout(() => {
			if (stepper === st && runState === "jit")
				pause("at the (initramfs) prompt");
		}, 300);
		return;
	}
	if (!(st.textSeen & TEXT_END))
		return;
	st.end = { kind: "panic-text", message: st.panicText(), exact: false, ...st.snapshot() };
	counting = false;
	pause(endText());
}

// At full speed (v86's JIT) the kernel's end, hlt with interrupts off,
// left the page "running (full speed, JIT)" at 0 instructions per second
// (also Continue after the end): pause there with the end, as stepping does.
function watchJitEnd() {
	if (!stepper || runState !== "jit" || ffActive || !stepper.dead())
		return;
	stepper.markHalt();
	counting = false;
	pause(endText());
}

function renderEnd() {
	const b = $("end_banner");
	const e = stepper && stepper.end;
	b.hidden = !e;
	b.textContent = e ? endText() : "";
	if (b.dataset.kind !== (e ? e.kind : "") && typeof G === "object")
		G.dirty = true; // the timeline's "now" marker becomes "end"
	b.dataset.kind = e ? e.kind : "";
	b.dataset.code = stepper && stepper.exit ? stepper.exit.code : "";
}

// Slow motion measured below half the set speed: the rate reached (one
// decimal under 10/s, where "0/s" read as a stall), else null.
function behindRate() {
	const set = Number(speed());
	if (runState !== "stepped" || entryPhase || fastStepping() || realSpeed === null || !(set > 0) || realSpeed >= set / 2)
		return null;
	return realSpeed < 10 ? realSpeed.toFixed(1) + "/s" : fmtRate(realSpeed);
}

// The state line's " (reaching 0.3/s: too fast for this device)".
function behindText() {
	const r = behindRate();
	return r === null ? "" : " (reaching " + r + ": too fast for this device)";
}

function updateDebugUi() {
	srcFocusKeep(drawDebugUi);
}

// v86 draws the screen's changed text rows in a requestAnimationFrame loop
// that draws nothing once the VM has stopped, so rows written in the last
// frame before a stop (the panic's last line) and rows the stepper writes
// while paused stayed blank: draw them whenever the Debugger is drawn.
function flushScreen() {
	const sa = emulator && emulator.screen_adapter, vga = emulator && emulator.v86 && emulator.v86.cpu.devices.vga;
	if (sa && typeof sa.update_text === "function" && vga && !vga.graphical_mode)
		sa.update_text();
}

// H1: the menu toggle (#runbadge, bottom right on every tab). Its face says
// whether the VM runs and how fast (a pulsing dot and the speed), else
// paused, halted, panic or off; its menu says the same in words with the
// instruction count. Drawn with the Debugger (drawDebugUi), so it costs
// nothing per instruction.
function runBadgeState() {
	if (!stepper || runState === "off")
		return "off";
	if (ffActive)
		return "ff";
	if (runState === "stepped" || runState === "jit")
		return "run";
	const e = stepper.end;
	return !e ? "paused" : e.kind.startsWith("panic") ? "panic" : "halted";
}

function renderRunBadge() {
	const box = $("runbadge"), tog = $("runbadge_toggle"), state = runBadgeState();
	// The speed's short text: "1k/s", "full" (the select's "full (JIT)").
	const speed = $("dbg_speed").selectedOptions[0].text.replace(/ \(JIT\)$/, "");
	const face = state === "run" ? speed : state === "ff" ? "JIT" : state;
	const words = { off: "no machine", ff: "fast-forwarding with the JIT", paused: "paused", halted: "halted (the end of the run)",
		panic: "kernel panic (the end of the run)",
		run: (runState === "jit" ? "running at full speed (JIT)" : (entryPhase ? "stepping to the kernel entry, then " + speed : "running at " + speed)) +
			behindText() }[state];
	const newer = typeof PWA !== "undefined" && PWA.newer ? PWA.newer : "";
	const text = words + (stepper && runState !== "off" ? " \u00b7 " + num(stepper.steps) + " instructions" : "");
	box.dataset.state = state;
	box.dataset.vm = ffActive ? "ff" : runState;
	box.dataset.steps = stepper ? stepper.steps : 0;
	box.dataset.newer = newer;
	tog.dataset.state = state;
	if ($("runbadge_speed").textContent !== face)
		$("runbadge_speed").textContent = face;
	if ($("runbadge_state").textContent !== text)
		$("runbadge_state").textContent = text;
	$("runbadge_toggle").querySelector(".rbnews").hidden = !newer;
	const label = "Menu: " + words + (newer ? ", a new version is deployed" : "") + ". Back, Forward, Pause";
	if (tog.title !== label) {
		tog.title = label;
		tog.setAttribute("aria-label", label);
	}
	const pause = $("dbg_pause"), cont = $("dbg_continue"), b = $("runbadge_pause");
	const canPause = !pause.disabled, ended = !!(stepper && stepper.end);
	// At the end of the run there is nothing to continue (the overlay's Continue goes too).
	b.disabled = canPause ? false : cont.disabled || ended;
	b.textContent = canPause ? "Pause" : "Continue";
	b.title = canPause ? "Stop the CPU (the Machine tab's Pause)" : "Run again at the selected speed (the Machine tab's Continue)";
}

// The "new version" notice (pwa.js) in the menu too: the Machine tab's copy
// is out of sight on the other tabs.
function renderRunBadgePwa() {
	const el = $("runbadge_pwa"), newer = typeof PWA !== "undefined" && PWA.newer;
	el.hidden = !newer;
	el.textContent = "";
	if (newer) {
		const b = document.createElement("button");
		b.type = "button";
		b.textContent = "Reload";
		b.addEventListener("click", pwaReload);
		el.append("A new version of this page is deployed (" + newer + "). ", b);
	}
	renderRunBadge();
}

function runBadgeOpen(open, focus = false) {
	const tog = $("runbadge_toggle");
	$("runbadge_menu").hidden = !open;
	tog.setAttribute("aria-expanded", open ? "true" : "false");
	bmMenuFit();
	if (focus)
		tog.focus();
}

function initRunBadge() {
	const tog = $("runbadge_toggle");
	tog.addEventListener("click", () => runBadgeOpen($("runbadge_menu").hidden));
	$("runbadge_state").addEventListener("click", () => {
		runBadgeOpen(false);
		showTab("machine");
	});
	$("runbadge_pause").addEventListener("click", () => {
		const b = $("dbg_pause").disabled ? $("dbg_continue") : $("dbg_pause");
		if (!b.disabled)
			b.click();
	});
	// Esc closes the menu (the focus back on the toggle); so does a press
	// anywhere outside it.
	document.addEventListener("keydown", (ev) => {
		if (ev.key === "Escape" && !$("runbadge_menu").hidden && !vmHasKeys) {
			ev.preventDefault();
			ev.stopPropagation();
			runBadgeOpen(false, true);
		}
	}, true);
	document.addEventListener("pointerdown", (ev) => {
		if (!$("runbadge_menu").hidden && !(ev.target.closest && ev.target.closest("#runbadge")))
			runBadgeOpen(false);
	}, true);
	renderRunBadge();
}

function drawDebugUi() {
	if (runState !== "jit")
		flushScreen();
	renderEnd();
	renderClock();
	const st = $("dbg_state");
	// Running: the instruction just executed. Paused: the one at EIP, which
	// the rest of the Debugger shows (at a function's first instruction the
	// last one executed is the caller's call).
	const phys = !stepper ? -1 : runState === "paused" ? stepper.nextPhys() : stepper.lastPhys;
	const sym = !stepper ? -1 : runState === "paused" ? (phys >= 0 ? stepper.symbolOf(phys) : -1) : stepper.curSym;
	const where = sym >= 0 ? ", in " + stepper.symName[sym] : "";
	let loc = null;
	if (where && dbgHas() && runState !== "jit") {
		// file:line only: the text would fetch every file stepped through.
		const r = dbgFrames(phys);
		if (r.state === "ok" && r.frames[0].file && r.frames[0].line)
			loc = r.frames[0];
	}
	// The text is cut with an ellipsis, the file:line link after it stays
	// whole (style.css #dbg_state).
	const text = document.createElement("span");
	text.className = "sttext";
	text.textContent = ffActive && ffInfo ? (ffInfo.runTo !== undefined ? "running to " + ffInfo.runTo : "fast-forwarding to " + ffInfo.name) +
		" with the JIT: " + num(stepper.steps - (ffInfo.start || 0)) + " instructions" : { off: "", jit: "running (full speed, JIT)",
		stepped: (entryPhase ? "stepping to the kernel entry" : fastStepping() ? (counting ? "stepping (counting)" : "stepping") :
			"slow motion " + $("dbg_speed").selectedOptions[0].text + behindText()) + where,
		paused: "paused" + where }[runState];
	st.replaceChildren(text);
	if (loc && !ffActive && (runState === "stepped" || runState === "paused")) {
		const at = document.createElement("span");
		at.className = "stloc";
		at.append("\u00a0(", srcLinkEl(loc.file, loc.line, "", phys), ")");
		st.append(at);
	}
	st.dataset.state = ffActive ? "ff" : runState;
	st.title = st.textContent;
	const can = !!stepper;
	$("dbg_pause").disabled = !can || (!ffActive && (runState === "paused" || runState === "off"));
	$("dbg_continue").disabled = !can || ffActive || runState !== "paused";
	// "VM paused" over the screen, with why (the debugger's last note, or
	// the end of the run) and Continue unless the run has ended.
	const paused = can && !ffActive && runState === "paused";
	$("vm_overlay").hidden = !paused;
	if (paused) {
		// The note keeps its file:line links (a copy of #dbg_status).
		const end = stepper.end ? "end: " + endText() : "", status = $("dbg_status"), note = status.textContent;
		if (end && !note.startsWith("Run to ") || !note)
			$("vm_reason").textContent = end;
		else
			$("vm_reason").replaceChildren(...[...status.childNodes].map((n) => n.cloneNode(true)));
		$("vm_continue").hidden = !!stepper.end;
	}
	for (const id of ["dbg_step", "dbg_stepn", "dbg_runto"])
		$(id).disabled = !can || runState === "off";
	renderRunBadge();
	renderRegs();
	renderCounters();
	renderMemEip();
	if (runState !== "stepped")
		renderContext();
}

// Pause, and wait for a fast-forward or a fast Run to that is still running
// (they are paused too) to return: a second one started next to it ran two
// loops on one machine (Run to start_kernel during the boot's
// fast-forward went on to the end of the run).
async function pauseAll() {
	if (runState !== "paused" || ffActive)
		await pause();
	while (ffActive)
		await ffYield();
}

async function doStep(n) {
	if (!stepper)
		return;
	await pauseAll();
	if (!stepper)
		return;
	if (stepper.dead()) {
		$("dbg_status").textContent = "The CPU executed hlt with interrupts off: nothing to step.";
		return;
	}
	const before = stepper.steps;
	const reason = stepper.run(n, -1, 5000);
	$("dbg_status").textContent = "Stepped " + num(stepper.steps - before) + " instruction" +
		(stepper.steps - before === 1 ? "" : "s") + (reason === "hlt" ? " (halted)" :
			reason === "milestone" ? " (a line appeared on the screen)" : reason === "end" ? " (" + endText() + ")" : reason === "prompt" ? " (at the (initramfs) prompt)" : reason === "time" ? " (5 s limit)" : "") + ".";
	scan(false);
	afterStop();
}

// Hex address: physical, or a kernel virtual address (above the kernel's
// virtual-physical offset from symbols.json, 0xC0000000 for Linux).
function parseAddr(text) {
	const v = parseInt(String(text).trim().replace(/^0x/i, ""), 16);
	if (!Number.isFinite(v) || v < 0 || v > 0xffffffff)
		return -1;
	const off = symbols && symbols.symbols.length ? symbols.symbols[0].vaddr - symbols.symbols[0].paddr : 0;
	return off > 0 && v >= off ? v - off : v;
}

// "Stopped at 0x… (Run to …)" for a stop at one of n targets.
function runToStopped(label, n, how) {
	const pa = stepper.nextPhys(), r = dbgFrames(pa);
	const f = r.state === "ok" && r.frames[0].file && r.frames[0].line ? r.frames[0] : null;
	return ["Stopped at " + hex(pa), ...(f ? [" ", srcLinkEl(f.file, f.line, "", pa)] : []), " (Run to " + label +
		(n > 1 ? ", the first of its " + n + " breakpoint addresses reached" : "") + ": " + how + ")."];
}

// The kernel section holding phys (symbols.json), or null.
const sectionAt = (phys) => (symbols && symbols.sections.find((s) => phys >= s.paddr && phys < s.paddr + s.size)) || null;

// Why the CPU can never stop at phys, or "": a target the kernel never
// executes makes Run to run on to the end. Kernel data, and the middle
// of an instruction of the build's disassembly.
function notCode(phys) {
	const sec = sectionAt(phys);
	if (sec && sec.class !== "text")
		return hex(phys) + " is in " + sec.name + " (" + (sec.class === "rodata" ? "read-only data" : sec.class) +
			"), not code: the CPU never executes it, so Run to would not stop";
	if (!sec || !disasm || !disasm.insns || !disasm.insns.length)
		return "";
	const i = insnIndex(phys), ins = disasm.insns[i];
	if (ins && ins[0] === phys)
		return "";
	const prev = disasm.insns[i - 1];
	if (prev && phys < prev[0] + prev[1].length / 2) {
		const name = fnSymAt(prev[0]);
		return hex(phys) + " is inside the instruction at " + hex(prev[0]) + (name ? " (" + name + ")" : "") +
			", not at its start: the CPU never fetches an instruction there";
	}
	return "";
}

// A symbol name: the addresses of the code symbols with that name (a
// static function can have several), or { why } naming similar ones.
function symbolTargets(text) {
	const list = symbols ? symbols.symbols : [];
	const exact = list.filter((s) => s.name === text);
	if (!exact.length) {
		const low = text.toLowerCase();
		const like = [...new Set(list.filter((s) => s.name.toLowerCase().includes(low)).map((s) => s.name))]
			.sort((a, b) => (b.toLowerCase().startsWith(low) - a.toLowerCase().startsWith(low)) || a.length - b.length);
		return { why: "Run to " + text + ": no symbol of that name" + (!symbols ? " (this kernel has no symbols)" : "") +
			(like.length ? "; similar: " + like.slice(0, 5).join(", ") + (like.length > 5 ? " and " + (like.length - 5) + " more" : "") : "") +
			". Run to takes a function name, a hex address or file:line." };
	}
	const code = exact.filter((s) => !notCode(s.paddr));
	if (!code.length)
		return { why: "Run to " + text + ": " + notCode(exact[0].paddr) };
	return { targets: [...new Set(code.map((s) => s.paddr))], label: text };
}

// What the Run to field names: { targets: [physical], label } or { why }.
// file:line (the breakpoint addresses of that line's code: dbgLineCode),
// a symbol name (a word that is not hex, or any exact symbol name not
// starting with 0x), or a hex address (physical or kernel virtual). A
// target that is not code is refused: Run to would run to the end.
async function runToTargets(text) {
	const m = /^(.*\D.*):(\d+)$/.exec(text);
	if (m) {
		$("dbg_status").textContent = "Finding the code of " + text + "…";
		const r = await dbgLineCodeWait(m[1], Number(m[2]));
		if (r.state !== "ok")
			return { why: "Run to " + text + ": " + r.why };
		return r.bps.length ? { targets: r.bps, label: r.path + ":" + r.line } : { why: "Run to " + text + ": no breakpoint address" };
	}
	const isHex = /^(0x)?[0-9a-f]+$/i.test(text);
	if (/^[A-Za-z_.$][\w.$]*$/.test(text) && (!isHex || (symbols && symbols.symbols.some((s) => s.name === text))))
		return symbolTargets(text);
	const phys = isHex ? parseAddr(text) : -1;
	if (phys < 0)
		return { why: "Run to " + text + ": not a function name, hex address or file:line" };
	const why = notCode(phys);
	return why ? { why: "Run to " + why + "." } : { targets: [phys], label: hex(phys) };
}

// "Run to file:line" from the Source panel: the Debugger's own Run to.
function runToLine(where) {
	$("dbg_addr").value = where;
	navJump(() => showTab("debugger"));
	doRunTo();
}

async function doRunTo() {
	if (!stepper)
		return;
	const want = await runToTargets($("dbg_addr").value.trim());
	if (!want.targets) {
		$("dbg_status").textContent = want.why;
		updateDebugUi(); // the paused screen's overlay says it too
		return;
	}
	const { targets, label } = want;
	const at = new Set(targets);
	await pauseAll();
	if (!stepper)
		return;
	// Run to from a target goes on to its next hit, stepped or with the JIT.
	runToFrom = stepper.nextPhys();
	runToMoved = false;
	// Fast run-to (JIT + breakpoints) for kernel code: first to the
	// kernel's entry when it has not run yet (its code is not in memory
	// before the decompressor), then to the targets.
	const t = ffEnabled() && ffTarget();
	// Kernel code in another section than the entry's (.init.text: e.g.
	// start_kernel) gets no breakpoint (that memory is freed and reused
	// after boot), but BIOS, setup and the decompressor still run on the
	// JIT: stepping them took minutes for the Debian sample.
	const inText = (a) => !!(symbols && symbols.sections.some((s) => s.class === "text" && a >= s.paddr && a < s.paddr + s.size));
	const sameSec = t && targets.every((a) => codeRange(t.phys)[0] <= a && a < codeRange(t.phys)[1]);
	if (t && (sameSec || targets.every((a) => inText(a) && a >= V86_KERNEL_ADDR))) {
		ffInfo = null;
		$("dbg_status").textContent = "Running to " + label + " with the JIT...";
		let ok = true;
		if (stepper.sinceEntry < 0 || stepper.nextPhys() < V86_KERNEL_ADDR)
			ok = await fastForward(t.phys, t.name);
		if (ok && entryPhase && stepper.sinceEntry >= 0)
			finishEntry(); // the entry phase ends here, not in driverTick
		if (ok && at.has(runToFrom) && stepper.nextPhys() === runToFrom)
			stepper.run(1, -1, 100);
		if (ok && sameSec && !ffAbort && !at.has(stepper.nextPhys()))
			ok = await fastRunTo(targets, label);
		if (ok && at.has(stepper.nextPhys())) {
			pause(runToStopped(label, targets.length, sameSec ? "JIT and " + (targets.length > 1 ? "breakpoints" : "a breakpoint") :
				"JIT to the kernel entry, then stepped"));
			return;
		}
		if (ffAbort) {
			afterStop();
			return;
		}
	}
	runTo = at;
	runToLabel = label;
	$("dbg_status").textContent = "Running to " + (targets.length > 1 ? label + " (" + targets.length + " addresses)" : "physical " + hex(targets[0])) + "...";
	resume();
}

function initDebugger() {
	$("dbg_pause").addEventListener("click", () => pause("Paused."));
	$("dbg_continue").addEventListener("click", () => { $("dbg_status").textContent = ""; resume(true); });
	$("vm_continue").addEventListener("click", () => $("dbg_continue").click());
	$("dbg_step").addEventListener("click", () => doStep(1));
	$("dbg_stepn").addEventListener("click", () => {
		const n = Math.max(1, Math.floor(Number($("dbg_n").value) || 100));
		doStep(n);
	});
	$("dbg_runto").addEventListener("click", doRunTo);
	$("context").addEventListener("click", (ev) => {
		const line = ev.target.closest("[data-addr]");
		if (line)
			jumpTo(Number(line.dataset.addr));
	});
	$("dbg_addr").addEventListener("keydown", (e) => { if (e.key === "Enter") doRunTo(); });
	$("dbg_n").addEventListener("keydown", (e) => { if (e.key === "Enter") $("dbg_stepn").click(); });
	$("dbg_speed").addEventListener("change", () => {
		prefSpeed = speed();
		try {
			localStorage.setItem(SPEED_KEY, prefSpeed);
		} catch (e) {
			// storage unavailable: the choice lasts until reload
		}
		if (runState === "stepped" || runState === "jit")
			resume(true);
	});
	$("clock_mips").addEventListener("change", () => {
		try {
			localStorage.setItem(MIPS_KEY, $("clock_mips").value);
		} catch (e) {
			// no storage: until reload
		}
		if (runState === "jit") {
			resume(true); // the JIT from the page (virtual) or v86's own loop (real)
		} else {
			CLOCK.set(!!clockMips());
			if (clockMips())
				CLOCK.mips = clockMips();
		}
		renderClock();
	});
	$("count").addEventListener("change", () => {
		try {
			localStorage.setItem(COUNT_KEY, $("count").checked ? "1" : "0");
		} catch (e) {
			// no storage: until reload
		}
		if (!$("count").checked && counting) {
			counting = false;
			countNote = "Counting switched off.";
			if (runState === "stepped")
				resume();
		}
		renderCounters();
	});
}

function reportError(err) {
	setStatus("Emulator error: " + err + (image ? " (" + image.name + ", " + kib(ram()) + " RAM)" : ""));
}
window.addEventListener("error", (e) => {
	// Benign: a resize re-render changed the layout within the same frame.
	if (!/ResizeObserver loop/.test(e.message))
		reportError(e.message);
});
window.addEventListener("unhandledrejection", (e) => reportError(e.reason));

// Every kernel starts with 2 MB; the header check says when that is too little.
function useImage(name, buffer, memFromUrl, syms) {
	const bytes = new Uint8Array(buffer);
	image = { name, bytes, hdr: parseHeader(bytes) };
	symbols = syms || null;
	$("memory").value = memFromUrl || String(DEFAULT_RAM);
	// A kernel that needs more than the default (a distro kernel decompresses
	// to 16 MiB and beyond) gets the smallest listed size that fits it.
	if (!memFromUrl) {
		const need = kernelEnd(image.hdr);
		const opts = Array.from($("memory").options, (o) => Number(o.value)).sort((a, b) => a - b);
		const fit = opts.find((v) => v >= need);
		if (need > DEFAULT_RAM)
			$("memory").value = String(fit || opts[opts.length - 1]);
	}
	ramForInitrd(); // keeps a loaded initrd: v86 needs RAM above 64 MiB + its size
	initrdNote();
	showHeader();
	drawMaps();
	renderSnapshots(); // which snapshots are for this kernel
}

// symbols.json is optional: without it the grid shows regions only.
async function loadSymbols(entry) {
	if (!entry.symbols)
		return null;
	try {
		const resp = await fetch(withV(entry.symbols));
		if (!resp.ok)
			return null;
		const s = await resp.json();
		s.symbols.sort((a, b) => a.paddr - b.paddr || a.name.localeCompare(b.name));
		return s;
	} catch (e) {
		console.warn("symbols:", e);
		return null;
	}
}

async function loadDisasm(entry) {
	if (!entry.disasm)
		return null;
	try {
		const resp = await fetch(withV(entry.disasm));
		return resp.ok ? await resp.json() : null;
	} catch (e) {
		console.warn("disasm:", e);
		return null;
	}
}

// Why a fetch failed: sw.js answers 503 with X-MKB-Offline for a file it
// does not have while offline.
function httpWhy(resp) {
	return resp.headers.get("X-MKB-Offline") ? "not available offline yet (open it once while online)" : "HTTP " + resp.status;
}

async function loadKernel(entry, memFromUrl) {
	setStatus("Fetching " + entry.path + "...");
	const resp = await fetch(withV(entry.path));
	if (!resp.ok)
		throw new Error(entry.path + ": " + httpWhy(resp));
	disasm = await loadDisasm(entry);
	await srcUseShipped(entry);
	dbgUseShards(entry);
	syncSrcToggle();
	// kernels.json may give a kernel its own RAM size (meta.json "ram").
	const ramDefault = entry.ram && [...$("memory").options].some((o) => o.value === String(entry.ram)) ? String(entry.ram) : null;
	// kernels.json "initrd": the initrd that comes with this entry. One the
	// user loaded stays; one from another entry goes.
	if (entry.initrd && (!initrd || initrd.entry)) {
		setStatus("Fetching " + entry.initrd + "...");
		const r = await fetch(withV(entry.initrd));
		if (!r.ok)
			throw new Error(entry.initrd + ": " + httpWhy(r));
		const name = entry.initrd.split("/").pop();
		initrd = { name, bytes: new Uint8Array(await r.arrayBuffer()), entry: entry.name };
		initrd.info = await analyzeInitrd(initrd.bytes, name);
	} else if (!entry.initrd && initrd && initrd.entry)
		initrd = null;
	useImage(entry.name, await resp.arrayBuffer(), memFromUrl || ramDefault, await loadSymbols(entry));
	showImported();
	await restoreImported();
	await restoreCmdline();
	await boot();
}

// ---- popovers --------------------------------------------------------------

// Title attributes never show on touch screens, so explanations are in
// data-tip and one popover (#tip_pop) shows them. Two kinds:
// - marks (the "?" markers, .tip, and in the listings .tipmark: ⊘, the
//   EIP mark, the gutter stubs, the tint key): a click, tap, Enter or
//   Space toggles the popover, hover shows it on devices that hover;
// - other elements with data-tip (buttons that go somewhere or open a
//   list on a tap: ↑ ↓ ↗, ←n, file:line): hover, keyboard focus and a
//   long press show it, and a tap still acts.
// A tap elsewhere or Esc closes it. The listings are drawn again while
// the CPU steps: the listeners are delegated, and an open popover moves
// to the same mark in the new drawing (tipReanchor).
let tipOpen = null;    // element whose popover is shown
let tipPinned = false; // opened by click/key/long press (stays until closed)
let tipKey = "";       // tipOpen's identity across drawings
let tipLong = null;    // { el, at }: shown by a long press, swallow the click that may follow
const TIP_MARK = ".tip, .tipmark";
const TIP_LONG_MS = 500;

function placeTip(el) {
	const pop = $("tip_pop");
	pop.textContent = el.dataset.tip;
	pop.hidden = false;
	const r = el.getBoundingClientRect();
	const w = Math.min(320, window.innerWidth - 16);
	pop.style.width = w + "px";
	let x = Math.min(Math.max(8, r.left + r.width / 2 - w / 2), window.innerWidth - w - 8);
	let y = r.bottom + 6;
	pop.style.left = x + "px";
	pop.style.top = y + "px";
	const h = pop.offsetHeight;
	if (y + h > window.innerHeight - 8 && r.top - h - 6 > 8)
		pop.style.top = (r.top - h - 6) + "px";
}

// What finds el again after its box is drawn anew: the box, its class and
// the address data it carries.
function tipKeyOf(el) {
	const box = el.closest("[id]");
	const d = el.dataset;
	return [box ? box.id : "", el.getAttribute("class"), d.addr, d.pa, d.go, d.dir, d.way, d.file, d.line].join("|");
}

function showTip(el, pinned) {
	if (!el.dataset.tip)
		return;
	if (tipOpen && tipOpen !== el)
		tipOpen.setAttribute("aria-expanded", "false");
	tipOpen = el;
	tipPinned = pinned;
	tipKey = tipKeyOf(el);
	el.setAttribute("aria-expanded", "true");
	placeTip(el);
}

function hideTip() {
	if (tipOpen)
		tipOpen.setAttribute("aria-expanded", "false");
	tipOpen = null;
	tipPinned = false;
	tipKey = "";
	$("tip_pop").hidden = true;
}

function toggleTip(el) {
	if (tipOpen === el && tipPinned)
		hideTip();
	else
		showTip(el, true);
}

// A box with an open popover's mark was drawn again: show it at the same
// mark in the new drawing (with its new text), or close it.
function tipReanchor() {
	if (!tipOpen || tipOpen.isConnected)
		return;
	const box = $(tipKey.split("|")[0]);
	const el = box ? [...box.querySelectorAll("[data-tip]")].find((e) => tipKeyOf(e) === tipKey) : null;
	if (!el) {
		hideTip();
		return;
	}
	tipOpen = el;
	el.setAttribute("aria-expanded", "true");
	placeTip(el);
}

// The element under ev whose popover a tap toggles: a mark, unless a
// button inside it was hit.
function tipMarkOf(t) {
	const mark = t.closest ? t.closest(TIP_MARK) : null;
	if (!mark)
		return null;
	const act = t.closest("button, a, input, select");
	return act && act !== mark && mark.contains(act) ? null : mark;
}

function initTips() {
	for (const el of document.querySelectorAll(".tip")) {
		if (el.title) {
			el.dataset.tip = el.title;
			el.removeAttribute("title");
		}
		el.setAttribute("aria-expanded", "false");
		el.setAttribute("aria-label", "Explain");
		el.setAttribute("aria-controls", "tip_pop");
	}
	// Before the listings' own handlers (capture): a tap on a mark only
	// toggles its popover (inside a label it must not activate the control).
	document.addEventListener("click", (ev) => {
		const t = ev.target;
		if (tipLong) {
			const long = tipLong;
			tipLong = null;
			const el = t.closest ? t.closest("[data-tip]") : null;
			if (el && (el === long.el || tipKeyOf(el) === long.key) && realNow() - long.at < 1500) {
				ev.preventDefault();
				ev.stopImmediatePropagation(); // jumps.js and source.js listen on document too
				return;
			}
		}
		const mark = tipMarkOf(t);
		if (mark) {
			ev.preventDefault();
			ev.stopImmediatePropagation(); // jumps.js and source.js listen on document too
			toggleTip(mark);
			return;
		}
		if (tipOpen && !$("tip_pop").contains(t))
			hideTip();
	}, true);
	// v86 takes every key on the window: Enter and Space on a mark here first.
	document.addEventListener("keydown", (ev) => {
		const mark = ev.key === "Enter" || ev.key === " " ? tipMarkOf(ev.target) : null;
		if (mark) {
			ev.preventDefault();
			ev.stopImmediatePropagation(); // jumps.js and source.js listen on document too
			toggleTip(mark);
		} else if (ev.key === "Escape" && tipOpen)
			hideTip();
	}, true);
	// Hover: a mouse on a device that hovers (a phone sends pointerover on
	// a tap, and mouse ones when the page changes under the last tap).
	const hover = window.matchMedia("(hover: hover)");
	document.addEventListener("pointerover", (ev) => {
		if (ev.pointerType !== "mouse" || !hover.matches || tipPinned)
			return;
		const el = ev.target.closest ? ev.target.closest("[data-tip]") : null;
		if (el && el !== tipOpen)
			showTip(el, false);
		else if (!el && tipOpen) // its mark may have been drawn again under the pointer
			hideTip();
	});
	document.addEventListener("pointerout", (ev) => {
		if (ev.pointerType !== "mouse" || !hover.matches || tipPinned || !tipOpen)
			return;
		const el = ev.target.closest ? ev.target.closest("[data-tip]") : null;
		if (el === tipOpen && !(ev.relatedTarget && el.contains(ev.relatedTarget)))
			hideTip();
	});
	// Keyboard focus (Tab) on a button with an explanation shows it; focus
	// moved by the page after a tap or click does not.
	let byKey = false;
	document.addEventListener("keydown", (ev) => { byKey = ev.key === "Tab"; }, true);
	document.addEventListener("pointerdown", () => { byKey = false; }, true);
	document.addEventListener("focusin", (ev) => {
		const el = ev.target;
		if (!byKey || tipPinned || !el.dataset || !el.dataset.tip || el.matches(TIP_MARK))
			return;
		showTip(el, false);
	});
	document.addEventListener("focusout", (ev) => {
		if (!tipPinned && tipOpen === ev.target)
			hideTip();
	});
	// A long press on a button with an explanation shows it; the click
	// that may follow does nothing.
	let press = null;
	const unpress = () => {
		if (press)
			clearTimeout(press.timer);
		press = null;
	};
	document.addEventListener("pointerdown", (ev) => {
		unpress();
		if (ev.pointerType === "mouse")
			return;
		const el = ev.target.closest ? ev.target.closest("[data-tip]") : null;
		if (!el || el.matches(TIP_MARK))
			return;
		press = { x: ev.clientX, y: ev.clientY, timer: setTimeout(() => {
			press = null;
			showTip(el, true);
			tipLong = { el, key: tipKeyOf(el), at: realNow() };
		}, TIP_LONG_MS) };
	}, true);
	document.addEventListener("pointermove", (ev) => {
		if (press && Math.hypot(ev.clientX - press.x, ev.clientY - press.y) > 10)
			unpress();
	}, true);
	document.addEventListener("pointerup", unpress, true);
	document.addEventListener("pointercancel", unpress, true);
	document.addEventListener("contextmenu", (ev) => {
		if (tipLong && ev.target.closest && ev.target.closest("[data-tip]"))
			ev.preventDefault();
	});
	// The listings are drawn again while the CPU runs.
	const mo = new MutationObserver(tipReanchor);
	for (const id of ["listing", "context", "cov_info", "eip_jumps", "cur_src", "src_body"])
		if ($(id))
			mo.observe($(id), { childList: true, subtree: true });
	const follow = () => {
		if (!tipOpen)
			return;
		// Hidden when its mark leaves the window or its scrolling listing.
		const r = tipOpen.getBoundingClientRect();
		const box = tipOpen.closest("#listing, #src_body");
		const b = box ? box.getBoundingClientRect() : { top: 0, bottom: window.innerHeight };
		if (r.bottom < Math.max(0, b.top) || r.top > Math.min(window.innerHeight, b.bottom))
			hideTip();
		else
			placeTip(tipOpen);
	};
	window.addEventListener("scroll", follow, { passive: true, capture: true });
	window.addEventListener("resize", follow);
}

async function init() {
	const params = new URLSearchParams(location.search);
	// ?mem=BYTES may name any multiple of 4 KiB, for probing the minimum.
	let memParam = null;
	const m = Number(params.get("mem"));
	if (Number.isInteger(m) && m > 0 && m % PAGE === 0) {
		memParam = String(m);
		if (![...$("memory").options].some((o) => o.value === memParam))
			$("memory").add(new Option(kib(m), memParam));
	}
	startPaused = params.get("paused") === "1";
	const options = [...$("dbg_speed").options].map((o) => o.value);
	let stored = null;
	try {
		stored = localStorage.getItem(SPEED_KEY);
	} catch (e) {
		stored = null;
	}
	prefSpeed = [params.get("speed"), stored, "1000"].find((v) => options.includes(v));
	const clocks = [...$("clock_mips").options].map((o) => o.value);
	let storedMips = null;
	try {
		storedMips = localStorage.getItem(MIPS_KEY);
	} catch (e) {
		storedMips = null;
	}
	$("clock_mips").value = [params.get("mips"), storedMips, "10"].find((v) => clocks.includes(v));
	initTips();
	initLegend();
	drawMaps();
	initDebugger();
	initGraph();
	initImport();
	$("live_decode").addEventListener("change", () => { if (liveDecode()) loadIced(); inspect(); renderContext(); });
	initInitrd();
	setInterval(watchPanicText, 250);
	setInterval(watchJitEnd, 250);
	setInterval(sampleSpeed, SPEED_SAMPLE_MS);
	document.addEventListener("visibilitychange", onVisibility);
	document.addEventListener("resume", onVisibility);
	loadVersion();
	if (params.has("cmdline"))
		cmdlineFromUrl = params.get("cmdline");
	let storedCount = null;
	try {
		storedCount = localStorage.getItem(COUNT_KEY);
	} catch (e) {
		storedCount = null;
	}
	if (params.get("count") === "0" || (!params.has("count") && storedCount === "0"))
		$("count").checked = false;
	initSource();
	initJumps();
	initCoverage();
	setupVars();
	dbgOnChange(() => {
		syncSrcToggle();
		showImported();
		if (tab === "debugger")
			renderContext();
		if (tab === "page")
			listingStale();
		if (typeof renderGraphInfo === "function")
			renderGraphInfo();
	});
	updateDebugUi();
	initCompactTabs();
	initSettings();
	for (const t of TABS)
		$("tab_" + t).addEventListener("click", () => showTab(t));
	initBookmarks(); // before the menu: its keys in the dialog first
	initRunBadge();
	window.addEventListener("hashchange", readHash);
	readHash();
	initNav();
	$("page_prev").addEventListener("click", () => selectedPage > 0 && selectPage(selectedPage - 1));
	$("page_next").addEventListener("click", () => selectedPage >= 0 && selectedPage + 1 < gridRam() / PAGE && selectPage(selectedPage + 1));
	$("reread").addEventListener("click", inspect);
	$("memory").addEventListener("change", () => { showHeader(); drawMaps(); });
	$("boot").addEventListener("click", boot);
	$("scan").addEventListener("click", () => scan());
	$("autoscan").addEventListener("change", setAutoscan);
	$("live").addEventListener("change", setLive);
	// Bytes per row and columns follow the listing's width.
	let dumpWidth = 0;
	new ResizeObserver(() => {
		const w = $("listing").clientWidth;
		if (w !== dumpWidth) {
			dumpWidth = w;
			requestAnimationFrame(inspect);
		}
	}).observe($("listing"));
	initSnapshots();
	const canvas = $("pages");
	canvas.addEventListener("click", (ev) => {
		const p = pageAt(ev);
		if (p >= 0)
			selectPage(p);
	});
	canvas.addEventListener("mousemove", (ev) => showHover(pageAt(ev)));
	canvas.addEventListener("mouseleave", () => showHover(selectedPage));
	$("kernel").addEventListener("change", () => {
		const k = kernels.find((x) => x.name === $("kernel").value);
		if (k)
			loadKernel(k).catch((e) => setStatus(String(e)));
	});
	$("file").addEventListener("change", async () => {
		const f = $("file").files[0];
		if (!f)
			return;
		disasm = null; // a file from disk comes without disasm.json, C source or DWARF
		srcFresh(null, "no C source for a bzImage from disk");
		dbgFresh(null, "no debug info for a bzImage from disk: import its unstripped vmlinux for file:line");
		syncSrcToggle();
		useImage(f.name, await f.arrayBuffer());
		showImported();
		await restoreImported();
		await restoreCmdline();
		await boot();
	});

	try {
		const resp = await fetch(withV("kernels.json"));
		kernels = (await resp.json()).kernels;
	} catch (e) {
		setStatus("No kernels.json (" + e + "). Load a bzImage from disk.");
		return;
	}
	for (const k of kernels)
		$("kernel").add(new Option(k.label || k.name, k.name));
	const wanted = kernels.find((k) => k.name === params.get("kernel")) || kernels[0];
	if (!wanted) {
		setStatus("kernels.json lists no kernels. Load a bzImage from disk.");
		return;
	}
	$("kernel").value = wanted.name;
	await loadKernel(wanted, memParam);
}

init().catch((e) => setStatus(String(e)));
