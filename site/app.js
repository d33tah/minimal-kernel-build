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
		text += usedPages[p] ? ", data" : ", all zero";
	if (syms.length)
		text += ": " + syms[0].name + (syms.length > 1 ? " and " + (syms.length - 1) + " more symbols" : "");
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

// One listing of page p in address order: instructions (disasm.json) as
// "addr bytes insn symbol+off", everything else as hexdump -C rows, symbol
// starts as "name:" headers. base: bytes from the last scan (changed bytes
// are marked); cur: physical address of the next instruction, or -1.
function listPage(p, bytes, base, cur, chars) {
	const lo = p * PAGE, hi = lo + PAGE;
	const per = hexPer(chars);
	const wide = chars >= 100;
	const syms = symbolsInPage(p).filter((s) => s.paddr >= lo);
	const sized = symbolsInPage(p).filter((s) => s.size > 0);
	const insns = new Map();
	if (disasm)
		for (let i = insnIndex(lo); i < disasm.insns.length && disasm.insns[i][0] < hi; i++)
			insns.set(disasm.insns[i][0], disasm.insns[i]);
	const out = [];
	let si = 0, nIns = 0, nBad = 0, nRows = 0, nonzero = 0, changed = 0, curLine = -1;
	let prevRow = null, starred = false;
	const mark = (off, text) => (base && base[off] !== bytes[off] ? "<mark>" + text + "</mark>" : text);
	for (let off = 0; off < PAGE; off++)
		if (bytes[off]) nonzero++;
	if (base)
		for (let off = 0; off < PAGE; off++)
			if (base[off] !== bytes[off]) changed++;
	let a = lo;
	while (a < hi) {
		while (si < syms.length && syms[si].paddr <= a) {
			const s = syms[si++];
			if (s.paddr === a || !insns.has(s.paddr))
				out.push('<span class="lbl" data-sym="' + escHtml(s.name) + '" data-addr="' + s.paddr + '">' +
					escHtml(s.name) + ":" + (s.size ? ' <span class="dim">' + s.size + " bytes, " + s.section + "</span>" : "") + "</span>");
			prevRow = null;
		}
		const nextSym = si < syms.length ? syms[si].paddr : hi;
		const ins = insns.get(a);
		if (ins) {
			const [pa, raw, text] = ins;
			const n = raw.length / 2;
			let ok = true, rawHtml = "";
			for (let k = 0; k < n && pa + k < hi; k++) {
				const want = parseInt(raw.substr(k * 2, 2), 16);
				if (bytes[pa - lo + k] !== want)
					ok = false;
				if (k < 7)
					rawHtml += mark(pa - lo + k, bytes[pa - lo + k].toString(16).padStart(2, "0"));
			}
			rawHtml += n > 7 ? "…" : "  ".repeat(7 - n) + " ";
			const s = symbolAt(sized, pa);
			const where = s ? s.name + "+0x" + (pa - s.paddr).toString(16) : "";
			let cls = ok ? "ins" : "ins bad";
			if (pa === cur) {
				cls += " cur";
				curLine = out.length;
			}
			out.push('<span class="' + cls + '" data-addr="' + pa + '">' + pa.toString(16).padStart(8, "0") +
				" " + rawHtml + " " + text.replace(/[<>&]/g, esc) +
				(wide && where ? ' <span class="dim">' + escHtml(where) + "</span>" : "") + "</span>");
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
		out.push('<span class="' + cls + '" data-addr="' + a + '">' + (a - pad).toString(16).padStart(8, "0") +
			" " + hx + " |" + asc + "|</span>");
		nRows++;
		a = end;
	}
	return { html: out.join("\n"), nIns, nBad, nRows, nonzero, changed, curLine };
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
				scrollListingTo(target);
		});
		el.append(b);
	}
}

function scrollListingTo(line) {
	const box = $("listing");
	box.scrollTop = line.offsetTop - box.offsetTop - box.clientHeight / 3;
}

