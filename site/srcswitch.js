"use strict";

// The Source panel's other files: the recent ones, the file lists the
// switcher offers and its moves (source.js opens a pick and draws it).

import { $ } from "./util.js";
import { stepper, image, runState, api } from "./page.js";
import { dbgFrames, dbgFiles, DBG } from "./dbginfo.js";
import { SRC } from "./srctext.js";

// Recently opened files, per kernel, newest first (localStorage; without
// it, until the page is reloaded).
const SRC_RECENT_KEY = "mkb-src-recent";
const SRC_RECENT_MAX = 8;
let srcRecentMem = {};

// A kernel rebuilt in the page (rebuild.js) is its source's: image.from.
const srcKernelKey = () => (image ? image.from || image.name : "");

function srcRecent() {
	let all = srcRecentMem;
	try {
		all = JSON.parse(localStorage.getItem(SRC_RECENT_KEY) || "{}") || {};
	} catch (e) {
		// no storage: this load's list
	}
	const l = all[srcKernelKey()];
	return Array.isArray(l) ? l.filter((p) => typeof p === "string") : [];
}

function srcRemember(path) {
	const l = [path, ...srcRecent().filter((p) => p !== path)].slice(0, SRC_RECENT_MAX);
	srcRecentMem[srcKernelKey()] = l;
	try {
		const all = JSON.parse(localStorage.getItem(SRC_RECENT_KEY) || "{}") || {};
		all[srcKernelKey()] = l;
		localStorage.setItem(SRC_RECENT_KEY, JSON.stringify(all));
	} catch (e) {
		// no storage: srcRecentMem
	}
}

// Every source file this kernel's debug info or source knows: path ->
// { k: files.json index or -1 }. Shipped kernels: the files of the line
// table (files.json) and the shipped source; an imported vmlinux: the
// C and assembly files of the picked source folder or .tar.
function srcKnownFiles() {
	const fl = dbgFiles();
	const key = [fl.state, DBG.version, SRC.have ? SRC.have.size : 0, SRC.entries ? SRC.entries.size : 0, SRC.prefix].join("\0");
	if (srcSw.known && srcSw.knownKey === key)
		return srcSw.known;
	const m = new Map();
	if (fl.state === "ok")
		fl.F.files.forEach(([p], k) => m.set(p, { k }));
	if (SRC.have)
		for (const p of SRC.have)
			if (!m.has(p))
				m.set(p, { k: -1 });
	if (SRC.entries) {
		const pre = SRC.prefix || "";
		for (const name of SRC.entries.keys())
			if (name.startsWith(pre) && /\.(c|h|S|s|lds|inc)$/.test(name) && !m.has(name.slice(pre.length)))
				m.set(name.slice(pre.length), { k: -1 });
	}
	srcSw.known = m;
	srcSw.knownKey = key;
	return m;
}

// Lines that ran and lines with code of a file, as the Source tab counts
// them (coverage.js covRun): { run: number or null (unknown yet), code }.
function srcFileCounts(info) {
	const fl = dbgFiles();
	if (fl.state !== "ok" || info.k < 0)
		return { run: null, code: null };
	const code = fl.F.files[info.k][1];
	const run = api.source.covRun(fl.F);
	return { run: run.tint && run.linesKnown ? run.fileRun[info.k].size : null, code, why: run.tint ? "" : run.why };
}

// The files of the call stack at a pause: EIP's inline chain and every
// unwound frame's (symview.js unwindHere), innermost first.
function srcStackFiles() {
	const out = [];
	if (!stepper || (runState !== "paused" && runState !== "stepped"))
		return out;
	const pcs = [stepper.nextPhys()];
	try {
		for (const f of api.symbols.unwindHere().frames)
			if (!f.user)
				pcs.push(api.symbols.insnStart(f.lookup));
	} catch (e) {
		// no unwind here: EIP's files only
	}
	for (const pc of pcs) {
		const r = pc >= 0 ? dbgFrames(pc) : { state: "none" };
		if (r.state === "ok")
			for (const f of r.frames)
				if (f.file && f.line && !out.includes(f.file))
					out.push(f.file);
	}
	return out;
}

