"use strict";

// Page scan: which guest pages hold data and which changed since the last
// scan (the Memory tab's grid colours them).

import { PAGE, $, kib } from "./util.js";
import { emulator, emuReady, emuRam, selectedPage, usedPages, setUsedPages, changedPages, setChangedPages, api,
	tab } from "./page.js";

let scanTimer = null;
let scanCopy = null;     // Uint8Array: guest RAM as the latest scan saw it (updated in place)

// One copy of RAM as the latest scan saw it (scanCopy), updated in place:
// a scan compares RAM with it page by page and copies in only the pages
// that changed, keeping the selected page's bytes from the scan before
// for its drawing "since the previous scan". Two copies
// (this scan's and the one before's, 128 MiB for Debian's 64 MiB) and a
// new copy at every scan (64 MiB of garbage each) were needed before.
// Hashes per page instead of the copy would lose the marks on a page
// written after a scan and selected then, as when code is copied and then
// jumped to (the relocation stub): suite legend, case yellow.
const pageWords = (u8) => new Int32Array(u8.buffer, u8.byteOffset, u8.length >> 2);

// Page p's bytes at the latest scan (a view of scanCopy), or null.
function scanBase(p) {
	return scanCopy && p >= 0 && p * PAGE < scanCopy.length ? scanCopy.subarray(p * PAGE, p * PAGE + PAGE) : null;
}

// render: also redraw the page listing.
function scan(render = true) {
	if (!emulator || !emuReady)
		return;
	const r = emuRam;
	const mem = emulator.read_memory(0, r); // a view of guest RAM, not a copy
	const words = pageWords(mem);
	const pages = r / PAGE;
	const per = PAGE / 4;
	const prev = scanCopy && scanCopy.length === r;
	if (!prev)
		scanCopy = new Uint8Array(r);
	const copy = pageWords(scanCopy);
	const sel = selectedPage;
	let before = null;
	setUsedPages(new Uint8Array(pages));
	setChangedPages(prev ? new Uint8Array(pages) : null);
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
		if (!prev)
			continue;
		let i = 0;
		while (i < per && words[base + i] === copy[base + i])
			i++;
		if (i < per) {
			changedPages[p] = 1;
			changed++;
			if (p === sel)
				before = scanCopy.slice(p * PAGE, p * PAGE + PAGE);
			scanCopy.set(mem.subarray(p * PAGE, p * PAGE + PAGE), p * PAGE);
		}
	}
	if (!prev)
		scanCopy.set(mem);
	api.memory.drawGrid();
	const fp = $("footprint");
	fp.dataset.nonzero = count;
	fp.dataset.total = pages;
	fp.dataset.changed = prev ? changed : "";
	fp.textContent = count + " of " + pages + " pages non-zero = " + kib(count * PAGE) +
		" (" + low + " below 1 MiB, " + (count - low) + " above)" +
		(prev ? "; " + changed + " changed since the previous scan" : "");
	if (render && sel >= 0 && sel < pages && tab === "page") {
		const now = scanBase(sel);
		api.show.renderPage(sel, now, before || (prev ? now : null), "since the previous scan");
	}
}

function setAutoscan() {
	clearInterval(scanTimer);
	scanTimer = null;
	if ($("autoscan").checked && emulator)
		scanTimer = setInterval(() => scan(), 1000);
}

// Other modules change them through these (an imported binding is read-only).
const setScanCopy = (v) => { scanCopy = v; };

export { scanBase, scanCopy, setAutoscan, scan, setScanCopy };