// Draw the selected page; base: bytes of the last scan (changes are marked).
function renderPage(p, bytes, base, how) {
	const box = $("listing");
	const cur = currentPhys();
	const r = listPage(p, bytes, base, cur >= p * PAGE && cur < p * PAGE + PAGE ? cur : -1, lineChars(box));
	box.innerHTML = r.html;
	Object.assign(box.dataset, { page: p, insns: r.nIns, hexrows: r.nRows, nonzero: r.nonzero,
		changed: r.changed, reads: Number(box.dataset.reads || 0) + 1 });
	if (r.curLine >= 0) {
		const line = box.querySelector(".cur");
		if (line)
			scrollListingTo(line);
	}
	$("dump_info").textContent = r.nIns + " instructions" + (r.nBad ? " (" + r.nBad + " differ from the build)" : "") +
		", " + r.nonzero + " non-zero bytes" + (base ? ", " + r.changed + " changed " + how : "") +
		", read " + new Date().toLocaleTimeString("en-GB");
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
	if (!emulator) {
		$("listing").textContent = "Boot a kernel to read this page.";
		return;
	}
	renderPage(p, readPage(p), scanNow ? scanNow.subarray(p * PAGE, p * PAGE + PAGE) : null, "since the last scan");
}

function selectPage(p, show = true) {
	selectedPage = p;
	drawGrid();
	inspect();
	setLive();
	if (show)
		showTab("page");
	else
		writeHash();
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

const TABS = ["memory", "page", "debugger", "snapshots", "about"];
let tab = "memory";

// Hash: #memory, #page/0x100000, #debugger, ...
function writeHash() {
	const h = "#" + tab + (tab === "page" && selectedPage >= 0 ? "/" + hex(selectedPage * PAGE) : "");
	if (location.hash !== h)
		history.replaceState(null, "", h);
}

function showTab(name) {
	tab = TABS.includes(name) ? name : "memory";
	for (const t of TABS) {
		$("panel_" + t).hidden = t !== tab;
		$("tab_" + t).setAttribute("aria-selected", t === tab);
	}
	writeHash();
	if (tab === "page")
		inspect();
	if (tab === "memory")
		drawGrid();
	if (tab === "debugger")
		renderContext();
}

function readHash() {
	const [name, addr] = location.hash.slice(1).split("/");
	if (addr) {
		const a = parseInt(addr, 16);
		if (Number.isFinite(a))
			selectedPage = Math.floor(a / PAGE);
	}
	showTab(name || "memory");
}

// ---- page scan ------------------------------------------------------------

// render: also redraw the page listing.
function scan(render = true) {
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
		if (counting || (stepper && stepper.steps)) {
			counting = false;
			countNote = "A snapshot was restored: the counters stopped there.";
		}
		if (runState === "paused" || runState === "stepped")
			afterStop();
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

async function boot() {
	if (!image)
		return;
	stopDriver();
	if (emulator) {
		const old = emulator;
		emulator = null;
		try {
			await old.destroy();
		} catch (e) {
			console.warn("destroy:", e);
		}
	}
	stepper = null;
	runState = "off";
	$("scan").disabled = true;
	$("snap_save").disabled = true;
	$("footprint").textContent = "";
	scanNow = scanPrev = usedPages = changedPages = null;
	emuRam = ram();
	$("screen_text").textContent = "";
	resetCounters();
	updateDebugUi();
	const fatal = checkImage(image.hdr, ram()).some(([level]) => level === "error");
	setStatus("Booting " + image.name + " with " + kib(ram()) + "...");
	try {
		emulator = new V86({
			memory_size: emuRam,
			vga_memory_size: 256 * 1024,
			screen_container: $("screen_container"),
			wasm_fn: loadWasm,
			bios: { url: withV("v86/seabios.bin") },
			vga_bios: { url: withV("v86/vgabios.bin") },
			bzimage: { buffer: image.bytes.buffer.slice(0) },
			cmdline: "",
			autostart: false,
			disable_mouse: true,
		});
	} catch (e) {
		setStatus("v86 refused to start: " + e);
		return;
	}
	const em = emulator;
	em.add_listener("emulator-loaded", () => {
		if (em !== emulator)
			return;
		setStatus("Running " + image.name + " with " + kib(ram()) +
			(fatal ? ". The header check predicts this will not work." : "."));
		$("scan").disabled = false;
		$("snap_save").disabled = false;
		try {
			stepper = new Stepper(em, V86_KERNEL_ADDR, kernelEnd(image.hdr));
			stepper.milestones = MILESTONES.map((text) => ({ text, found: null }));
		} catch (e) {
			stepper = null;
			$("dbg_status").textContent = "No debugger: " + (wasmPatchError || e);
		}
		counting = !!stepper && $("count").checked;
		countStart = performance.now();
		countMs = 0;
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
		} else {
			resume();
		}
	});
}

// ---- debugger -------------------------------------------------------------

const MILESTONES = ["Hello, World!", "Still alive"];
const COUNT_LIMIT_MS = 120000;  // give up counting after this much stepping
const SLICE_MS = 25;            // fast stepping: JS time per slice

let stepper = null;
let runState = "off";    // "off", "jit" (v86's own loop), "stepped" (our loop), "paused"
let counting = false;    // stepping from reset until every milestone is on screen
let countStart = 0;      // performance.now() when the count began
let countMs = 0;         // stepping time spent counting
let countNote = "";
let startPaused = false; // ?paused=1: stop at the reset vector
let runTo = -1;          // physical address "Run to" stops at, or -1
let driverTimer = null;
let driverChannel = null;
let lastUi = 0;
let lastFrame = 0;
let stepCredit = 0;
let prevRegs = null;
let flashes = 0;

const speed = () => $("dbg_speed").value;  // "full" or instructions per second

function resetCounters() {
	counting = false;
	countMs = 0;
	countNote = "";
	runTo = -1;
	prevRegs = null;
	flashes = 0;
	$("pages").dataset.flashes = 0;
}

function countDone() {
	return !stepper || stepper.milestones.every((m) => m.found);
}

function wantStepped() {
	return !!stepper && (speed() !== "full" || counting || runTo >= 0);
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

// Run the VM in whichever way the controls ask for.
async function resume() {
	if (!emulator)
		return;
	stopDriver();
	if (wantStepped()) {
		if (emulator.is_running())
			await emulator.stop();
		runState = "stepped";
		stepCredit = 0;
		scheduleDriver();
	} else {
		runState = "jit";
		emulator.run();
	}
	updateDebugUi();
}

async function pause(note) {
	if (!emulator)
		return;
	stopDriver();
	if (emulator.is_running())
		await emulator.stop();
	runState = "paused";
	runTo = -1;
	if (note !== undefined)
		$("dbg_status").textContent = note;
	afterStop();
}

function scheduleDriver() {
	if (speed() === "full" || runTo >= 0) {
		// As fast as possible: zero-delay message loop, works in background tabs.
		if (!driverChannel) {
			driverChannel = new MessageChannel();
			driverChannel.port1.onmessage = driverTick;
		}
		driverChannel.port2.postMessage(0);
	} else {
		lastFrame = requestAnimationFrame((t) => { lastFrame = 0; driverTick(t); });
	}
}

let prevTick = 0;
function driverTick() {
	if (runState !== "stepped" || !stepper)
		return;
	const now = performance.now();
	let reason;
	const t0 = now;
	if (speed() === "full" || runTo >= 0) {
		reason = stepper.run(Infinity, runTo, SLICE_MS);
	} else {
		const rate = Number(speed());
		const dt = Math.min(250, prevTick ? now - prevTick : 16);
		stepCredit = Math.min(stepCredit + rate * dt / 1000, rate + 1);
		const n = Math.floor(stepCredit);
		stepCredit -= n;
		reason = n ? stepper.run(n, -1, SLICE_MS) : "count";
	}
	prevTick = now;
	if (counting)
		countMs += performance.now() - t0;
	if (reason === "addr") {
		pause("Stopped at " + hex(stepper.nextPhys()) + " (Run to).");
		return;
	}
	if (counting && (countDone() || countMs > COUNT_LIMIT_MS || stepper.dead())) {
		counting = false;
		if (!countDone())
			countNote = stepper.dead() ? "The CPU halted with interrupts off before every line appeared." :
				"Gave up after " + Math.round(countMs / 1000) + " s of stepping.";
		if (speed() === "full" && runTo < 0) {
			updateDebugUi();
			resume();
			return;
		}
	}
	if (reason === "hlt" && stepper.dead()) {
		pause("The CPU executed hlt with interrupts off: nothing more to run.");
		return;
	}
	const slow = speed() !== "full" && runTo < 0;
	if (slow || now - lastUi > 200) {
		lastUi = now;
		updateDebugUi();
		if (slow)
			frameUpdate();
	}
	if (reason === "hlt")
		driverTimer = setTimeout(scheduleDriver, slow ? 16 : 1);
	else
		scheduleDriver();
}

// Live view while slow-stepping: grid flashes, asm view follows EIP.
let lastFrameScan = 0;
function frameUpdate() {
	const now = performance.now();
	if (now - lastFrameScan >= (emuRam > 4 * 1024 * 1024 ? 250 : 0)) {
		lastFrameScan = now;
		scan(false);
		const n = Number($("footprint").dataset.changed || 0);
		$("pages").dataset.flashed = n;
		if (n)
			$("pages").dataset.flashes = ++flashes;
	}
	followEip();
}

function afterStop() {
	// Baseline for the grid: pages written after this flash.
	scan(false);
	updateDebugUi();
	followEip();
}

// While paused or stepping, the Page tab shows the page holding EIP.
function followEip() {
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
		if (tab === "page")
			inspect();
	}
	renderContext();
}

// Debugger tab: the next instructions from disasm.json, or raw bytes.
function renderContext() {
	const el = $("context");
	if (!stepper || tab !== "debugger" || runState === "jit" || runState === "off") {
		el.textContent = runState === "jit" ? "Running at full speed. Pause to see the next instructions." : "";
		return;
	}
	const phys = stepper.nextPhys();
	if (phys < 0) {
		el.textContent = "EIP is not mapped.";
		return;
	}
	const lines = [];
	const i = disasm ? insnIndex(phys) : -1;
	if (i >= 0 && i < disasm.insns.length && disasm.insns[i][0] === phys) {
		const sized = symbols ? symbols.symbols.filter((s) => s.size > 0 && s.paddr <= phys + 64 && s.paddr + s.size > phys - 64) : [];
		for (let k = Math.max(0, i - 3); k < Math.min(disasm.insns.length, i + 12); k++) {
			const [pa, raw, text] = disasm.insns[k];
			const sym = symbolAt(sized, pa);
			const line = (k === i ? "\u25b6 " : "  ") + pa.toString(16).padStart(8, "0") + " " +
				escHtml(text) + (sym ? ' <span class="dim">' + escHtml(sym.name) + "+0x" + (pa - sym.paddr).toString(16) + "</span>" : "");
			lines.push(k === i ? '<span class="cur">' + line + "</span>" : line);
		}
		el.dataset.source = "disasm";
	} else {
		const n = Math.min(16, emuRam - phys);
		const b = n > 0 ? emulator.read_memory(phys, n) : [];
		lines.push('<span class="cur">\u25b6 ' + phys.toString(16).padStart(8, "0") + " " +
			Array.from(b, (v) => v.toString(16).padStart(2, "0")).join(" ") + "</span>");
		lines.push('<span class="dim">Not kernel code in disasm.json (BIOS, option ROM or real-mode setup): raw bytes at EIP.</span>');
		el.dataset.source = "bytes";
	}
	el.innerHTML = lines.join("\n");
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
		if (prevRegs && prevRegs[name] !== undefined && prevRegs[name] !== value)
			d.className = "chg";
		const b = document.createElement("b");
		b.textContent = name;
		d.append(b, " " + value);
		if (title)
			d.title = title;
		if (name === "EFLAGS" || name === "mode")
			d.classList.add("wide");
		el.append(d);
	}
	prevRegs = Object.fromEntries(cells.map(([n, v]) => [n, v]));
	el.dataset.eip = r.eip;
	el.dataset.phys = phys;
}

