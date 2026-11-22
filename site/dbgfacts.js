"use strict";
// The facts of functions (DW_TAG_subprogram: dwarf.js DwarfFuncs) from the
// current kernel's debug info (dbginfo.js), and the tooltip of the code at
// an address (the inline chain and each function's facts).

import { api } from "./page.js";
import { DBG, dbgHas, dbgGet, dbgChanged, dbgTypes, dbgFrames, dbgChainText } from "./dbginfo.js";
import { dwFuncBase, dwFuncShard } from "./dwarffuncs.js";

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

export { dbgFnFacts, dbgFnHolds, dbgFnFactsFor, dbgFnFactsAt, dbgTitle };
