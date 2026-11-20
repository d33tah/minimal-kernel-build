"use strict";

// The Coverage tab: every source file and function of the kernel with its
// lines with code and its instructions, and how many of them have executed
// in this boot. Totals come with the kernel (files.json, gen-dwarf.js), so
// the tree is complete before any line table is read. What ran is what
// the stepper recorded (Stepper.seen, the listing tint's record, with the
// same rule about v86's JIT: coverageFor); which lines that is comes from
// the line rows of the DWARF shards holding a recorded address, fetched as
// needed. A line counts as run when an instruction started in one of its
// address ranges, in any frame (the call line of inlined code too), the
// way the Source panel counts it (dwarftables.js dwarfEachLine).

import { $, num, hex } from "./util.js";
import { stepper, image, api, provide, state, tab } from "./page.js";
import { download } from "./graph.js";
import { dwarfEachLine } from "./dwarftables.js";
import { DBG, dbgTableAt, dbgFuncAt, dbgFiles, dbgOnChange } from "./dbginfo.js";
import { openSourceFile, srcPanel, renderSourcePanel } from "./source.js";
import { fnDetailEl } from "./fnfacts.js";
import { BM, bmMarks, bmStarEl, bmAddButton, bmDraftFn, bmDraftLine } from "./bookmarks.js";
import { srcFile, SRC_OVER } from "./srctext.js";
import { textHash, textKeep, textOf, lineMap, imageLabel, buildFamily } from "./buildmap.js";

// prev: what ran in earlier builds of the kernels shown (E8, covKeep), the
// newest last; cmp: the id of the one the tree is compared with ("": none).
const COV = { open: new Set(), sort: "path", q: "", run: null, tree: null, key: "", exporting: false, prev: [], cmp: "", cmpState: null };
const COV_PREV_MAX = 8;
const COV_MAX_ROWS = 1500;

// The directory tree of files.json, built once per kernel: nodes { name,
// path, dirs (Map), files (indices) }, fileFns: the functions of each file.
function covTree(F) {
	if (COV.tree && COV.tree.F === F)
		return COV.tree;
	const root = { name: "", path: "", dirs: new Map(), files: [] };
	F.files.forEach(([p], k) => {
		const parts = p.replace(/^\/+/, "").split("/");
		let n = root;
		for (let i = 0; i < parts.length - 1; i++) {
			if (!n.dirs.has(parts[i]))
				n.dirs.set(parts[i], { name: parts[i], path: parts.slice(0, i + 1).join("/"), dirs: new Map(), files: [] });
			n = n.dirs.get(parts[i]);
		}
		n.files.push(k);
	});
	const fileFns = F.files.map(() => []);
	F.funcs.forEach((fn, i) => { if (fn[1] >= 0) fileFns[fn[1]].push(i); });
	for (const l of fileFns)
		l.sort((x, y) => F.funcs[x][4] - F.funcs[y][4]);
	COV.tree = { F, root, fileFns };
	if (!COV.open.size)
		COV.open.add("");
	return COV.tree;
}