const num = (n) => (n === null || n === undefined ? "-" : n.toLocaleString("en"));

function renderCounters() {
	const tbody = $("counters").tBodies[0];
	tbody.textContent = "";
	if (!stepper) {
		$("count_text").textContent = !emulator ? "" : "Not counted: " + (wasmPatchError || "no debugger") + ".";
		return;
	}
	const rows = stepper.milestones.map((m) => [m.text, m.found]);
	rows.push(["now", stepper.snapshot()]);
	for (const [label, c] of rows) {
		const tr = tbody.insertRow();
		tr.dataset.milestone = label;
		const cells = c ? [label, num(c.steps), num(c.sinceEntry), num(c.uniqueKernel) + "/" + num(c.unique - c.uniqueKernel)] :
			[label, counting ? "counting..." : "not reached", "", ""];
		for (const v of cells)
			tr.insertCell().textContent = v;
		if (c) {
			tr.dataset.steps = c.steps;
			tr.dataset.entry = c.sinceEntry === null ? "" : c.sinceEntry;
			tr.dataset.unique = c.unique;
			tr.dataset.kernel = c.uniqueKernel;
			tr.cells[0].title = "Reached by the instruction at physical " + hex(c.phys);
		}
	}
	const rate = countMs > 0 ? stepper.steps / countMs / 1000 : 0;
	const info = $("count_text");
	$("count_info").dataset.ms = Math.round(countMs);
	info.textContent = (counting ? (runState === "paused" ? "Counting (paused): " : "Counting: ") : stepper.steps ? "Counted " : "") +
		(stepper.steps ? num(stepper.steps) + " instructions stepped" +
			(countMs ? " in " + (countMs / 1000).toFixed(2) + " s (" + rate.toFixed(1) + " M/s)" : "") + ". " : "") +
		(!$("count").checked && !stepper.steps ? "Counting is off: the boot ran at full speed. " : "") +
		(runState === "jit" && !countDone() && stepper.steps ? "Running at full speed now: not counting. " : "") +
		countNote;
}

