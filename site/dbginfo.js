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

const DBG_DB = "mkb-dwarf";
let DBG = { mode: null, why: "no debug info", listeners: [], version: 0 };

function dbgFresh(mode, why) {
	if (DBG.worker)
		DBG.worker.terminate();
	DBG = { mode, why, listeners: DBG.listeners, version: (DBG.version || 0) + 1, tables: new Map(), pending: new Set(),
		index: null, info: "" };
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
	dbgChanged();
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