// What has run: per file and per function the lines that ran, per function
// the instruction starts recorded. Kept until the stepper records a new
// address or more debug info arrives.
// peek (covKeep): only the line tables already read, none fetched, not kept.
function covRun(F, peek = false) {
	const st = stepper;
	if (!st)
		return { tint: false, why: "boot a kernel to record what runs" };
	const cov = api.source.coverageFor(st.kernelLo);
	if (!cov.tint)
		return cov;
	const c = COV.run;
	if (!peek && c && c.F === F && c.st === st && c.unique === st.unique && c.ver === DBG.version)
		return c;
	const size = 2 ** F.shardBits;
	const r = { F, st, unique: st.unique, ver: DBG.version, tint: true, partial: cov.partial, fnIns: new Float64Array(F.funcs.length),
		fileRun: F.files.map(() => new Set()), fnRun: F.funcs.map(() => null), need: 0, waiting: 0, failed: [], inShards: 0 };
	F.funcs.forEach((fn, i) => {
		let n = 0;
		for (let k = 4; k < fn.length; k += 2)
			n += st.countIn(fn[k], fn[k] + fn[k + 1]);
		r.fnIns[i] = n;
	});
	for (const s of DBG.index.shards) {
		const n = st.countIn(s * size, (s + 1) * size);
		if (!n)
			continue;
		r.inShards += n;
		r.need++;
		const t = dbgTableAt(s * size, peek);
		if (t.state !== "ok") {
			if (t.state === "loading")
				r.waiting++;
			else
				r.failed.push(t.why);
			continue;
		}
		dwarfEachLine(t.t, s * size, (s + 1) * size, (a, b, frames) => {
			if (!st.countIn(a, b))
				return;
			const fi = dbgFuncAt(F, a);
			const own = fi >= 0 && F.funcs[fi][1] >= 0 ? F.files[F.funcs[fi][1]][0] : null;
			for (const f of frames) {
				if (!f.file || !f.line)
					continue;
				const k = F.fileIx.get(f.file);
				if (k !== undefined)
					r.fileRun[k].add(f.line);
				if (f.file === own)
					(r.fnRun[fi] = r.fnRun[fi] || new Set()).add(f.line);
			}
		});
	}
	r.linesKnown = !r.waiting && !r.failed.length;
	if (!peek)
		COV.run = r;
	return r;
}

// ---- earlier builds (E8) ------------------------------------------------------

// What ran in the kernel shown now, kept to compare the next build of its
// family with (rebuild.jsx calls it before it boots a build, and before
// Back to the shipped kernel): per file the lines that ran, per function
// (name@file) its own lines that ran, and the text each edited or hot
// patched file was counted on. From the line tables and texts already
// read (the Source tab reads those of the code that ran): fetching them
// here would hold the switch, and redraw a big kernel's tree for each one
// (Debian: thousands of files). unread: the shards with code that ran
// whose lines are not in the record. null when nothing was recorded.
function covKeep() {
	const img = image, fl = dbgFiles();
	if (fl.state !== "ok")
		return null;
	const run = covRun(fl.F, true);
	if (!run.tint)
		return null;
	const F = fl.F, b = img.build || null;
	const shown = new Map();
	for (const p of new Set([...Object.keys(b ? b.files : {}), ...SRC_OVER.keys()])) {
		const f = srcFile(p);
		if (f.state === "ok")
			shown.set(p, textKeep(f.lines.join("\n")));
	}
	const files = new Map(), fns = new Map();
	F.files.forEach(([p], k) => { if (run.fileRun[k].size) files.set(p, new Set(run.fileRun[k])); });
	F.funcs.forEach((fn, i) => { if (run.fnRun[i] && fn[1] >= 0) fns.set(fn[0] + "@" + F.files[fn[1]][0], new Set(run.fnRun[i])); });
	const rec = { id: "c" + Date.now().toString(36) + COV.prev.length, label: imageLabel(img), fam: buildFamily(img), at: Date.now(),
		files, fns, shown, base: b ? b.base || {} : {}, partial: run.partial, unread: run.waiting + run.failed.length,
		lines: [...files.values()].reduce((n, s) => n + s.size, 0) };
	COV.prev.push(rec);
	if (COV.prev.length > COV_PREV_MAX)
		COV.prev.shift();
	return rec;
}

