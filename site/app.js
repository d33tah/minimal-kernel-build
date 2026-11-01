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
const MAX_SNAPSHOTS = 8;

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
let emulator = null;
let emuRam = 0;          // RAM size the running emulator was started with
let scanTimer = null;
let symbols = null;      // symbols.json of the current kernel, or null
let disasm = null;       // disasm.json of the current kernel, or null
let view = "hex";        // inspector view: "hex" or "asm"
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
let snapshots = [];
let snapCounter = 0;

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
	for (const [a, b, cls, label] of regions) {
		const lo = Math.max(a, start), hi = Math.min(b, end);
		if (hi <= lo)
			continue;
		const div = document.createElement("div");
		div.style.flex = (hi - lo) + " 0 0";
		div.style.background = "var(--" + cls + ")";
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
		all.push([need, r, "r-ext", "free"]);
		end = Math.max(r, need);
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
		text += usedPages[p] ? ", non-zero" : ", all zero";
	if (syms.length) {
		const names = syms.slice(0, 6).map((s) => s.name + " " + symRange(s)).join(", ");
		text += ": " + names + (syms.length > 6 ? " and " + (syms.length - 6) + " more" : "");
	} else if (symbols) {
		text += ": no symbols";
	}
	el.textContent = text;
}

// ---- page inspector -------------------------------------------------------

function readPage(p) {
	return emulator.read_memory(p * PAGE, PAGE).slice();
}

const asciiOf = (b) => (b >= 0x20 && b < 0x7f ? String.fromCharCode(b) : ".");
const esc = (c) => (c === "<" ? "&lt;" : c === ">" ? "&gt;" : c === "&" ? "&amp;" : c);

const narrow = () => window.matchMedia("(max-width: 640px)").matches;

// Characters of the inspector's monospace font that fit in one line.
let charWidth = 0, charFont = "";
function lineChars() {
	const dump = $("hexdump");
	const cs = getComputedStyle(dump);
	const font = cs.fontSize + " " + cs.fontFamily;
	if (!charWidth || charFont !== font) {
		const ctx = document.createElement("canvas").getContext("2d");
		ctx.font = font;
		charFont = font;
		charWidth = ctx.measureText("0123456789abcdef").width / 16 || 7;
	}
	const w = dump.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight);
	return Math.max(20, Math.floor(w / charWidth) - 1);
}

// Bytes per hex dump row: the most of 16, 8, 4 whose line fits.
function hexLayout() {
	const chars = lineChars();
	for (const per of [16, 8, 4]) {
		const addr = per === 16 ? 8 : 6;
		const len = addr + 1 + per * 3 - 1 + (per === 16 ? 1 : 0) + 2 + per + 1;
		if (len <= chars || per === 4)
			return { per, addr, spare: chars - len };
	}
}

