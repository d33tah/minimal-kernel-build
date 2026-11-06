"use strict";
// Worker: DWARF of an imported vmlinux, off the UI thread. Reads File
// slices (FileReaderSync), never the whole file: the ELF headers and the
// small .debug_* sections on "open", then one compile unit (and its line
// program) per "unit" request. dbginfo.js talks to it.
//
//   { type: "open", file, offset }  -> { type: "open", ok, why?, units, versions, index: [[lo, hi, unit], ...] }
//   { type: "unit", i }             -> { type: "unit", i, table }       (dwarf.js table, physical addresses)
//   any failure                     -> { type: "error", i?, message }

importScripts("dwarf.js" + self.location.search);

let dw = null, toPa = null;

function open(file, offset) {
	const rd = new FileReaderSync();
	const read = (off, len) => new Uint8Array(rd.readAsArrayBuffer(file.slice(off, off + len)));
	const elf = dwarfElf(read);
	const sec = dwarfSections(elf, read);
	if (!sec)
		return { type: "open", ok: false, why: "this vmlinux has no DWARF (.debug_info): build it with CONFIG_DEBUG_INFO for file:line" };
	dw = new DwarfReader(sec);
	// Physical = virtual - the offset the symbols use, for code inside a
	// PT_LOAD segment only (discarded code sits at 0).
	toPa = (va) => elf.toPhys(va) === null ? null : va - offset;
	const units = dw.units();
	const index = [];
	for (const [lo, hi, i] of dw.unitIndex()) {
		const pa = toPa(lo);
		if (pa !== null)
			index.push([pa, pa + (hi - lo), i]);
	}
	return { type: "open", ok: true, units: units.length, versions: [...new Set(units.map((u) => u.ver))],
		aranges: !!sec.aranges, index };
}

self.onmessage = (ev) => {
	const m = ev.data;
	try {
		if (m.type === "open")
			self.postMessage(open(m.file, m.offset));
		else if (m.type === "unit") {
			const table = dwarfTable([dw.decodeUnit(m.i)], toPa);
			self.postMessage({ type: "unit", i: m.i, table });
		}
	} catch (e) {
		self.postMessage({ type: "error", i: m.i, message: String(e && e.message || e) });
	}
};