// The comparison with COV.cmp's record, the earlier build's lines moved to
// the text shown now: { rec, files: Map(path -> Set), fns: Map(name@path ->
// Set), moved: [paths], waiting } (a line the edit changed becomes -line,
// which no line here matches), or null.
function covCmpState() {
	const rec = COV.cmp ? COV.prev.find((x) => x.id === COV.cmp) : null;
	if (!rec || rec.fam !== buildFamily()) {
		COV.cmpState = null;
		return null;
	}
	const key = rec.id + "|" + DBG.version + "|" + COV.prev.length;
	if (COV.cmpState && COV.cmpState.key === key && COV.cmpState.img === image)
		return COV.cmpState;
	const cur = image && image.build;
	const paths = new Set([...rec.shown.keys(), ...Object.keys(cur ? cur.files : {}), ...SRC_OVER.keys()]);
	const maps = new Map();
	let waiting = 0;
	for (const p of paths) {
		const oldH = rec.shown.get(p) || (cur && cur.base && cur.base[p]) || rec.base[p];
		const f = srcFile(p);
		if (f.state === "loading") {
			waiting++;
			continue;
		}
		// No text to diff with (it went, or the file is not shown): the same numbers.
		if (f.state !== "ok" || !oldH || textOf(oldH) === undefined)
			continue;
		const now = f.lines.join("\n");
		if (textHash(now) !== oldH)
			maps.set(p, lineMap(textOf(oldH), now));
	}
	const move = (p, set) => {
		const m = maps.get(p);
		return m ? new Set([...set].map((l) => (l < m.length && m[l] ? m[l] : -l))) : set;
	};
	const files = new Map(), fns = new Map();
	for (const [p, s] of rec.files)
		files.set(p, move(p, s));
	for (const [k, s] of rec.fns)
		fns.set(k, move(k.slice(k.indexOf("@") + 1), s));
	COV.cmpState = { key, img: image, rec, files, fns, moved: [...maps.keys()], waiting };
	return COV.cmpState;
}

// Lines of path that ran in the compared build, moved to this text (the
// Source panel's ◌), or null.
function covWas(path) {
	const c = covCmpState();
	return c ? c.files.get(path) || new Set() : null;
}

// Lines that ran there (was), only here (plus), only there (minus).
function covDelta(prev, now) {
	let plus = 0, minus = 0;
	for (const l of now)
		if (!prev.has(l))
			plus++;
	for (const l of prev)
		if (!now.has(l))
			minus++;
	return { was: prev.size, plus, minus };
}

// The "compare with" picker: the earlier builds of this family recorded.
function covCmpRender() {
	const fam = buildFamily();
	const recs = COV.prev.filter((x) => x.fam === fam);
	const box = $("cov_cmp_box"), sel = $("cov_cmp");
	box.hidden = !recs.length;
	if (COV.cmp && !recs.some((x) => x.id === COV.cmp))
		COV.cmp = "";
	const want = [["", "nothing"], ...recs.map((x) => [x.id, x.label + ", " + x.lines.toLocaleString("en") + " lines ran" + (x.partial ? " (incomplete)" : "")])];
	if ([...sel.options].map((o) => o.value + "\t" + o.textContent).join("\n") !== want.map((w) => w.join("\t")).join("\n")) {
		sel.textContent = "";
		for (const [v, t] of want)
			sel.add(new Option(t, v));
	}
	sel.value = COV.cmp;
	const c = covCmpState();
	$("cov_cmp_status").textContent = !c ? "" : c.waiting ? "Reading the text of " + c.waiting + " edited file" + (c.waiting === 1 ? "" : "s") + "…" :
		"Lines that ran in " + c.rec.label + " next to this build's: there, + only here, − only there" +
		(c.moved.length ? "; lines of " + c.moved.join(", ") + " moved to this build's text through the diff of the two texts" : "") + "." +
		(c.rec.unread ? " Incomplete: the line tables of " + c.rec.unread + " of its 64 KiB pieces of code that ran were not read before the switch " +
			"(the coverage tree reads them when shown), so their lines count as not run there." : "");
}

