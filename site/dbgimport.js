"use strict";

// An imported vmlinux's debug info (dbginfo.js): its DWARF read by the
// Worker, kept in IndexedDB across reloads, restored and forgotten.

import { withV } from "./util.js";
import { dbgFresh, DBG, dbgChanged, dbgIdbOp, dbgSetCfi } from "./dbginfo.js";

// Units of the import are stored as key + ":unit:" + i, the index as key + ":index".
async function dbgImport(file, offset, key) {
	dbgFresh("import", "");
	DBG.key = key;
	DBG.file = file;
	DBG.offset = offset;
	DBG.info = "reading the DWARF of " + file.name + "…";
	if (key)
		await dbgForget(key); // a new file for this bzImage: the old one's units go
	const mine = DBG;
	const w = new Worker(withV("dwarfworker.js"), { type: "module" });
	DBG.worker = w;
	w.onmessage = (ev) => {
		if (mine !== DBG)
			return;
		const m = ev.data;
		if (m.type === "open") {
			if (!m.ok) {
				DBG.mode = null;
				DBG.why = m.why;
				w.terminate();
				DBG.worker = null;
			} else {
				dbgSetIndex(m, file.name);
				if (key)
					dbgIdbOp("readwrite", (s) => s.put({ index: m.index, units: m.units, versions: m.versions, file: file.name }, key + ":index"));
			}
			dbgChanged();
		} else if (m.type === "cfi") {
			dbgSetCfi(m);
		} else if (m.type === "unit") {
			DBG.tables.set(m.i, m.table);
			DBG.pending.delete(m.i);
			if (key)
				dbgIdbOp("readwrite", (s) => s.put(m.table, key + ":unit:" + m.i));
			dbgChanged();
		} else if (m.type === "lines") {
			DBG.lineCode.set(m.q, m.why ? { state: "none", why: m.why } : { state: "ok", path: m.path, code: m.code,
				how: m.units + " compile unit" + (m.units === 1 ? "" : "s") + " of the imported vmlinux name " + m.path });
			dbgChanged();
		} else if (m.type === "error") {
			if (m.q !== undefined) {
				DBG.lineCode.set(m.q, { state: "none", why: "could not read the line tables: " + m.message });
			} else if (m.i !== undefined) {
				DBG.tables.set(m.i, { failed: m.message });
				DBG.pending.delete(m.i);
			} else {
				DBG.mode = null;
				DBG.why = "could not read the DWARF of " + file.name + ": " + m.message;
			}
			dbgChanged();
		}
	};
	w.onerror = (e) => {
		if (mine !== DBG)
			return;
		DBG.mode = null;
		DBG.why = "the DWARF reader failed: " + (e.message || "worker error");
		dbgChanged();
	};
	w.postMessage({ type: "open", file, offset });
}

function dbgSetIndex(m, name) {
	DBG.index = m.index; // [[lo, hi, unit], ...] physical, sorted by lo
	DBG.maxUnit = m.index.reduce((x, [lo, hi]) => Math.max(x, hi - lo), 0);
	DBG.info = "DWARF " + m.versions.join("/") + " of the imported " + name + ": " + m.units + " compile units, each decoded when first needed" +
		(DBG.worker ? "" : " (remembered from an earlier import: units not decoded then need the file again)");
}

// "Forget": the units remembered for this bzImage go too.
async function dbgForget(key) {
	await dbgIdbOp("readwrite", (s) => {
		const r = s.getAllKeys(IDBKeyRange.bound(key + ":", key + ":\uffff"));
		r.onsuccess = () => r.result.forEach((k) => s.delete(k));
		return null;
	});
}

// After a reload: the index and the units decoded before, from IndexedDB.
async function dbgRestore(key) {
	dbgFresh("cached", "");
	const mine = DBG;
	const ix = key ? await dbgIdbOp("readonly", (s) => s.get(key + ":index")) : undefined;
	if (mine !== DBG)
		return;
	if (!ix) {
		DBG.mode = null;
		DBG.why = "import the unstripped vmlinux again to see its DWARF (file:line)";
		dbgChanged();
		return;
	}
	DBG.key = key;
	dbgSetIndex(ix, ix.file);
	const units = await dbgIdbOp("readonly", (s) => {
		const out = [];
		const c = s.openCursor(IDBKeyRange.bound(key + ":unit:", key + ":unit:\uffff"));
		c.onsuccess = () => {
			if (c.result) {
				out.push([Number(String(c.result.key).split(":").pop()), c.result.value]);
				c.result.continue();
			}
		};
		return { result: out };
	});
	if (mine !== DBG)
		return;
	for (const [i, t] of units || [])
		DBG.tables.set(i, t);
	const types = await dbgIdbOp("readonly", (s) => s.get(key + ":types"));
	if (mine !== DBG)
		return;
	if (types)
		DBG.types = types;
	const cfi = await dbgIdbOp("readonly", (s) => s.get(key + ":cfi"));
	if (mine !== DBG)
		return;
	if (cfi)
		DBG.cfi = { t: cfi.table, source: cfi.source };
	const locals = await dbgIdbOp("readonly", (s) => {
		const out = [];
		const c = s.openCursor(IDBKeyRange.bound(key + ":locals:", key + ":locals:\uffff"));
		c.onsuccess = () => {
			if (c.result) {
				out.push([Number(String(c.result.key).split(":").pop()), c.result.value]);
				c.result.continue();
			}
		};
		return { result: out };
	});
	if (mine !== DBG)
		return;
	for (const [i, g] of locals || [])
		DBG.localTables.set("u" + i, g);
	dbgChanged();
}

export { dbgImport, dbgForget, dbgRestore };
