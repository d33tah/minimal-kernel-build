"use strict";

// Debug info (DWARF) of the current kernel: file:line and the inlined-at
// chain at a physical address, for the Debugger, the listings and the
// Graph tab. Two sources, one lookup (dwarf.js dwarfFrames):
//   shipped kernels  kernels/<name>/dwarf/ (site/gen-dwarf.js in CI), one
//                    shard per 64 KiB, fetched the first time an address
//                    in it is asked for
//   imported vmlinux dwarfworker.js decodes the compile unit that covers
//                    the address, off the UI thread; decoded units are kept
//                    in IndexedDB for this bzImage, so they survive a reload
//                    (the file itself does not: other units need it again)
// dbgFrames() answers synchronously from what is loaded and starts the
// load of the rest; dbgOnChange listeners re-render when it arrives.
// Backtraces (dbgUnwind) read the call frame information the same way:
// cfi-<hex>.bin shards, or the import's .debug_frame through the Worker.

const DBG_DB = "mkb-dwarf";
let DBG = { mode: null, why: "no debug info", listeners: [], version: 0 };

function dbgFresh(mode, why) {
	if (DBG.worker)
		DBG.worker.terminate();
	if (DBG.typesWorker)
		DBG.typesWorker.terminate();
	DBG = { mode, why, listeners: DBG.listeners, version: (DBG.version || 0) + 1, tables: new Map(), pending: new Set(),
		index: null, info: "", cfiTables: new Map(), cfiPending: new Set() };
}

function dbgChanged() {
	DBG.version++;
	if (DBG.timer)
		return;
	DBG.timer = setTimeout(() => {
		DBG.timer = null;
		for (const fn of DBG.listeners)
			try { fn(); } catch (e) { console.warn("dwarf listener:", e); }
	}, 30);
}

function dbgOnChange(fn) {
	DBG.listeners.push(fn);
}

// Whether the current kernel has DWARF at all (shipped or imported).
const dbgHas = () => DBG.mode === "shards" || DBG.mode === "import" || DBG.mode === "cached";

// A shipped kernel: kernels.json "dwarf" (a directory with index.json).
function dbgUseShards(entry) {
	if (!entry || !entry.dwarf) {
		dbgFresh(null, "no debug info for this kernel");
		return;
	}
	dbgFresh("shards", "");
	DBG.base = entry.dwarf.replace(/\/?$/, "/");
	DBG.indexLoad = fetch(withV(DBG.base + "index.json")).then((r) => r.ok ? r.json() : Promise.reject(new Error("HTTP " + r.status)))
		.then((ix) => {
			DBG.index = ix;
			DBG.shardSet = new Set(ix.shards);
			// A small kernel's few shards all load now: the Graph colours
			// its functions by their source directory from the start.
			if (ix.shards.length <= 4)
				ix.shards.forEach(dbgLoadShard);
			DBG.info = "DWARF " + ix.version.join("/") + " of " + ix.source + ", precomputed in CI: " + ix.shards.length +
				" shards of " + 2 ** ix.shardBits / 1024 + " KiB, loaded when needed";
			dbgChanged();
		}).catch((e) => {
			DBG.mode = null;
			DBG.why = "debug info did not load (" + e.message + ")";
			dbgChanged();
		});
}

function dbgLoadShard(s) {
	if (DBG.pending.has(s))
		return;
	DBG.pending.add(s);
	const mine = DBG;
	fetch(withV(DBG.base + s.toString(16) + ".bin")).then((r) => r.ok ? r.arrayBuffer() : Promise.reject(new Error("HTTP " + r.status)))
		.then((buf) => {
			if (mine !== DBG)
				return;
			DBG.tables.set(s, dwarfDecode(new Uint8Array(buf)));
			dbgChanged();
		}).catch((e) => {
			if (mine !== DBG)
				return;
			DBG.tables.set(s, { failed: e.message });
			dbgChanged();
		});
}

// ---- imported vmlinux ------------------------------------------------------

function dbgIdb() {
	return new Promise((resolve, reject) => {
		if (typeof indexedDB === "undefined")
			return reject(new Error("no IndexedDB"));
		const rq = indexedDB.open(DBG_DB, 1);
		rq.onupgradeneeded = () => rq.result.createObjectStore("kv");
		rq.onsuccess = () => resolve(rq.result);
		rq.onerror = () => reject(rq.error);
	});
}