// Totals of a node: { lines, linesRun, ins, insRun } (linesRun null while
// the line rows are being read; *Run null without a record); with a
// comparison (E8) also cmp: { was, plus, minus } of the lines that ran.
function covSum(T, run, node, cs = COV.cmpState) {
	const F = T.F, s = { lines: 0, linesRun: 0, ins: 0, insRun: 0, files: 0 };
	const add = (x) => {
		s.lines += x.lines; s.ins += x.ins; s.files += x.files || 0;
		s.linesRun = s.linesRun === null || x.linesRun === null ? null : s.linesRun + x.linesRun;
		s.insRun = s.insRun === null || x.insRun === null ? null : s.insRun + x.insRun;
		if (x.cmp)
			s.cmp = s.cmp ? { was: s.cmp.was + x.cmp.was, plus: s.cmp.plus + x.cmp.plus, minus: s.cmp.minus + x.cmp.minus } : { ...x.cmp };
	};
	const known = cs && run.tint && run.linesKnown;
	if (node.kind === "func") {
		const fn = F.funcs[node.i];
		const own = known && fn[1] >= 0 ? covDelta(cs.fns.get(fn[0] + "@" + F.files[fn[1]][0]) || new Set(), run.fnRun[node.i] || new Set()) : null;
		return { lines: fn[2], ins: fn[3] || 0, linesRun: !run.tint ? null : !run.linesKnown && run.fnIns[node.i] ? null : run.fnRun[node.i] ? run.fnRun[node.i].size : 0,
			insRun: run.tint ? Math.min(fn[3] || 0, run.fnIns[node.i]) : null, cmp: own };
	}
	if (node.kind === "file") {
		if (!node.sum) {
			s.lines = F.files[node.i][1];
			s.files = 1;
			s.linesRun = run.tint && run.linesKnown ? run.fileRun[node.i].size : null;
			s.insRun = run.tint ? 0 : null;
			if (known)
				s.cmp = covDelta(cs.files.get(F.files[node.i][0]) || new Set(), run.fileRun[node.i]);
			for (const fi of T.fileFns[node.i]) {
				const x = covSum(T, run, { kind: "func", i: fi });
				s.ins += x.ins;
				if (s.insRun !== null)
					s.insRun += x.insRun;
			}
			node.sum = s;
		}
		return node.sum;
	}
	if (!node.sum) {
		for (const d of node.dirs.values())
			add(covSum(T, run, d));
		for (const k of node.files)
			add(covSum(T, run, covFileNode(k)));
		node.sum = s;
	}
	return node.sum;
}

const covFileNodes = new Map();
function covFileNode(k) {
	let n = covFileNodes.get(k);
	if (!n) {
		n = { kind: "file", i: k };
		covFileNodes.set(k, n);
	}
	return n;
}

function covClearSums(node) {
	node.sum = null;
	for (const d of node.dirs.values())
		covClearSums(d);
}

const covPct = (run, total) => run === null || !total ? null : run / total;

function covCell(run, total, what, cmp) {
	const cell = document.createElement("span");
	cell.className = "covcell";
	const bar = document.createElement("span");
	bar.className = "covbar";
	const fill = document.createElement("span");
	const p = covPct(run, total);
	fill.style.width = p === null ? "0" : Math.max(p > 0 ? 2 : 0, Math.round(100 * p)) + "%";
	bar.append(fill);
	const text = document.createElement("span");
	text.className = "mono";
	text.textContent = (run === null ? "–" : run.toLocaleString("en")) + "/" + total.toLocaleString("en") +
		(p === null ? "" : " " + (p > 0 && p < 0.01 ? "<1" : Math.floor(100 * p)) + "%");
	cell.title = !total ? "no " + what : run === null ? total.toLocaleString("en") + " " + what + "; what ran is not known (see below)" :
		run.toLocaleString("en") + " of " + total.toLocaleString("en") + " " + what + " have executed";
	cell.append(bar, text);
	if (cmp) {
		const was = document.createElement("span");
		was.className = "mono covwas";
		was.textContent = "there " + cmp.was.toLocaleString("en") + " +" + cmp.plus + " −" + cmp.minus;
		cell.title += "; in " + COV.cmpState.rec.label + " " + cmp.was + " ran, " + cmp.plus + " ran only here, " + cmp.minus + " only there";
		cell.append(was);
	}
	return cell;
}

