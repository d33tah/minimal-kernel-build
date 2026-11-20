"use strict";

// Debug info (DWARF) of the current kernel: file:line and the inlined-at
// chain at a physical address, for the Debugger, the listings and the
// Graph tab. Two sources, one lookup (dwarftables.js dwarfFrames):
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

import { withV } from "./util.js";
import { api, provide } from "./page.js";
import { dwarfFrames, dwarfFunction, dwarfRow, dwarfResolvePath,
	dwarfFileCode } from "./dwarftables.js";
import { dwFuncBase, dwFuncShard } from "./dwarffuncs.js";
import { dwarfLocalFrames, dwarfScopesAt } from "./dwarflocals.js";
import { cfiFrameInfo, cfiRow, unwind } from "./unwind.js";
import { mgRead, DbgMerge } from "./dbgmerge.js";

const DBG_DB = "mkb-dwarf";
let DBG = { mode: null, why: "no debug info", listeners: [], version: 0 };

function dbgFresh(mode, why) {
	if (DBG.worker)
		DBG.worker.terminate();
	if (DBG.typesWorker)
		DBG.typesWorker.terminate();
	DBG = { mode, why, listeners: DBG.listeners, version: (DBG.version || 0) + 1, tables: new Map(), pending: new Set(),
		index: null, info: "", cfiTables: new Map(), cfiPending: new Set(), localTables: new Map(), lineCode: new Map(), linesAsked: new Set(),
		funcTables: new Map(), frameInfo: new Map() };
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

// A debug info file of the shards' directory, decoded (dbgmerge.js mgRead:
// kind index, line, cfi, locals, funcs, types, files); for a kernel rebuilt
// from a kit without debug sections, made from both sources (DbgMerge).
const dbgGet = (kind, key = 0) => DBG.merge ? DBG.merge.get(kind, key) : mgRead(api.source.kfetch, DBG.base, DBG.index, kind, key);

// A shipped kernel: kernels.json "dwarf" (a directory with index.json).
// entry.under (genimport.js, a rebuild from a kit without debug sections):
// the shipped kernel's entry, whose DWARF covers the units not compiled
// here, moved to the rebuilt kernel's symbols entry.syms (dbgmerge.js).
function dbgUseShards(entry) {
	if (!entry || !entry.dwarf) {
		dbgFresh(null, "no debug info for this kernel");
		return;
	}
	dbgFresh("shards", "");
	DBG.base = entry.dwarf.replace(/\/?$/, "/");
	if (entry.under)
		DBG.merge = new DbgMerge(api.source.kfetch, DBG.base, entry.under, entry.syms);
	// Where the files come from: CI, or genimport.js for an imported vmlinux.
	DBG.made = entry.made || "precomputed in CI";
	// The index of this DBG only: when it arrives after another DWARF took
	// over (an import restored after a reload, dbgRestore), it is dropped;
	// it once became the restored import's index and info.
	const mine = DBG;
	DBG.indexLoad = dbgGet("index")
		.then((ix) => {
			if (mine !== DBG)
				return;
			DBG.index = ix;
			DBG.shardSet = new Set(ix.shards);
			// A small kernel's few shards all load now: the Graph colours
			// its functions by their source directory from the start.
			if (ix.shards.length <= 4)
				ix.shards.forEach(dbgLoadShard);
			DBG.info = "DWARF " + ix.version.join("/") + " of " + ix.source + ", " + DBG.made + ": " + ix.shards.length +
				" shards of " + 2 ** ix.shardBits / 1024 + " KiB, loaded when needed";
			dbgChanged();
		}).catch((e) => {
			if (mine === DBG) {
				DBG.mode = null;
				DBG.why = "debug info did not load (" + e.message + ")";
				dbgChanged();
			}
		});
}

function dbgLoadShard(s) {
	if (DBG.pending.has(s))
		return;
	DBG.pending.add(s);
	const mine = DBG;
	dbgGet("line", s)
		.then((t) => {
			if (mine !== DBG)
				return;
			DBG.tables.set(s, t);
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
// For the tests: a slower or wrapped one in its place.
const setDbgIdbOp = (f) => { dbgIdbOp = f; };

// ---- variables and types --------------------------------------------------

// A second Worker for the variables of an import: its pass over every
// unit does not hold up the file:line lookups of the first.
function dbgTypesWorker() {
	const mine = DBG, key = DBG.key;
	const w = new Worker(withV("dwarfworker.js"), { type: "module" });
	DBG.typesWorker = w;
	// It stays for the parameters and locals of a unit (dbgLocals): it
	// holds the type table their type ids refer to.
	const done = (types) => {
		if (types.failed)
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
		else if (m.type === "locals" && mine === DBG)
			dbgSetLocals(m.i, m.groups, m.base, m.added);
		else if (m.type === "error" && m.i !== undefined && mine === DBG) {
			DBG.localTables.set("u" + m.i, { failed: m.message });
			dbgChanged();
		} else if (m.type === "error")
			done({ failed: m.message });
	};
	w.onerror = (e) => done({ failed: e.message || "worker error" });
	w.postMessage({ type: "open", file: DBG.file, offset: DBG.offset });
}

// { state: "ok", T } (dwarftypes.js DwarfTypes data), { state: "loading", why }
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
			DBG.typesLoad = dbgGet("types")
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

// ---- parameters and locals ----------------------------------------------------

function dbgSetLocals(i, groups, base, added) {
	// The Worker's table got types the one here lacks (only when the types
	// pass and this unit disagree): append them where the ids fit.
	if (added && added.length && DBG.types && !DBG.types.failed) {
		if (DBG.types.types.length === base)
			DBG.types.types.push(...added);
		else
			for (const g of groups)
				for (const sc of g)
					for (const v of sc[5])
						if (v[1] >= base)
							v[1] = -1;
	}
	DBG.localTables.set("u" + i, groups);
	if (DBG.key)
		dbgIdbOp("readwrite", (s) => s.put(groups, DBG.key + ":locals:" + i));
	dbgChanged();
}

// The frames at pa (a frame's EIP, or a caller's call instruction) with
// their parameters and locals (dwarflocals.js dwarfLocalFrames, innermost
// first): { state: "ok", frames, T } with T the type table their type ids
// are in, or { state: "loading" | "none", why }.
function dbgLocals(pa) {
	const r = dbgLocalGroups(pa);
	return r.state === "ok" ? { state: "ok", frames: dwarfLocalFrames(dwarfScopesAt(r.G, pa)), T: r.T } : r;
}

// The scope groups (dwarflocals.js dwarfScopeGroups) with the code at pa:
// { state: "ok", G, T } or { state: "loading" | "none", why }. Shipped
// kernels: the locals-<hex>.json.gz shard of pa; an import: the unit of
// pa, from the Worker that read the variables (after it has), kept in
// IndexedDB.
function dbgLocalGroups(pa) {
	if (!dbgHas())
		return { state: "none", why: DBG.why };
	const ty = dbgTypes();
	if (ty.state !== "ok")
		return ty;
	let key, T = ty.T;
	if (DBG.mode === "shards") {
		const L = DBG.index.locals;
		if (!L)
			return { state: "none", why: "this kernel's debug info has no parameters and locals (locals-*.json.gz)" };
		const s = Math.floor(pa / 2 ** DBG.index.shardBits);
		// Code compiled in this page (a rebuild, dbgmerge.js): the files made
		// here, with their own type table.
		const own = DBG.merge && DBG.merge.isOwned(pa);
		if (own && !DBG.merge.ownT)
			return { state: "none", why: "the debug info made here has no variables" };
		if (own)
			T = DBG.merge.ownT;
		if (!DBG.localSet)
			DBG.localSet = new Set(L.shards);
		if (!(own ? DBG.merge.ownLocals : DBG.localSet).has(s))
			return { state: "ok", G: [], T };
		key = (own ? "o" : "s") + s;
		if (!DBG.localTables.has(key) && !DBG.localTables.has("pending" + key)) {
			DBG.localTables.set("pending" + key, true);
			const mine = DBG;
			dbgGet(own ? "ownLocals" : "locals", s)
				.then((x) => { if (mine === DBG) { DBG.localTables.set(key, x.fns); dbgChanged(); } })
				.catch((e) => { if (mine === DBG) { DBG.localTables.set(key, { failed: e.message }); dbgChanged(); } });
		}
	} else {
		const i = dbgUnitAt(pa);
		if (i < 0)
			return { state: "ok", G: [], T: ty.T };
		key = "u" + i;
		if (!DBG.localTables.has(key)) {
			if (!DBG.typesWorker || DBG.mode !== "import")
				return { state: "none", why: "the parameters and locals of this compile unit were not read before the reload: import the vmlinux again" };
			if (!DBG.localTables.has("pending" + key)) {
				DBG.localTables.set("pending" + key, true);
				DBG.typesWorker.postMessage({ type: "locals", i });
			}
		}
	}
	const g = DBG.localTables.get(key);
	if (!g)
		return { state: "loading", why: "reading the parameters and locals here…" };
	if (g.failed)
		return { state: "none", why: "the parameters and locals did not load: " + g.failed };
	return { state: "ok", G: g, T };
}

// ---- function facts ---------------------------------------------------------------

// The functions called name (an ELF symbol's GCC suffix dropped:
// foo.isra.0 is foo) with their facts from DW_TAG_subprogram (dwarf.js
// DwarfFuncs): { state: "ok", fns: [{ e, file }], T } with T the type
// table the entries' type ids are in, or { state: "loading" | "none",
// why }. Shipped kernels: the funcs-<k>.json.gz shard the name hashes to;
// an import: the facts the Worker collected with the variables.
function dbgFnFacts(name) {
	if (!dbgHas())
		return { state: "none", why: DBG.why };
	const ty = dbgTypes();
	if (ty.state !== "ok")
		return ty;
	const base = dwFuncBase(name || "");
	let F;
	// A function compiled in this page (a rebuild, dbgmerge.js): its facts
	// from the files made here, with their type table.
	const own = DBG.merge && DBG.merge.ownT && DBG.merge.ownFactsOf(base);
	if (own)
		return { state: "ok", fns: own, T: DBG.merge.ownT };
	if (DBG.mode === "shards") {
		const f = DBG.index.funcs;
		if (!f)
			return { state: "none", why: "this kernel's debug info has no function facts (funcs-*.json.gz)" };
		const k = dwFuncShard(base, f.shards);
		F = DBG.funcTables.get(k);
		if (!F) {
			DBG.funcTables.set(k, "pending");
			const mine = DBG;
			dbgGet("funcs", k)
				.then((x) => { if (mine === DBG) { DBG.funcTables.set(k, x); dbgChanged(); } })
				.catch((e) => { if (mine === DBG) { DBG.funcTables.set(k, { failed: e.message }); dbgChanged(); } });
		}
		if (F === undefined || F === "pending")
			return { state: "loading", why: "reading the facts of the functions…" };
	} else {
		F = ty.T.funcs;
		if (!F)
			return { state: "none", why: "the variables of this import were read by an older version of this page, without function facts: import the vmlinux again" };
	}
	if (F.failed)
		return { state: "none", why: "the function facts did not load: " + F.failed };
	if (!F.byName) {
		F.byName = new Map();
		for (const e of F.fns) {
			if (!F.byName.has(e[0]))
				F.byName.set(e[0], []);
			F.byName.get(e[0]).push(e);
		}
	}
	return { state: "ok", fns: (F.byName.get(base) || []).map((e) => ({ e, file: e[1] >= 0 ? F.files[e[1]] : null })), T: ty.T };
}

// Whether physical pa is in the out-of-line code of entry e.
const dbgFnHolds = (e, pa) => e[7].some((r) => { for (let k = 0; k < r.length; k += 2) if (pa >= r[k] && pa < r[k] + r[k + 1]) return true; return false; });

// The facts of name for code at pa: the function whose own code holds pa,
// else (an inlined frame) the one with an inlined copy inside the
// function `outer` holds pa in, else the only one of that name. { state:
// "ok", f: { e, file } or null, others: how many more share the name, T }.
function dbgFnFactsFor(name, pa, outer) {
	const r = dbgFnFacts(name);
	if (r.state !== "ok")
		return r;
	let f = r.fns.length === 1 ? r.fns[0] : null;
	if (!f && pa >= 0)
		f = r.fns.find((x) => dbgFnHolds(x.e, pa)) || null;
	if (!f && outer)
		f = r.fns.find((x) => x.e[8].some((c) => dbgFnHolds(outer.e, c))) || null;
	return { state: "ok", f, others: f ? r.fns.length - 1 : r.fns.length, T: r.T };
}

// The facts of every function of the inline chain at pa, innermost
// first: { state: "ok", list: [{ func, f, others, state, why }], T } (a
// frame without facts has f null and why), or the state of the line data.
function dbgFnFactsAt(pa) {
	const r = dbgFrames(pa);
	if (r.state !== "ok")
		return r;
	const list = [];
	let T = null, outer = null;
	for (let k = r.frames.length - 1; k >= 0; k--) {
		const fr = r.frames[k];
		if (!fr.func) {
			list.unshift({ func: null, f: null, state: "none", why: "no DWARF function here" });
			continue;
		}
		const x = dbgFnFactsFor(fr.func, k === r.frames.length - 1 ? pa : -1, outer);
		if (x.state !== "ok") {
			list.unshift({ func: fr.func, f: null, state: x.state, why: x.why });
			continue;
		}
		T = x.T;
		if (k === r.frames.length - 1)
			outer = x.f;
		list.unshift({ func: fr.func, f: x.f, others: x.others, state: x.f ? "ok" : "none",
			why: x.f ? "" : x.others ? x.others + " functions are called " + fr.func + " and none of them is known to be here" : "no DW_TAG_subprogram facts for " + fr.func });
	}
	return { state: "ok", list, T };
}

// The call frame information's view of the frame of the function whose
// FDE covers pa (unwind.js cfiFrameInfo): an object, null (no FDE),
// { why }, or "loading" (its CFI shard is being fetched).
function dbgCfiFrame(pa) {
	if (DBG.frameInfo.has(pa))
		return DBG.frameInfo.get(pa);
	const row = dbgCfiRow(pa);
	if (row === "loading")
		return "loading";
	if (!row || row.why)
		return row;
	const t = DBG.mode === "shards" ? DBG.cfiTables.get(Math.floor(pa / 2 ** DBG.index.shardBits)) : DBG.cfi.t;
	const info = cfiFrameInfo(t, pa);
	DBG.frameInfo.set(pa, info);
	return info;
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
		DBG.filesLoad = dbgGet("files")
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
			dbgGet("cfi", s)
				.then((t) => { if (mine === DBG) { DBG.cfiTables.set(s, t); dbgChanged(); } })
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
		return DBG.index.cfi.source + " of " + DBG.index.source + " (" + DBG.index.cfi.fdes + " FDEs), " + DBG.made;
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
	// The code of hot patches (hotpatch.jsx): their own table.
	if (DBG.patch && pa >= DBG.patch.lo && pa < DBG.patch.hi)
		return { state: "ok", t: DBG.patch.t, end: DBG.patch.hi };
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
	return { state: "ok", t: DBG.remap ? dbgRemapped(t) : t, end };
}

// Line numbers moved to the text the machine runs (E8): DBG.remap = { key,
// files: Map(path -> Int32Array new line of each old line, 0: changed) },
// set by hotpatch.jsx while a patched file is shown with its patched text
// (srctext.js SRC_OVER). Each table is copied once per remap with the line
// rows and call lines of those files moved; a line the patch changed or
// removed gets line 0 (its old code no longer runs: the old entry jumps to
// the copy).
function dbgSetRemap(files) {
	const key = files ? [...files].map(([p, m]) => p + ":" + m.length + ":" + m.join(",")).join(";") : "";
	if ((DBG.remap ? DBG.remap.key : "") === key)
		return;
	DBG.remap = files && files.size ? { key, files, copies: new WeakMap() } : null;
	DBG.lineCode = new Map();
	dbgChanged();
}

function dbgRemapped(t) {
	const R = DBG.remap;
	const had = R.copies.get(t);
	if (had)
		return had;
	const ids = new Map();
	t.strings.forEach((s, k) => { if (R.files.has(s)) ids.set(k, R.files.get(s)); });
	let out = t;
	if (ids.size) {
		const move = (m, l) => (l > 0 && l < m.length ? m[l] : l);
		const ll = t.ll.slice(), rl = t.rl.slice();
		for (let k = 0; k < ll.length; k++)
			if (ids.has(t.lf[k]))
				ll[k] = move(ids.get(t.lf[k]), ll[k]);
		for (let q = 0; q < rl.length; q++)
			if (ids.has(t.rf[q]))
				rl[q] = move(ids.get(t.rf[q]), rl[q]);
		out = { ...t, ll, rl };
	}
	R.copies.set(t, out);
	return out;
}

// { state: "ok", frames } (dwarftables.js dwarfFrames: innermost first),
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

// ---- line -> address ----------------------------------------------------------

// The code of a source file per line (dwarftables.js dwarfFileCode), for the
// Source panel's address list and "Run to file:line": { state: "ok", path,
// code: Map line -> { rows, calls, bps, noStmt }, how } or the state of the
// load. q: the file as shown or as typed ("main.c" if only one file is
// called that). Shipped kernels: the shards files.json lists for the file;
// an import: the Worker decodes every unit whose line program names it.
function dbgFileCode(q) {
	if (!dbgHas())
		return { state: "none", why: DBG.why };
	const hit = DBG.lineCode.get(q);
	if (hit)
		return hit;
	if (DBG.mode === "shards") {
		const fl = dbgFiles();
		if (fl.state !== "ok")
			return fl;
		const F = fl.F;
		if (!F.paths)
			F.paths = F.files.map((f) => f[0]);
		const p = dwarfResolvePath(F.paths, q);
		if (!p.path) {
			DBG.lineCode.set(q, { state: "none", why: p.why });
			return DBG.lineCode.get(q);
		}
		const size = 2 ** F.shardBits;
		const shards = F.files[F.fileIx.get(p.path)][2];
		const tables = shards.map((s) => [s * size, (s + 1) * size, dbgTableAt(s * size)]);
		const bad = tables.find((x) => x[2].state === "none");
		if (bad)
			return bad[2];
		const wait = tables.filter((x) => x[2].state !== "ok").length;
		if (wait)
			return { state: "loading", why: "reading the line tables of " + p.path + " (" + wait + " of " + shards.length + " shards to go)…" };
		const list = tables.map(([lo, hi, r]) => [lo, hi, r.t]);
		// Hot patches' copies of its functions (E8): Run to a line of the
		// patched text stops in the copy that runs it.
		const patched = DBG.patch && DBG.patch.t.strings.includes(p.path);
		if (patched)
			list.push([DBG.patch.lo, DBG.patch.hi, DBG.patch.t]);
		const res = { state: "ok", path: p.path, code: dwarfFileCode(list, p.path),
			how: "the line table shards that hold code of " + p.path + " (" + shards.length + ")" + (patched ? " and the hot patches' code" : "") };
		DBG.lineCode.set(q, res);
		return res;
	}
	if (!DBG.worker)
		return { state: "none", why: "the imported vmlinux is not open since the reload: import it again to find the code of a line" };
	if (!DBG.linesAsked.has(q)) {
		DBG.linesAsked.add(q);
		DBG.worker.postMessage({ type: "lines", q });
	}
	return { state: "loading", why: "reading the line tables of the compile units that name " + q + "…" };
}

// The code of path:line: { state: "ok", path, line, rows, calls, bps,
// noStmt, how } (see dwarftables.js dwarfFileCode) or loading / none.
function dbgLineCode(q, line) {
	const r = dbgFileCode(q);
	if (r.state !== "ok")
		return r;
	const e = r.code.get(line);
	if (!e)
		return { state: "none", path: r.path, line, why: r.path + ":" + line + " has no code in the line table (a comment, a declaration, or code the compiler left out or merged into another line)" };
	return { state: "ok", path: r.path, line, how: r.how, ...e };
}

// dbgLineCode once loaded (or timeoutMs passed).
async function dbgLineCodeWait(q, line, timeoutMs = 120000) {
	const t0 = Date.now();
	for (;;) {
		const r = dbgLineCode(q, line);
		if (r.state !== "loading" || Date.now() - t0 > timeoutMs)
			return r.state === "loading" ? { state: "none", why: "gave up " + r.why } : r;
		await new Promise((res) => setTimeout(res, 50));
	}
}

// "init/main.c:73" or "no line information" (line 0 is code the compiler
// made up, e.g. spills and jumps shared by several lines).
const dbgWhere = (f) => f.file && f.line ? f.file + ":" + f.line : "no line information";

// One line per frame, innermost first, for titles and the Debugger.
function dbgChainText(frames, sym) {
	return frames.map((f, k) => (k ? "inlined into " : "") + (f.func || sym || "?") +
		(f.file && f.line ? " at " + dbgWhere(f) : " (no line information)")).join("\n");
}

// Tooltip text for the code at pa: "func at file:line" plus the inline
// chain, then the facts of each function of it (fnfacts.js fnFactsText).
function dbgTitle(pa, sym) {
	if (pa < 0)
		return "";
	const r = dbgFrames(pa);
	if (r.state === "loading")
		return "reading DWARF…";
	if (r.state === "none")
		return r.why;
	const facts = dbgFnFactsAt(pa);
	const more = facts.state !== "ok" ? [facts.why || "reading the function facts…"] :
		facts.list.filter((x) => x.func).map((x) => x.f ? api.symbols.fnFactsText(x.f, facts.T) : x.func + ": " + (x.why || "reading the function facts…"));
	return dbgChainText(r.frames, sym) + (more.length ? "\n" + more.join("\n") : "");
}

provide({ dbgHas, dbgFrames });

export { DBG, dbgFresh, dbgChanged, dbgOnChange, dbgHas, dbgUseShards, dbgIdbOp, setDbgIdbOp, dbgTypes,
	dbgLocals, dbgLocalGroups, dbgFnFacts, dbgFnHolds, dbgFnFactsFor, dbgFnFactsAt, dbgCfiFrame, dbgFiles, dbgFuncAt,
	dbgCfiRow, dbgSetCfi, dbgCfiInfo, dbgUnwind, dbgTableAt, dbgFrames, dbgFunction, dbgFileCode, dbgLineCode,
	dbgLineCodeWait, dbgWhere, dbgChainText, dbgTitle, dbgSetRemap };