// Fuzzy match of the filter q on a path: the score (higher is better) or
// -1. The basename counts most (exact, prefix, substring), then a substring
// of the path, then q's characters in order in the path (earlier, closer
// together and at word starts better).
function srcFuzzy(q, path) {
	const p = path.toLowerCase(), base = p.slice(p.lastIndexOf("/") + 1);
	if (base === q)
		return 1000;
	if (base.startsWith(q))
		return 900 - base.length;
	let i = base.indexOf(q);
	if (i >= 0)
		return 800 - i - base.length / 100;
	i = p.indexOf(q);
	if (i >= 0)
		return 700 - i / 10 - p.length / 100;
	let at = -1, score = 500;
	for (const c of q) {
		const j = p.indexOf(c, at + 1);
		if (j < 0)
			return -1;
		score -= j - at - 1;
		if (j === 0 || "/._-".includes(p[j - 1]))
			score += 2;
		at = j;
	}
	return score;
}

// The switcher (#src_switch under the panel's title): with the filter
// empty, the call stack's files, the recently opened ones, then every file;
// with a filter, the best matches; after a breadcrumb, one directory's
// subdirectories and files. Arrows move the active row, Enter opens it,
// Escape closes.
const SRC_SW_MAX = 300;   // rows of matches
const SRC_SW_ALL = 5000;  // rows of every file (Debian has about 2,600)
const srcSw = { q: "", dir: null, rows: [], at: 0, known: null, knownKey: "" };

function srcSwClose(focus = true) {
	if ($("src_switch").hidden)
		return;
	$("src_switch").hidden = true;
	$("src_files").setAttribute("aria-expanded", "false");
	$("src_q").setAttribute("aria-expanded", "false");
	if (focus)
		$("src_files").focus({ preventScroll: true });
}

function srcSwRows() {
	const known = srcKnownFiles();
	const rows = [];
	const file = (path, group) => rows.push({ path, group, info: known.get(path) || { k: -1 } });
	const q = srcSw.q.toLowerCase();
	if (q) {
		const hits = [];
		for (const p of known.keys()) {
			const s = srcFuzzy(q, p);
			if (s >= 0)
				hits.push([s, p]);
		}
		hits.sort((a, b) => b[0] - a[0] || a[1].localeCompare(b[1]));
		for (const [, p] of hits.slice(0, SRC_SW_MAX))
			file(p, "match");
		srcSw.more = Math.max(0, hits.length - SRC_SW_MAX);
		srcSw.none = !hits.length;
	} else if (srcSw.dir !== null) {
		const d = srcSw.dir, pre = d ? d + "/" : "";
		if (d)
			rows.push({ dir: d.includes("/") ? d.slice(0, d.lastIndexOf("/")) : "", group: "up" });
		const dirs = new Set(), files = [];
		for (const p of known.keys())
			if (p.startsWith(pre)) {
				const rest = p.slice(pre.length), k = rest.indexOf("/");
				if (k >= 0)
					dirs.add(pre + rest.slice(0, k));
				else
					files.push(p);
			}
		for (const x of [...dirs].sort())
			rows.push({ dir: x, group: "dir" });
		for (const p of files.sort())
			file(p, "dir");
		srcSw.more = 0;
		srcSw.none = false;
	} else {
		const recent = srcRecent();
		for (const p of srcStackFiles())
			file(p, "stack");
		for (const p of recent)
			file(p, "recent");
		const all = [...known.keys()].sort();
		for (const p of all.slice(0, SRC_SW_ALL))
			file(p, "all");
		srcSw.more = Math.max(0, all.length - SRC_SW_ALL);
		srcSw.none = !all.length;
	}
	return rows;
}

const SRC_SW_GROUPS = { stack: "Call stack (at EIP first)", recent: "Recently opened", all: "Every file", match: "Matches" };

function srcSwMove(to) {
	if (!srcSw.rows.length)
		return;
	srcSw.at = Math.min(Math.max(to, 0), srcSw.rows.length - 1);
	for (const li of $("src_list").querySelectorAll(".srcopt")) {
		const on = Number(li.dataset.i) === srcSw.at;
		li.classList.toggle("active", on);
		li.setAttribute("aria-selected", on);
		if (on)
			li.scrollIntoView({ block: "nearest" });
	}
	$("src_q").setAttribute("aria-activedescendant", "src_opt_" + srcSw.at);
}

export { srcKernelKey, srcRemember, srcFileCounts, srcSw, srcSwClose, srcSwRows, SRC_SW_GROUPS, srcSwMove };