// Sort keys: children of a directory (directories first, then files) and
// functions of a file.
const COV_SORTS = {
	path: null,
	run: (s) => -(covPct(s.linesRun, s.lines) ?? -1),
	notrun: (s) => -(s.lines - (s.linesRun || 0)),
	lines: (s) => -s.lines,
	ins: (s) => -(covPct(s.insRun, s.ins) ?? -1),
};

function covRender() {
	const box = $("cov_tree"), info = $("covtree_info");
	const fl = dbgFiles();
	box.dataset.state = fl.state;
	$("cov_export").disabled = fl.state !== "ok";
	if (fl.state !== "ok") {
		box.textContent = "";
		$("cov_status").textContent = fl.why;
		info.textContent = "";
		return;
	}
	const F = fl.F, T = covTree(F), run = covRun(F);
	covCmpRender();
	covClearSums(T.root);
	for (const n of covFileNodes.values())
		n.sum = null;
	const q = COV.q.toLowerCase();
	const fnMatch = (i) => q && F.funcs[i][0].toLowerCase().includes(q);
	const fileMatch = (k) => !q || F.files[k][0].toLowerCase().includes(q) || T.fileFns[k].some(fnMatch);
	const dirMatch = (d) => !q || d.files.some(fileMatch) || [...d.dirs.values()].some(dirMatch);
	const key = COV_SORTS[COV.sort];
	const order = (items, sum, name) => key ? items.sort((x, y) => key(sum(x)) - key(sum(y)) || name(x).localeCompare(name(y))) :
		items.sort((x, y) => name(x).localeCompare(name(y)));

	box.textContent = "";
	const head = document.createElement("div");
	head.className = "trow thead covrow";
	head.innerHTML = '<span class="ttog"></span><span class="tname">file or function</span><span class="covcell">lines run</span><span class="covcell">instructions run</span><span class="covbm"></span>';
	box.append(head);
	let rows = 0, more = 0;
	const marks = BM.list.length ? bmMarks() : null;
	const row = (kind, path, depth, label, s, open, kids, onName, title, bm) => {
		if (rows >= COV_MAX_ROWS) {
			more++;
			return;
		}
		rows++;
		const r = document.createElement("div");
		r.className = "trow covrow";
		Object.assign(r.dataset, { kind, path, depth, lines: s.lines, linesRun: s.linesRun ?? "", ins: s.ins, insRun: s.insRun ?? "" },
			s.cmp ? { was: s.cmp.was, plus: s.cmp.plus, minus: s.cmp.minus } : {});
		const tog = document.createElement("button");
		tog.type = "button";
		tog.className = "ttog";
		tog.textContent = kids ? (open ? "▾" : "▸") : "·";
		tog.disabled = !kids;
		tog.setAttribute("aria-label", (open ? "collapse " : "expand ") + (path || "all files"));
		const toggle = () => {
			if (COV.open.has(path)) COV.open.delete(path); else COV.open.add(path);
			COV.key = "";
			covRender();
		};
		tog.addEventListener("click", toggle);
		const name = document.createElement("button");
		name.type = "button";
		name.className = "tname mono covname";
		name.style.paddingLeft = Math.min(depth, 20) * 0.6 + "em";
		name.textContent = label;
		name.title = title;
		name.addEventListener("click", onName || toggle);
		r.append(tog, name, covCell(s.linesRun, s.lines, "lines with code", s.cmp), covCell(s.insRun, s.ins, "instructions"));
		// A file or function: ★ once bookmarked, and its ☆ (K1).
		const bmc = document.createElement("span");
		bmc.className = "covbm";
		if (bm) {
			Object.assign(r.dataset, bm.kind === "fn" ? { pa: bm.t.pa, fn: bm.t.name } : {});
			const names = marks && (bm.kind === "fn" ? marks.fn.get(bm.t.pa) : marks.file.get(bm.t.path));
			if (names)
				bmc.append(bmStarEl(names));
			bmc.append(bmAddButton(() => bm, "Bookmark " + (bm.kind === "fn" ? bm.t.name + "()" : bm.t.path)));
		}
		r.append(bmc);
		box.append(r);
	};
	const fnRows = (k, depth) => {
		const fns = order(T.fileFns[k].filter((i) => !q || fnMatch(i) || F.files[k][0].toLowerCase().includes(q)),
			(i) => covSum(T, run, { kind: "func", i }), (i) => COV.sort === "path" ? String(F.funcs[i][4]).padStart(12, "0") : F.funcs[i][0]);
		for (const i of fns) {
			const fn = F.funcs[i];
			const path = F.files[k][0] + ":" + fn[0];
			const open = COV.open.has(path);
			row("func", path, depth, fn[0] + "()", covSum(T, run, { kind: "func", i }), open, true,
				() => api.show.jumpTo(fn[4]), fn[0] + " at " + hex(fn[4]) + " (" + (fn.length - 4) / 2 + (fn.length > 6 ? " parts" : " part") +
				"): the lines in " + F.files[k][0] + " and the instructions of its own code; where it was inlined into a caller, that code counts in the file's lines but not here. Tap to open it in the Page tab; the arrow shows its prototype, declaration, linkage, inlined copies and frame.",
				bmDraftFn(fn[4], fn[0]));
			// Its facts from DW_TAG_subprogram under the row.
			if (open && rows <= COV_MAX_ROWS) {
				const d = fnDetailEl(fn[0], fn[4], "cov");
				d.classList.add("covfacts");
				d.style.paddingLeft = Math.min(depth + 1, 20) * 0.6 + 2 + "em";
				d.dataset.path = path;
				box.append(d);
			}
		}
	};
	const dirRows = (d, depth) => {
		const kids = [...d.dirs.values()].filter(dirMatch).map((x) => ({ d: x })).concat(d.files.filter(fileMatch).map((k) => ({ k })));
		const sum = (x) => covSum(T, run, x.d || covFileNode(x.k));
		const name = (x) => x.d ? x.d.name : F.files[x.k][0];
		const dirs = order(kids.filter((x) => x.d), sum, name), files = order(kids.filter((x) => !x.d), sum, name);
		for (const x of dirs) {
			const open = !!q || COV.open.has(x.d.path);
			const s = sum(x);
			row("dir", x.d.path, depth, x.d.name + "/", s, open, true, null, x.d.path + "/: " + s.files + " files with code");
			if (open)
				dirRows(x.d, depth + 1);
		}
		for (const x of files) {
			const p = F.files[x.k][0];
			const fns = T.fileFns[x.k].length;
			const open = (q && T.fileFns[x.k].some(fnMatch)) || COV.open.has(p);
			row("file", p, depth, p.replace(/^.*\//, ""), sum(x), open, fns > 0, () => openSourceFile(p),
				p + ": " + fns + " functions" + (fns ? "" : " (its code is inlined into functions of other files, or assembly)") +
				". Tap to open the whole file with its executed lines.", bmDraftLine(p, 0));
			if (open)
				fnRows(x.k, depth + 1);
		}
	};
	const all = covSum(T, run, T.root);
	row("root", "", 0, "all " + all.files.toLocaleString("en") + " files", all, COV.open.has("") || !!q, true, null, "every source file the DWARF line table names");
	if (COV.open.has("") || q)
		dirRows(T.root, 1);
	if (more) {
		const m = document.createElement("p");
		m.className = "note";
		m.textContent = more.toLocaleString("en") + " more rows: narrow the filter or close some directories.";
		box.append(m);
	}
	box.dataset.rows = rows;
	box.dataset.coverage = !run.tint ? "none" : run.partial ? "partial" : "stepped";

	$("cov_status").textContent = !run.tint ? "No execution coverage: " + run.why + ". Totals only." :
		run.waiting ? "Reading the line tables of " + run.waiting + " of the " + run.need + " shards (64 KiB of code each) with executed code…" :
		run.failed.length ? "Lines run unknown: " + run.failed[0] :
		run.partial ? "Incomplete: v86's JIT has run kernel code since the entry (full speed, or a Run to with the JIT) and records nothing, " +
			"so code shown as not run may have run. Step, or run at a stepped speed (1k/s ... 10M/s), to record." : "";
	const ins = F.insns;
	info.textContent = "Lines: lines with code in the DWARF line table (a line inlined somewhere and the line of that call both count), " +
		"run when an instruction started in one of their address ranges while stepping. Instructions: " +
		(ins ? ins.tool + " of the -g build, " + num(ins.inFunctions) + " in DWARF functions, " + num(ins.total - ins.inFunctions) +
			" outside them (assembly, padding) not in the tree" : "not counted (" + F.insnsWhy + ")") +
		"; run = addresses where the stepper started an instruction (code that ran at the same address before the kernel was there counts too)." +
		(run.tint && run.partial ? " v86's JIT has run since the kernel entry and records nothing, so code shown as not run may have run." : "");
}

// lcov tracefile of what ran: every file's lines with code (DA:line,1 ran
// at least once, 0 not; no counts). Needs every shard's line rows.
function covExport() {
	const fl = dbgFiles();
	if (fl.state !== "ok")
		return;
	const F = fl.F, size = 2 ** F.shardBits, shards = DBG.index.shards;
	const tables = shards.map((s) => [s, dbgTableAt(s * size)]);
	const waiting = tables.filter(([, r]) => r.state === "loading").length;
	if (waiting) {
		COV.exporting = true;
		$("cov_export_status").textContent = "lcov: reading " + waiting + " of " + shards.length + " shards…";
		return;
	}
	COV.exporting = false;
	const run = covRun(F);
	const st = run.tint ? stepper : null;
	const lines = F.files.map(() => new Map());
	for (const [s, r] of tables)
		if (r.state === "ok")
			dwarfEachLine(r.t, s * size, (s + 1) * size, (a, b, frames) => {
				const ran = !!st && st.countIn(a, b) > 0;
				for (const f of frames) {
					const k = f.file && f.line ? F.fileIx.get(f.file) : undefined;
					if (k !== undefined)
						lines[k].set(f.line, lines[k].get(f.line) || ran);
				}
			});
	const out = ["TN:" + (image ? image.name : "kernel")];
	F.files.forEach(([p], k) => {
		const ls = [...lines[k]].sort((x, y) => x[0] - y[0]);
		out.push("SF:" + p, ...ls.map(([l, ran]) => "DA:" + l + "," + (ran ? 1 : 0)), "LF:" + ls.length, "LH:" + ls.filter((x) => x[1]).length, "end_of_record");
	});
	download((image ? image.name : "kernel") + "-coverage.info", out.join("\n") + "\n", "text/plain");
	$("cov_export_status").textContent = "lcov: " + F.files.length + " files" + (st ? "" : " (nothing recorded: every line 0)");
}

function initCoverage() {
	$("cov_filter").addEventListener("input", () => { COV.q = $("cov_filter").value.trim(); covRender(); });
	$("cov_sort").addEventListener("change", () => { COV.sort = $("cov_sort").value; covRender(); });
	$("cov_cmp").addEventListener("change", () => {
		COV.cmp = $("cov_cmp").value;
		covRender();
		if (srcPanel && !$("src_panel").hidden)
			renderSourcePanel();
	});
	$("cov_export").addEventListener("click", covExport);
	// The tree is drawn when the Source tab is shown (showTab).
	state.subscribe(() => {
		if (tab === "source")
			covRender();
	}, ["tabShows"]);
	dbgOnChange(() => {
		COV.tree = COV.tree && DBG.files === COV.tree.F ? COV.tree : null;
		if (COV.exporting)
			covExport();
		if (!$("panel_source").hidden)
			covRender();
	});
	// While the kernel is stepped, what ran grows.
	setInterval(() => {
		if ($("panel_source").hidden || !stepper)
			return;
		const k = stepper.unique + ":" + DBG.version;
		if (k !== COV.key) {
			COV.key = k;
			covRender();
		}
	}, 1000);
}

provide({ covRun, covRender, covWas });
export { COV, covTree, covRender, covKeep, covWas, initCoverage };
