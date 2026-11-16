"use strict";

// Memory tab: the memory map's bars and legend, the page grid, the EIP's
// page in both (M1), and the page under the pointer.

import { $, kib, PAGE, hex, V86_KERNEL_ADDR, V86_INITRD_ADDR, h8 } from "./util.js";
import { usedPages, pageClass, image, initrd, emulator, emuRam, symbols, setPageClass, setGrid, changedPages,
	selectedPage, stepper, runState, grid, tab, api, state, provide } from "./page.js";
import { IRQ_COLOUR, SWITCH, PALETTE } from "./graph.js";
import { ffActive } from "./fastforward.js";
import { pageWalk } from "./cpustate.js";
import { bmRenderPages } from "./bookmarks.js";
import { ram, V86_SETUP_ADDR, V86_CMDLINE_ADDR, kernelEnd } from "./header.js";

let focusClass = null;   // page class picked from a legend or bar: highlight those pages
let pageSections = [];   // per page: section classes it overlaps, or null

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
	setPageClass(cls);
}

function drawGrid() {
	const pages = gridRam() / PAGE;
	if (pageClass.length !== pages)
		computePageClasses();
	const cols = pages <= 1024 ? 32 : 64;
	const cell = pages <= 1024 ? 14 : 9;
	setGrid({ cols, cell, pages });
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
		go.addEventListener("click", () => api.show.jumpTo(w.phys));
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
		go.addEventListener("click", () => api.show.jumpTo(w.sphys));
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
		b.addEventListener("click", () => api.show.navJump(() => {
			api.show.showTab("about");
			// Below the header, which stays on top on a wide screen.
			const top = document.querySelector("header.top");
			const over = getComputedStyle(top).position === "sticky" ? top.getBoundingClientRect().height : 0;
			window.scrollTo(0, window.scrollY + $("about_legend").getBoundingClientRect().top - over - 8);
		}));
	// The EIP's page in the map and the grid, drawn with the Debugger.
	state.subscribe(renderMemEip, ["machine"]);
	// The grid is drawn when the Memory tab is shown (showTab).
	state.subscribe(() => {
		if (tab === "memory")
			drawGrid();
	}, ["tabShows"]);
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

const symDesc = (st, i, a) => {
	if (i < 0)
		return a >= 0 && a < V86_KERNEL_ADDR ? "(outside the kernel: BIOS, option ROM or setup)" :
			a >= V86_KERNEL_ADDR && image && a < kernelEnd(image.hdr) && i === -1 ?
				"(not vmlinux code: relocation stub or not loaded yet)" : st.symLabel(i);
	return st.symName[i];
};

// The page API's entries of this module (page.js).
provide({ drawGrid, symbolAt, drawMaps, pageLabel, symbolsInPage, pageAt, memCellStyle });

export { symbolsInPage, symbolAt, symRange, gridRam, pageLabel, drawGrid, computePageClasses, symDesc,
	drawMaps, initLegend, pageAt, showHover, REGION_NAMES, SECTION_NAMES, memCellStyle };
