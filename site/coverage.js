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
// way the Source panel counts it (dwarf.js dwarfEachLine).

const COV = { open: new Set(), sort: "path", q: "", run: null, tree: null, key: "", exporting: false };
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
function covRun(F) {
	const st = stepper;
	if (!st)
		return { tint: false, why: "boot a kernel to record what runs" };
	const cov = coverageFor(st.kernelLo);
	if (!cov.tint)
		return cov;
	const c = COV.run;
	if (c && c.F === F && c.st === st && c.unique === st.unique && c.ver === DBG.version)
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
		const t = dbgTableAt(s * size);
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
	COV.run = r;
	return r;
}

// Totals of a node: { lines, linesRun, ins, insRun } (linesRun null while
// the line rows are being read; *Run null without a record).
function covSum(T, run, node) {
	const F = T.F, s = { lines: 0, linesRun: 0, ins: 0, insRun: 0, files: 0 };
	const add = (x) => {
		s.lines += x.lines; s.ins += x.ins; s.files += x.files || 0;
		s.linesRun = s.linesRun === null || x.linesRun === null ? null : s.linesRun + x.linesRun;
		s.insRun = s.insRun === null || x.insRun === null ? null : s.insRun + x.insRun;
	};
	if (node.kind === "func") {
		const fn = F.funcs[node.i];
		return { lines: fn[2], ins: fn[3] || 0, linesRun: !run.tint ? null : !run.linesKnown && run.fnIns[node.i] ? null : run.fnRun[node.i] ? run.fnRun[node.i].size : 0,
			insRun: run.tint ? Math.min(fn[3] || 0, run.fnIns[node.i]) : null };
	}
	if (node.kind === "file") {
		if (!node.sum) {
			s.lines = F.files[node.i][1];
			s.files = 1;
			s.linesRun = run.tint && run.linesKnown ? run.fileRun[node.i].size : null;
			s.insRun = run.tint ? 0 : null;
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

function covCell(run, total, what) {
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
	const box = $("cov_tree"), info = $("cov_info");
	const fl = dbgFiles();
	box.dataset.state = fl.state;
	if (fl.state !== "ok") {
		box.textContent = "";
		$("cov_status").textContent = fl.why;
		info.textContent = "";
		return;
	}
	const F = fl.F, T = covTree(F), run = covRun(F);
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
	head.innerHTML = '<span class="ttog"></span><span class="tname">file or function</span><span class="covcell">lines run</span><span class="covcell">instructions run</span>';
	box.append(head);
	let rows = 0, more = 0;
	const row = (kind, path, depth, label, s, open, kids, onName, title) => {
		if (rows >= COV_MAX_ROWS) {
			more++;
			return;
		}
		rows++;
		const r = document.createElement("div");
		r.className = "trow covrow";
		Object.assign(r.dataset, { kind, path, depth, lines: s.lines, linesRun: s.linesRun ?? "", ins: s.ins, insRun: s.insRun ?? "" });
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
		r.append(tog, name, covCell(s.linesRun, s.lines, "lines with code"), covCell(s.insRun, s.ins, "instructions"));
		box.append(r);
	};
	const fnRows = (k, depth) => {
		const fns = order(T.fileFns[k].filter((i) => !q || fnMatch(i) || F.files[k][0].toLowerCase().includes(q)),
			(i) => covSum(T, run, { kind: "func", i }), (i) => COV.sort === "path" ? String(F.funcs[i][4]).padStart(12, "0") : F.funcs[i][0]);
		for (const i of fns) {
			const fn = F.funcs[i];
			row("func", F.files[k][0] + ":" + fn[0], depth, fn[0] + "()", covSum(T, run, { kind: "func", i }), false, false,
				() => jumpTo(fn[4]), fn[0] + " at " + hex(fn[4]) + " (" + (fn.length - 4) / 2 + (fn.length > 6 ? " parts" : " part") +
				"): the lines in " + F.files[k][0] + " and the instructions of its own code; where it was inlined into a caller, that code counts in the file's lines but not here. Tap to open it in the Page tab.");
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
				". Tap to open the whole file with its executed lines.");
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
		run.failed.length ? "Lines run unknown: " + run.failed[0] : "";
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

function covShow(visible) {
	if (visible)
		covRender();
}

function initCoverage() {
	$("cov_filter").addEventListener("input", () => { COV.q = $("cov_filter").value.trim(); covRender(); });
	$("cov_sort").addEventListener("change", () => { COV.sort = $("cov_sort").value; covRender(); });
	$("cov_export").addEventListener("click", covExport);
	dbgOnChange(() => {
		COV.tree = COV.tree && DBG.files === COV.tree.F ? COV.tree : null;
		if (COV.exporting)
			covExport();
		if (!$("panel_coverage").hidden)
			covRender();
	});
	// While the kernel is stepped, what ran grows.
	setInterval(() => {
		if ($("panel_coverage").hidden || !stepper)
			return;
		const k = stepper.unique + ":" + DBG.version;
		if (k !== COV.key) {
			COV.key = k;
			covRender();
		}
	}, 1000);
}