async function dbgIdbOp(mode, fn) {
	try {
		const db = await dbgIdb();
		return await new Promise((resolve, reject) => {
			const tx = db.transaction("kv", mode);
			const rq = fn(tx.objectStore("kv"));
			tx.oncomplete = () => { db.close(); resolve(rq ? rq.result : undefined); };
			tx.onerror = tx.onabort = () => { db.close(); reject(tx.error); };
		});
	} catch (e) {
		return undefined; // no IndexedDB (private mode, quota): nothing remembered
	}
}

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
	const w = new Worker(withV("dwarfworker.js"));
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
		} else if (m.type === "error") {
			if (m.i !== undefined) {
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
	dbgChanged();
}

// ---- variables and types --------------------------------------------------

// A second Worker for the variables of an import: its pass over every
// unit does not hold up the file:line lookups of the first.
function dbgTypesWorker() {
	const mine = DBG, key = DBG.key;
	const w = new Worker(withV("dwarfworker.js"));
	DBG.typesWorker = w;
	const done = (types) => {
		w.terminate();
		if (mine !== DBG)
			return;
		DBG.types = types;
		if (key && !types.failed)
			dbgIdbOp("readwrite", (s) => s.put(types, key + ":types"));
		dbgChanged();
	};
	w.onmessage = (ev) => {
		const m = ev.data;
		if (m.type === "open")
			m.ok ? w.postMessage({ type: "types" }) : done({ failed: m.why });
		else if (m.type === "types-progress" && mine === DBG) {
			DBG.typesProgress = [m.done, m.total];
			dbgChanged();
		} else if (m.type === "types")
			done(m.types);
		else if (m.type === "error")
			done({ failed: m.message });
	};
	w.onerror = (e) => done({ failed: e.message || "worker error" });
	w.postMessage({ type: "open", file: DBG.file, offset: DBG.offset });
}

// { state: "ok", T } (dwarf.js DwarfTypes data), { state: "loading", why }
// or { state: "none", why }. Starts the load: the shipped types.json, or
// for an import one pass of the Worker over every compile unit.
function dbgTypes() {
	if (!dbgHas())
		return { state: "none", why: DBG.why };
	if (DBG.types)
		return DBG.types.failed ? { state: "none", why: "the variables did not load: " + DBG.types.failed } : { state: "ok", T: DBG.types };
	if (DBG.mode === "shards") {
		if (!DBG.index)
			return { state: "loading", why: "reading the debug info index…" };
		if (!DBG.index.types)
			return { state: "none", why: "this kernel's debug info has no variables (types.json)" };
		if (!DBG.typesLoad) {
			const mine = DBG;
			DBG.typesLoad = fetch(withV(DBG.base + DBG.index.types)).then((r) => r.ok ? r.json() : Promise.reject(new Error("HTTP " + r.status)))
				.then((T) => { if (mine === DBG) { DBG.types = T; dbgChanged(); } })
				.catch((e) => { if (mine === DBG) { DBG.types = { failed: e.message }; dbgChanged(); } });
		}
		return { state: "loading", why: "reading the types of " + DBG.index.variables + " variables (" +
			Math.round(DBG.index.typeBytes / 1024) + " KiB before compression)…" };
	}
	if (!DBG.file)
		return { state: "none", why: "the variables were not read before the reload: import the vmlinux again" };
	if (!DBG.typesWorker)
		dbgTypesWorker();
	const p = DBG.typesProgress;
	return { state: "loading", why: "reading the variables of the imported vmlinux: every compile unit once" +
		(p ? " (" + p[0] + " of " + p[1] + ")" : "") + "…" };
}

// ---- files and functions (the Coverage tab) ----------------------------------

// { state: "ok", F } with F the shipped files.json (gen-dwarf.js Coverage)
// plus lookups: fileIx (path -> index), fnAt (outermost ranges sorted by
// start: [lo, hi, function index]), fnLen (the longest), or the state of
// the load. Only shipped kernels have it: an imported vmlinux would need
// every line row of every unit decoded first.
function dbgFiles() {
	if (!dbgHas())
		return { state: "none", why: DBG.why };
	if (DBG.mode !== "shards")
		return { state: "none", why: "the coverage tree needs the list of every file's lines with code that CI precomputes for the kernels shipped here; an imported vmlinux is only read around the addresses shown" };
	if (!DBG.index)
		return { state: "loading", why: "reading the debug info index…" };
	if (!DBG.index.files)
		return { state: "none", why: "this kernel's debug info has no file list (files.json)" };
	if (DBG.files)
		return DBG.files.failed ? { state: "none", why: "the file list did not load: " + DBG.files.failed } : { state: "ok", F: DBG.files };
	if (!DBG.filesLoad) {
		const mine = DBG;
		DBG.filesLoad = fetch(withV(DBG.base + DBG.index.files)).then((r) => r.ok ? r.json() : Promise.reject(new Error("HTTP " + r.status)))
			.then((F) => {
				F.fileIx = new Map(F.files.map((f, k) => [f[0], k]));
				F.fnAt = [];
				F.funcs.forEach((fn, i) => { for (let k = 4; k < fn.length; k += 2) F.fnAt.push([fn[k], fn[k] + fn[k + 1], i]); });
				F.fnAt.sort((x, y) => x[0] - y[0]);
				F.fnLen = F.fnAt.reduce((m, r) => Math.max(m, r[1] - r[0]), 0);
				if (mine === DBG) { DBG.files = F; dbgChanged(); }
			})
			.catch((e) => { if (mine === DBG) { DBG.files = { failed: e.message }; dbgChanged(); } });
	}
	return { state: "loading", why: "reading the list of source files and functions…" };
}

// Index of the function (files.json funcs) whose code holds pa, or -1.
function dbgFuncAt(F, pa) {
	const r = F.fnAt;
	let a = 0, b = r.length;
	while (a < b) { const m = (a + b) >> 1; if (r[m][0] <= pa) a = m + 1; else b = m; }
	for (let q = a - 1; q >= 0 && r[q][0] >= pa - F.fnLen; q--)
		if (r[q][1] > pa)
			return r[q][2];
	return -1;
}

// Unit of the import covering pa, or -1.
function dbgUnitAt(pa) {
	const ix = DBG.index;
	let a = 0, b = ix.length;
	while (a < b) { const m = (a + b) >> 1; if (ix[m][0] <= pa) a = m + 1; else b = m; }
	for (let k = a - 1; k >= 0 && ix[k][0] >= pa - DBG.maxUnit; k--)
		if (pa < ix[k][1])
			return ix[k][2];
	return -1;
}

// ---- call frame information and backtraces -----------------------------------

// The CFI row (unwind.js cfiRow) at pa: a row, null (no FDE covers pa),
// { why } (no CFI at all, or it did not load) or "loading". Shipped
// kernels: the cfi-<hex>.bin shard of pa, fetched the first time; an
// import: the Worker parses .debug_frame (or .eh_frame) once, kept in
// IndexedDB like the units.
function dbgCfiRow(pa) {
	if (!dbgHas())
		return { why: DBG.why };
	if (DBG.mode === "shards") {
		if (!DBG.index)
			return "loading";
		const c = DBG.index.cfi;
		if (!c || c.why)
			return { why: c ? c.why : "this kernel's debug info has no call frame information" };
		const s = Math.floor(pa / 2 ** DBG.index.shardBits);
		if (!DBG.cfiSet)
			DBG.cfiSet = new Set(c.shards);
		if (!DBG.cfiSet.has(s))
			return null;
		const t = DBG.cfiTables.get(s);
		if (t)
			return t.failed ? { why: "the call frame information did not load: " + t.failed } : cfiRow(t, pa);
		if (!DBG.cfiPending.has(s)) {
			DBG.cfiPending.add(s);
			const mine = DBG;
			fetch(withV(DBG.base + "cfi-" + s.toString(16) + ".bin")).then((r) => r.ok ? r.arrayBuffer() : Promise.reject(new Error("HTTP " + r.status)))
				.then((buf) => { if (mine === DBG) { DBG.cfiTables.set(s, cfiDecode(new Uint8Array(buf))); dbgChanged(); } })
				.catch((e) => { if (mine === DBG) { DBG.cfiTables.set(s, { failed: e.message }); dbgChanged(); } });
		}
		return "loading";
	}
	if (DBG.cfi)
		return DBG.cfi.why ? { why: DBG.cfi.why } : cfiRow(DBG.cfi.t, pa);
	if (!DBG.worker)
		return { why: "the call frame information was not read before the reload: import the vmlinux again" };
	if (!DBG.cfiAsked) {
		DBG.cfiAsked = true;
		DBG.worker.postMessage({ type: "cfi" });
	}
	return "loading";
}

function dbgSetCfi(m) {
	DBG.cfi = m.why ? { why: m.why } : { t: m.table, source: m.source };
	if (DBG.key && !m.why)
		dbgIdbOp("readwrite", (s) => s.put({ table: m.table, source: m.source }, DBG.key + ":cfi"));
	dbgChanged();
}

// Where the CFI comes from, for the call stack's title.
function dbgCfiInfo() {
	if (DBG.mode === "shards" && DBG.index && DBG.index.cfi && !DBG.index.cfi.why)
		return DBG.index.cfi.source + " of " + DBG.index.source + " (" + DBG.index.cfi.fdes + " FDEs), precomputed in CI";
	if (DBG.cfi && DBG.cfi.t)
		return DBG.cfi.source + " of the imported vmlinux (" + DBG.cfi.t.lo.length + " FDEs)";
	return "";
}

// Backtrace at a pause (unwind.js unwind) from the registers r
// (stepper.regs()), reading guest memory through read32(va) and code
// through toPhys(va); symAt(pa) -> [name, start] or null; entryEsp(pa,
// esp) -> the ESP the recorded call into pa's function arrived with. Assembly entry
// code that calls C with pt_regs (interrupts, exceptions, int $0x80) is
// crossed through the saved registers; ret_from_fork and startup_32 are
// where a stack starts.
function dbgUnwind(r, read32, toPhys, symAt, entryEsp) {
	if (!dbgHas())
		return { frames: [], state: "none", why: DBG.why };
	const res = unwind(r, {
		rowAt: dbgCfiRow, read32, toPhys, symAt, entryEsp, framePointer: true,
		isEntry: (n) => /^(irq_entries_start|asm_|entry_INT80|entry_SYSENTER|nmi_stub|handle_exception$)/.test(n),
	});
	// The outermost code of a stack: no CFI, nothing calls it.
	const last = res.frames[res.frames.length - 1];
	const s = last ? symAt(last.lookup) : null;
	if (res.state === "stopped" && s && /^(ret_from_fork|ret_from_fork_asm|startup_32|startup_32_smp|start_cpu0)$/.test(s[0]))
		return { ...res, state: "ok", why: s[0] + " starts this stack" };
	return res;
}

// ---- lookup ------------------------------------------------------------------

// The table that holds pa: { state: "ok", t, end } (end: where the table's
// { state: "none", why }. Starts the load of a missing one unless peek.
// { state: "none", why }. Starts the load of a missing one.
function dbgTableAt(pa, peek) {
	if (!dbgHas())
		return { state: "none", why: DBG.why };
	if (!DBG.index)
		return { state: "loading" };
	let t, key, end = Infinity;
	if (DBG.mode === "shards") {
		key = Math.floor(pa / 2 ** DBG.index.shardBits);
		if (!DBG.shardSet.has(key))
			return { state: "none", why: "no DWARF here: not code of this kernel's vmlinux" };
		end = (key + 1) * 2 ** DBG.index.shardBits;
		t = DBG.tables.get(key);
		if (!t) {
			if (!peek)
				dbgLoadShard(key);
			return { state: "loading" };
		}
	} else {
		key = dbgUnitAt(pa);
		if (key < 0)
			return { state: "none", why: "no compile unit of the imported vmlinux covers this address" };
		t = DBG.tables.get(key);
		if (!t) {
			if (!DBG.worker)
				return { state: "none", why: "not decoded before the reload: import the vmlinux again to read its DWARF here" };
			if (!peek && !DBG.pending.has(key)) {
				DBG.pending.add(key);
				DBG.worker.postMessage({ type: "unit", i: key });
			}
			return { state: "loading" };
		}
	}
	if (t.failed)
		return { state: "none", why: "debug info did not load: " + t.failed };
	return { state: "ok", t, end };
}

// { state: "ok", frames } (dwarf.js dwarfFrames: innermost first),
// { state: "loading" } or { state: "none", why }. peek: only look in what
// is loaded.
function dbgFrames(pa, peek) {
	const r = dbgTableAt(pa, peek);
	if (r.state !== "ok")
		return r;
	const frames = dwarfFrames(r.t, pa);
	if (!frames)
		return { state: "none", why: "no line information here (padding or code without debug info)" };
	return { state: "ok", frames };
}

// The outermost function around pa and its line rows:
// { state: "ok", lo, hi, rows: [[a, b, frames], ...] } covering [lo, hi),
// or the state of what is missing (a function may span two shards).
function dbgFunction(pa) {
	const r = dbgTableAt(pa);
	if (r.state !== "ok")
		return r;
	const fn = dwarfFunction(r.t, pa);
	if (!fn)
		return { state: "none", why: "no DWARF function here (assembly or code without debug info)" };
	const [lo, hi] = fn;
	const rows = [];
	for (let a = lo; a < hi;) {
		const q = dbgTableAt(a);
		if (q.state !== "ok")
			return q;
		const k = dwarfRow(q.t, a);
		const b = Math.min(hi, q.end, k + 1 < q.t.la.length ? q.t.la[k + 1] : Infinity);
		if (!(b > a))
			break;
		rows.push([a, b, dwarfFrames(q.t, a) || []]);
		a = b;
	}
	return { state: "ok", lo, hi, rows };
}

// "init/main.c:73" or "no line information" (line 0 is code the compiler
// made up, e.g. spills and jumps shared by several lines).
const dbgWhere = (f) => f.file && f.line ? f.file + ":" + f.line : "no line information";

// One line per frame, innermost first, for titles and the Debugger.
function dbgChainText(frames, sym) {
	return frames.map((f, k) => (k ? "inlined into " : "") + (f.func || sym || "?") +
		(f.file && f.line ? " at " + dbgWhere(f) : " (no line information)")).join("\n");
}

// Tooltip text for the code at pa: "func at file:line" plus the inline chain.
function dbgTitle(pa, sym) {
	if (pa < 0)
		return "";
	const r = dbgFrames(pa);
	if (r.state === "loading")
		return "reading DWARF…";
	if (r.state === "none")
		return r.why;
	return dbgChainText(r.frames, sym);
}