function updateDebugUi() {
	const st = $("dbg_state");
	st.textContent = { off: "", jit: "running (full speed, JIT)", stepped: counting ? "stepping (counting)" : "stepping",
		paused: "paused" }[runState];
	st.dataset.state = runState;
	const can = !!stepper;
	$("dbg_pause").disabled = !can || runState === "paused" || runState === "off";
	$("dbg_continue").disabled = !can || runState !== "paused";
	for (const id of ["dbg_step", "dbg_stepn", "dbg_runto"])
		$(id).disabled = !can || runState === "off";
	renderRegs();
	renderCounters();
	if (runState !== "stepped")
		renderContext();
}

async function doStep(n) {
	if (!stepper)
		return;
	if (runState !== "paused")
		await pause();
	if (stepper.dead()) {
		$("dbg_status").textContent = "The CPU executed hlt with interrupts off: nothing to step.";
		return;
	}
	const before = stepper.steps;
	const reason = stepper.run(n, -1, 5000);
	$("dbg_status").textContent = "Stepped " + num(stepper.steps - before) + " instruction" +
		(stepper.steps - before === 1 ? "" : "s") + (reason === "hlt" ? " (halted)" :
			reason === "milestone" ? " (a line appeared on the screen)" : reason === "time" ? " (5 s limit)" : "") + ".";
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

async function doRunTo() {
	if (!stepper)
		return;
	const phys = parseAddr($("dbg_addr").value);
	if (phys < 0) {
		$("dbg_status").textContent = "Not a hex address: " + $("dbg_addr").value;
		return;
	}
	if (runState !== "paused")
		await pause();
	runTo = phys;
	$("dbg_status").textContent = "Running to physical " + hex(phys) + "...";
	resume();
}

function initDebugger() {
	$("dbg_pause").addEventListener("click", () => pause("Paused."));
	$("dbg_continue").addEventListener("click", () => { $("dbg_status").textContent = ""; resume(); });
	$("dbg_step").addEventListener("click", () => doStep(1));
	$("dbg_stepn").addEventListener("click", () => {
		const n = Math.max(1, Math.floor(Number($("dbg_n").value) || 100));
		doStep(n);
	});
	$("dbg_runto").addEventListener("click", doRunTo);
	$("dbg_addr").addEventListener("keydown", (e) => { if (e.key === "Enter") doRunTo(); });
	$("dbg_n").addEventListener("keydown", (e) => { if (e.key === "Enter") $("dbg_stepn").click(); });
	$("dbg_speed").addEventListener("change", () => {
		if (runState === "stepped" || runState === "jit")
			resume();
	});
	$("count").addEventListener("change", () => {
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

// ---- popovers --------------------------------------------------------------

// Every "?" marker: title attributes never show on touch screens, so the
// text moves into data-tip and a popover shows it. Click, tap, Enter or
// Space toggles it, hover shows it on devices that hover; a tap elsewhere
// or Esc closes it. One popover at a time.
let tipOpen = null;    // marker whose popover is shown
let tipPinned = false; // opened by click/key (stays until closed)

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

function showTip(el, pinned) {
	if (tipOpen && tipOpen !== el)
		tipOpen.setAttribute("aria-expanded", "false");
	tipOpen = el;
	tipPinned = pinned;
	el.setAttribute("aria-expanded", "true");
	placeTip(el);
}

function hideTip() {
	if (tipOpen)
		tipOpen.setAttribute("aria-expanded", "false");
	tipOpen = null;
	tipPinned = false;
	$("tip_pop").hidden = true;
}

function initTips() {
	const hover = window.matchMedia("(hover: hover)").matches;
	for (const el of document.querySelectorAll(".tip")) {
		if (el.title) {
			el.dataset.tip = el.title;
			el.removeAttribute("title");
		}
		el.setAttribute("aria-expanded", "false");
		el.setAttribute("aria-label", "Explain");
		el.setAttribute("aria-controls", "tip_pop");
		const toggle = (ev) => {
			ev.preventDefault(); // inside a label: do not activate its control
			ev.stopPropagation();
			if (tipOpen === el && tipPinned)
				hideTip();
			else
				showTip(el, true);
		};
		el.addEventListener("click", toggle);
		el.addEventListener("keydown", (ev) => { if (ev.key === "Enter" || ev.key === " ") toggle(ev); });
		if (hover) {
			el.addEventListener("mouseenter", () => { if (!tipPinned) showTip(el, false); });
			el.addEventListener("mouseleave", () => { if (!tipPinned && tipOpen === el) hideTip(); });
		}
	}
	document.addEventListener("click", (ev) => {
		if (tipOpen && !$("tip_pop").contains(ev.target))
			hideTip();
	});
	document.addEventListener("keydown", (ev) => { if (ev.key === "Escape") hideTip(); });
	window.addEventListener("scroll", () => { if (tipOpen) placeTip(tipOpen); }, { passive: true });
	window.addEventListener("resize", () => { if (tipOpen) placeTip(tipOpen); });
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
	initTips();
	drawMaps();
	initDebugger();
	updateDebugUi();
	for (const t of TABS)
		$("tab_" + t).addEventListener("click", () => showTab(t));
	window.addEventListener("hashchange", readHash);
	readHash();
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
