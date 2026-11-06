"use strict";
// Worker: DWARF of an imported vmlinux, off the UI thread. Reads File
// slices (FileReaderSync), never the whole file: the ELF headers and the
// small .debug_* sections on "open", then one compile unit (and its line
// program) per "unit" request. dbginfo.js talks to it.
//
//   { type: "open", file, offset }  -> { type: "open", ok, why?, units, versions, index: [[lo, hi, unit], ...] }
//   { type: "unit", i }             -> { type: "unit", i, table }       (dwarf.js table, physical addresses)
//   { type: "types" }               -> { type: "types-progress", done, total } now and then, then
//                                      { type: "types", types }  (dwarf.js DwarfTypes: every unit read once)
//   { type: "cfi" }                 -> { type: "cfi", table, source } (unwind.js: .debug_frame or .eh_frame
//                                      parsed whole, ~2 MB for a distro kernel) or { type: "cfi", why }
//   { type: "locals", i }           -> { type: "locals", i, groups, base, added } (after "types", in the
//                                      same Worker: dwarf.js dwarfScopeGroups of unit i with the type ids
//                                      of the types sent; added: types new to that table, appended at base)
//   { type: "lines", q }            -> { type: "lines", q, path, units, code } (q: a file as typed; code:
//                                      dwarf.js dwarfFileCode of every unit whose line program names
//                                      it, each unit decoded and dropped in turn) or { type: "lines", q, why }
//   any failure                     -> { type: "error", i?, message }

importScripts("dwarf.js" + self.location.search, "unwind.js" + self.location.search);

let dw = null, toPa = null, elf = null, readFile = null, typesAcc = null, fileUnits = null;

function open(file, offset) {
	const rd = new FileReaderSync();
	const read = (off, len) => new Uint8Array(rd.readAsArrayBuffer(file.slice(off, off + len)));
	readFile = read;
	elf = dwarfElf(read);
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

// Variables and types need every unit (no name index in a vmlinux). The
// types of parameters and locals are in the table too (decodeData with
// locals), so a unit's locals asked for later need no new ones.
function types() {
	const acc = new DwarfTypes(dw);
	typesAcc = acc;
	const sections = [];
	const secOf = dwarfSectionOf(elf, sections);
	const us = dw.units();
	let last = 0;
	for (let i = 0; i < us.length; i++) {
		if (us[i].unitType === 1 || us[i].unitType === 3)
			acc.addUnit(dw.decodeData(i, true), toPa, secOf);
		if (Date.now() - last > 250) {
			last = Date.now();
			self.postMessage({ type: "types-progress", done: i + 1, total: us.length });
		}
	}
	return acc.finish(sections);
}

// The code of a source file, every line: units are found by the file
// tables of their line programs (headers only, fast), then decoded one at
// a time (a header's code can be in most of a distro kernel's units).
function lines(q) {
	if (!fileUnits) {
		fileUnits = new Map();
		const us = dw.units();
		for (let i = 0; i < us.length; i++) {
			if (us[i].unitType !== 1 && us[i].unitType !== 3)
				continue;
			const r = dw.root(i);
			if (r.stmt < 0)
				continue;
			for (const f of new Set(dw.lineProgram(r.stmt, r.c.comp, true).files))
				if (f) {
					if (!fileUnits.has(f))
						fileUnits.set(f, []);
					fileUnits.get(f).push(i);
				}
		}
	}
	const p = dwarfResolvePath([...fileUnits.keys()], q);
	if (!p.path)
		return { type: "lines", q, why: p.why };
	const units = fileUnits.get(p.path);
	function* tables() {
		for (const i of units)
			yield [0, Infinity, dwarfTable([dw.decodeUnit(i)], toPa)];
	}
	return { type: "lines", q, path: p.path, units: units.length, code: dwarfFileCode(tables(), p.path) };
}

self.onmessage = (ev) => {
	const m = ev.data;
	try {
		if (m.type === "open")
			self.postMessage(open(m.file, m.offset));
		else if (m.type === "cfi") {
			const c = cfiFromElf(elf, readFile, toPa);
			self.postMessage(c.why ? { type: "cfi", why: c.why } : { type: "cfi", table: c.t, source: c.source });
		} else if (m.type === "lines")
			self.postMessage(lines(m.q));
		else if (m.type === "types")
			self.postMessage({ type: "types", types: types() });
		else if (m.type === "locals") {
			if (!typesAcc)
				throw new Error("the variables and types are not read yet");
			const r = typesAcc.scopesOf(dw.decodeData(m.i, true), toPa);
			self.postMessage({ type: "locals", i: m.i, groups: r.groups, base: typesAcc.types.length - r.added.length, added: r.added });
		} else if (m.type === "unit") {
			const table = dwarfTable([dw.decodeUnit(m.i)], toPa);
			self.postMessage({ type: "unit", i: m.i, table });
		}
	} catch (e) {
		self.postMessage({ type: "error", i: m.i, q: m.type === "lines" ? m.q : undefined, message: String(e && e.message || e) });
	}
};