const esc2 = (t) => t.replace(/[<>&"]/g, (c) => ({ "<": "&lt;", ">": "&gt;", "&": "&amp;", '"': "&quot;" })[c]);

// Hex dump of one page in the style of hexdump -C: runs of identical rows
// collapse into "*". Bytes that differ from `base` (if given) are marked and
// such rows never collapse; byte ranges of symbols are tinted, alternating,
// and the symbols that start in a row are named at its end.
function renderDump(p, bytes, base, how) {
	const lines = [];
	const { per, addr, spare } = hexLayout();
	const syms = symbolsInPage(p);
	const sized = syms.filter((s) => s.size > 0);
	const tint = new Int8Array(PAGE).fill(-1);
	sized.forEach((s, k) => {
		const a = Math.max(s.paddr, p * PAGE) - p * PAGE, b = Math.min(s.paddr + s.size, p * PAGE + PAGE) - p * PAGE;
		tint.fill(k % 2, a, b);
	});
	let nonzero = 0, changed = 0, collapsed = false, prevRow = null;
	for (let row = 0; row < PAGE; row += per) {
		const lo = p * PAGE + row;
		const starts = syms.filter((s) => s.paddr >= lo && s.paddr < lo + per);
		let hx = "", asc = "", anyDiff = false, open = -1;
		const cur = bytes.subarray(row, row + per);
		for (let i = 0; i < per; i++) {
			const b = bytes[row + i];
			if (b)
				nonzero++;
			const diff = base && base[row + i] !== b;
			if (diff) {
				changed++;
				anyDiff = true;
			}
			const t = tint[row + i];
			const sep = i === 8 && per === 16 ? "  " : i ? " " : "";
			if (t !== open) {
				if (open >= 0) { hx += "</span>"; asc += "</span>"; }
				hx += sep;
				if (t >= 0) { hx += '<span class="sym' + t + '">'; asc += '<span class="sym' + t + '">'; }
				open = t;
			} else {
				hx += sep;
			}
			const h = b.toString(16).padStart(2, "0");
			const a = esc(asciiOf(b));
			hx += diff ? "<mark>" + h + "</mark>" : h;
			asc += diff ? "<mark>" + a + "</mark>" : a;
		}
		if (open >= 0) { hx += "</span>"; asc += "</span>"; }
		const same = prevRow && !anyDiff && !starts.length && cur.every((v, i) => v === prevRow[i]);
		prevRow = cur;
		if (same) {
			if (!collapsed)
				lines.push("*");
			collapsed = true;
			continue;
		}
		collapsed = false;
		let line = lo.toString(16).padStart(addr, "0") + " " + hx + " |" + asc + "|";
		if (starts.length) {
			const all = starts.map((s) => s.name).join(" <");
			const room = Math.max(4, spare - 2);
			const shown = all.length > room ? all.slice(0, room - 1) + "…" : all;
			line += ' <span class="sname" title="' + esc2(starts.map((s) => s.name + " " + symRange(s)).join(", ")) +
				'">&lt;' + esc2(shown) + "</span>";
		}
		lines.push(line);
	}
	const dump = $("hexdump");
	dump.classList.remove("asm");
	dump.innerHTML = lines.join("\n");
	dump.dataset.page = p;
	dump.dataset.view = "hex";
	dump.dataset.per = per;
	dump.dataset.nonzero = nonzero;
	dump.dataset.changed = changed;
	dump.dataset.reads = Number(dump.dataset.reads || 0) + 1;
	$("dump_info").textContent = nonzero + " non-zero bytes" +
		(base ? ", " + changed + " changed " + how : "") +
		", read " + new Date().toLocaleTimeString("en-GB");
}

// Instructions whose first byte lies in page p, from disasm.json (llvm-objdump
// of the kernel ELF at build time). Bytes in guest memory that differ from
// what the linker produced are marked (not loaded yet, overwritten, freed).
function renderAsm(p, bytes) {
	const dump = $("hexdump");
	dump.classList.add("asm");
	dump.dataset.page = p;
	dump.dataset.view = "asm";
	if (!disasm) {
		dump.textContent = "No disasm.json for this kernel.";
		dump.dataset.insns = 0;
		$("dump_info").textContent = "";
		return;
	}
	const lo = p * PAGE, hi = lo + PAGE;
	const ins = disasm.insns;
	let a = 0, b = ins.length;
	while (a < b) { const m = (a + b) >> 1; if (ins[m][0] < lo) a = m + 1; else b = m; }
	const labels = new Map(disasm.labels.filter(([pa]) => pa >= lo && pa < hi));
	const sized = symbolsInPage(p).filter((s) => s.size > 0);
	const where = (pa) => {
		const s = symbolAt(sized, pa);
		return s ? s.name + "+0x" + (pa - s.paddr).toString(16) : "";
	};
	let symW = 0;
	for (let i = a; i < ins.length && ins[i][0] < hi; i++)
		symW = Math.max(symW, where(ins[i][0]).length);
	symW = Math.min(symW, 28);
	// Columns that fit next to ~40 characters of instruction text; the text
	// itself wraps (pre-wrap), so there is never a horizontal scroll bar.
	const chars = lineChars();
	const showSym = symW > 0 && !narrow() && chars >= 9 + symW + 1 + 40;
	const showRaw = chars >= 9 + (showSym ? symW + 1 : 0) + 17 + 40;
	const out = [];
	let n = 0, bad = 0;
	for (let i = a; i < ins.length && ins[i][0] < hi; i++) {
		const [pa, raw, text] = ins[i];
		if (labels.has(pa))
			out.push('<span class="lbl">' + labels.get(pa) + ":</span>");
		let ok = true;
		for (let k = 0; k < raw.length / 2 && pa + k < hi; k++)
			if (bytes[pa - lo + k] !== parseInt(raw.substr(k * 2, 2), 16))
				ok = false;
		n++;
		if (!ok)
			bad++;
		let w = showSym ? where(pa) : "";
		if (w.length > symW)
			w = w.slice(0, symW - 1) + "…";
		const line = pa.toString(16).padStart(8, "0") +
			(showSym ? ' <span class="off">' + esc2(w.padEnd(symW, " ")) + "</span>" : "") +
			(showRaw ? ' <span class="raw">' + raw.padEnd(16, " ") + "</span>" : "") +
			" " + text.replace(/[<>&]/g, esc);
		out.push(ok ? line : '<span class="bad">' + line + "</span>");
	}
	dump.innerHTML = out.length ? out.join("\n") : "No kernel instructions in this page.";
	dump.dataset.insns = n;
	dump.dataset.reads = Number(dump.dataset.reads || 0) + 1;
	$("dump_info").textContent = n + " instructions" + (bad ? ", " + bad + " differ from the build" : "") +
		", read " + new Date().toLocaleTimeString("en-GB");
}

function renderPageSymbols(p) {
	const tbody = $("page_symbols").tBodies[0];
	tbody.textContent = "";
	const syms = symbolsInPage(p);
	$("page_symbols").dataset.count = syms.length;
	$("page_symbols").hidden = syms.length === 0;
	$("symbols_note").textContent = !symbols ? "No symbols.json for this kernel: no symbol names." :
		syms.length ? syms.length + " symbol" + (syms.length > 1 ? "s" : "") + " in this page" :
		"No symbols in this page.";
	for (const s of syms) {
		const tr = tbody.insertRow();
		for (const v of [s.name, hex(s.vaddr), symRange(s), String(s.size), s.section + " (" + s.type + ")"])
			tr.insertCell().textContent = v;
	}
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
	renderPageSymbols(p);
	if (!emulator) {
		$("hexdump").textContent = "Boot a kernel to read this page.";
		return;
	}
	renderView(p, readPage(p), scanNow ? scanNow.subarray(p * PAGE, p * PAGE + PAGE) : null, "since the last scan");
}

// The one place the inspector is drawn: hex or asm, whichever is chosen.
function renderView(p, bytes, base, how) {
	if (view === "asm")
		renderAsm(p, bytes);
	else
		renderDump(p, bytes, base, how);
}

function setView(v) {
	view = v;
	$("view_hex").setAttribute("aria-pressed", v === "hex");
	$("view_asm").setAttribute("aria-pressed", v === "asm");
	inspect();
}

function selectPage(p) {
	selectedPage = p;
	drawGrid();
	inspect();
	setLive();
}

function setLive() {
	clearInterval(liveTimer);
	liveTimer = null;
	if ($("live").checked && emulator && selectedPage >= 0)
		liveTimer = setInterval(() => {
			if (emulator && emulator.is_running())
				inspect();
		}, LIVE_MS);
}

// ---- page scan ------------------------------------------------------------

function scan() {
	if (!emulator)
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
	if (selectedPage >= 0) {
		const p = selectedPage;
		renderView(p, scanNow.subarray(p * PAGE, p * PAGE + PAGE),
			scanPrev ? scanPrev.subarray(p * PAGE, p * PAGE + PAGE) : null,
			"since the previous scan");
	}
}

function setAutoscan() {
	clearInterval(scanTimer);
	scanTimer = null;
	if ($("autoscan").checked && emulator)
		scanTimer = setInterval(scan, 1000);
}

// ---- snapshots ------------------------------------------------------------

const HAVE_GZIP = typeof CompressionStream === "function" && typeof DecompressionStream === "function";

async function pipe(buffer, stream) {
	return new Response(new Blob([buffer]).stream().pipeThrough(stream)).arrayBuffer();
}

async function saveSnapshot() {
	if (!emulator)
		return;
	const em = emulator;
	$("snap_save").disabled = true;
	$("snap_status").textContent = "Saving...";
	try {
		const t0 = performance.now();
		const raw = await em.save_state();
		const t1 = performance.now();
		const data = HAVE_GZIP ? await pipe(raw, new CompressionStream("gzip")) : raw;
		const t2 = performance.now();
		snapshots.push({
			id: ++snapCounter, time: new Date(), kernel: image.name, ram: emuRam,
			raw: raw.byteLength, data, gzip: HAVE_GZIP,
			saveMs: Math.round(t1 - t0), gzipMs: Math.round(t2 - t1),
		});
		while (snapshots.length > MAX_SNAPSHOTS)
			snapshots.shift();
		$("snap_status").textContent = "Saved #" + snapCounter + " in " + Math.round(t1 - t0) + " ms" +
			(HAVE_GZIP ? ", gzip took " + Math.round(t2 - t1) + " ms." : ".");
	} catch (e) {
		$("snap_status").textContent = "save_state failed: " + e;
	}
	$("snap_save").disabled = !emulator;
	renderSnapshots();
}

async function restoreSnapshot(s) {
	if (!emulator || emuRam !== s.ram) {
		$("snap_status").textContent = "Snapshot #" + s.id + " was taken with " + kib(s.ram) +
			" of RAM; boot with that size to restore it.";
		return;
	}
	$("snap_status").textContent = "Restoring #" + s.id + "...";
	try {
		const raw = s.gzip ? await pipe(s.data, new DecompressionStream("gzip")) : s.data;
		await emulator.restore_state(raw);
		$("snap_status").textContent = "Restored #" + s.id + " (" + s.kernel + ").";
		$("snap_status").dataset.restored = s.id;
		if (selectedPage >= 0)
			inspect();
	} catch (e) {
		$("snap_status").textContent = "restore_state failed: " + e;
	}
}

function downloadSnapshot(s) {
	const a = document.createElement("a");
	a.href = URL.createObjectURL(new Blob([s.data], { type: "application/octet-stream" }));
	a.download = s.kernel + "-" + s.ram / 1024 + "k-" + s.id + ".v86state" + (s.gzip ? ".gz" : "");
	a.click();
	setTimeout(() => URL.revokeObjectURL(a.href), 10000);
}

function renderSnapshots() {
	const tbody = $("snapshots").tBodies[0];
	tbody.textContent = "";
	for (const s of snapshots) {
		const tr = tbody.insertRow();
		tr.dataset.raw = s.raw;
		tr.dataset.stored = s.data.byteLength;
		const ratio = s.raw / s.data.byteLength;
		const cells = [
			"#" + s.id,
			s.time.toLocaleTimeString("en-GB"),
			s.kernel + ", " + kib(s.ram),
			s.raw.toLocaleString("en"),
			s.gzip ? s.data.byteLength.toLocaleString("en") : "not compressed",
			s.gzip ? ratio.toFixed(1) + " : 1 (" + (100 * s.data.byteLength / s.raw).toFixed(2) + "%)" : "-",
		];
		for (const v of cells)
			tr.insertCell().textContent = v;
		tr.cells[5].className = "ratio";
		const td = tr.insertCell();
		const restore = document.createElement("button");
		restore.type = "button";
		restore.textContent = "Restore";
		restore.className = "restore";
		restore.addEventListener("click", () => restoreSnapshot(s));
		const dl = document.createElement("button");
		dl.type = "button";
		dl.textContent = "Download";
		dl.addEventListener("click", () => downloadSnapshot(s));
		td.append(restore, " ", dl);
	}
	$("snapshots").hidden = snapshots.length === 0;
}

// ---- boot -----------------------------------------------------------------

async function boot() {
	if (!image)
		return;
	if (emulator) {
		const old = emulator;
		emulator = null;
		try {
			await old.destroy();
		} catch (e) {
			console.warn("destroy:", e);
		}
	}
	$("scan").disabled = true;
	$("snap_save").disabled = true;
	$("footprint").textContent = "";
	scanNow = scanPrev = usedPages = changedPages = null;
	emuRam = ram();
	$("screen_text").textContent = "";
	const fatal = checkImage(image.hdr, ram()).some(([level]) => level === "error");
	setStatus("Booting " + image.name + " with " + kib(ram()) + "...");
	try {
		emulator = new V86({
			wasm_path: withV("v86/v86.wasm"),
			memory_size: emuRam,
			vga_memory_size: 256 * 1024,
			screen_container: $("screen_container"),
			bios: { url: withV("v86/seabios.bin") },
			vga_bios: { url: withV("v86/vgabios.bin") },
			bzimage: { buffer: image.bytes.buffer.slice(0) },
			cmdline: "",
			autostart: true,
			disable_mouse: true,
		});
	} catch (e) {
		setStatus("v86 refused to start: " + e);
		return;
	}
	const em = emulator;
	em.add_listener("emulator-started", () => {
		if (em !== emulator)
			return;
		setStatus("Running " + image.name + " with " + kib(ram()) +
			(fatal ? ". The header check predicts this will not work." : "."));
		$("scan").disabled = false;
		$("snap_save").disabled = false;
		setAutoscan();
		setLive();
		computePageClasses();
		drawGrid();
		inspect();
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
	showHeader();
	drawMaps();
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

async function loadKernel(entry, memFromUrl) {
	setStatus("Fetching " + entry.path + "...");
	const resp = await fetch(withV(entry.path));
	if (!resp.ok)
		throw new Error(entry.path + ": HTTP " + resp.status);
	disasm = await loadDisasm(entry);
	useImage(entry.name, await resp.arrayBuffer(), memFromUrl, await loadSymbols(entry));
	await boot();
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
	drawMaps();
	$("memory").addEventListener("change", () => { showHeader(); drawMaps(); });
	$("boot").addEventListener("click", boot);
	$("scan").addEventListener("click", scan);
	$("autoscan").addEventListener("change", setAutoscan);
	$("live").addEventListener("change", setLive);
	for (const v of ["hex", "asm"])
		$("view_" + v).addEventListener("click", () => setView(v));
	// Bytes per row and asm columns follow the inspector's width.
	let dumpWidth = 0;
	new ResizeObserver(() => {
		const w = $("hexdump").clientWidth;
		if (w !== dumpWidth) {
			dumpWidth = w;
			requestAnimationFrame(inspect);
		}
	}).observe($("hexdump"));
	$("snap_save").addEventListener("click", saveSnapshot);
	$("snap_save").title = HAVE_GZIP ?
		"Compressed in the browser with CompressionStream(\"gzip\")." :
		"This browser has no CompressionStream: snapshots are kept uncompressed.";
	if (!HAVE_GZIP)
		$("snap_note").textContent = "no gzip in this browser: snapshots uncompressed";
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
		useImage(f.name, await f.arrayBuffer());
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
		$("kernel").add(new Option(k.name, k.name));
	const wanted = kernels.find((k) => k.name === params.get("kernel")) || kernels[0];
	if (!wanted) {
		setStatus("kernels.json lists no kernels. Load a bzImage from disk.");
		return;
	}
	$("kernel").value = wanted.name;
	await loadKernel(wanted, memParam);
}

init().catch((e) => setStatus(String(e)));
