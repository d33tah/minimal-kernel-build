"use strict";

// C source for the listing (objdump -S style) and live line coverage, from
// kernels/<name>/sources.json (site/gen-source.py; DWARF of a -g build
// whose code is byte-identical to the kernel that boots).

let sources = null;       // sources.json of the current kernel, or null
let srcPanel = null;      // { fn, file, first, last } shown in the side panel

async function loadSources(entry) {
	sources = null;
	if (!entry || !entry.sources)
		return;
	try {
		const resp = await fetch(withV(entry.sources));
		sources = resp.ok ? await resp.json() : null;
	} catch (e) {
		console.warn("sources:", e);
	}
}

const showSource = () => !!sources && $("show_src").checked;

// Location entry ([paddr, file, line, func, chain?]) in force at pa.
function srcLoc(pa) {
	if (!sources)
		return null;
	const loc = sources.loc;
	let a = 0, b = loc.length;
	while (a < b) { const m = (a + b) >> 1; if (loc[m][0] <= pa) a = m + 1; else b = m; }
	return a > 0 ? loc[a - 1] : null;
}

function srcText(f, l) {
	const rs = sources && sources.text[f];
	if (!rs)
		return null;
	for (const [first, lines] of rs)
		if (l >= first && l < first + lines.length)
			return lines[l - first];
	return null;
}

const srcName = (f, l) => sources.files[f].replace(/^.*\//, "") + ":" + l;

// A C line for the listing when pa starts a new source location; `state`
// keeps the last one printed in this listing.
function srcLineHtml(pa, state) {
	if (!showSource())
		return null;
	const loc = srcLoc(pa);
	if (!loc || loc[1] < 0)
		return null;
	const key = loc[1] + ":" + loc[2] + ":" + loc[3];
	if (key === state.key)
		return null;
	state.key = key;
	const text = srcText(loc[1], loc[2]);
	const inl = loc[4] && loc[4].length ? " (inlined into " + sources.funcs[loc[4][loc[4].length - 1][2]] + ")" : "";
	return '<span class="src" data-addr="' + pa + '" title="' + escHtml(sources.files[loc[1]] + ":" + loc[2] + inl) + '">' +
		escHtml(srcName(loc[1], loc[2])) + "  " + escHtml(text === null ? "" : text.trim()) + "</span>";
}

// "file.c:123 text" of the instruction at pa, or "".
function srcHere(pa) {
	const loc = srcLoc(pa);
	if (!loc || loc[1] < 0)
		return "";
	const t = srcText(loc[1], loc[2]);
	return srcName(loc[1], loc[2]) + (t ? "  " + t.trim() : "");
}

// Open the side panel with the whole function around the C line of pa.
function openSource(pa) {
	const loc = srcLoc(pa);
	if (!loc || loc[1] < 0)
		return;
	const r = sources.ranges.find(([fn, f, a, b]) => fn === loc[3] && f === loc[1] && loc[2] >= a - 2 && loc[2] <= b) ||
		[loc[3], loc[1], Math.max(1, loc[2] - 5), loc[2] + 5];
	srcPanel = { fn: r[0], file: r[1], first: Math.max(1, r[2] - 2), last: r[3], line: loc[2] };
	renderSourcePanel();
	$("src_panel").hidden = false;
	const cur = $("src_body").querySelector(".here");
	if (cur)
		$("src_body").scrollTop = cur.offsetTop - $("src_body").clientHeight / 3;
}

// Lines of the panel's file that have code, and those whose code has run
// (any instruction of that line, or of code inlined from it, executed).
function lineCoverage(file, first, last) {
	const code = new Set(), run = new Set();
	if (!disasm)
		return { code, run };
	const loc = sources.loc, ins = disasm.insns;
	const seen = stepper ? stepper.seen : null;
	let j = 0;
	for (let i = 0; i < ins.length; i++) {
		const pa = ins[i][0];
		while (j + 1 < loc.length && loc[j + 1][0] <= pa)
			j++;
		const e = loc[j];
		if (!e || e[0] > pa)
			continue;
		const frames = [[e[1], e[2]]].concat(e[4] || []);
		const done = seen && pa < seen.length * 8 && (seen[pa >> 3] & (1 << (pa & 7)));
		for (const [f, l] of frames)
			if (f === file && l >= first && l <= last) {
				code.add(l);
				if (done)
					run.add(l);
			}
	}
	return { code, run };
}

function renderSourcePanel() {
	if (!srcPanel || !sources)
		return;
	const { fn, file, first, last, line } = srcPanel;
	const { code, run } = lineCoverage(file, first, last);
	$("src_title").textContent = sources.funcs[fn] + " — " + sources.files[file] + ":" + first + "–" + last;
	const out = [];
	for (let l = first; l <= last; l++) {
		const t = srcText(file, l);
		if (t === null)
			continue;
		const cls = run.has(l) ? "exec" : code.has(l) ? "code" : "";
		const mark = run.has(l) ? "●" : code.has(l) ? "○" : " ";
		out.push('<span class="' + cls + (l === line ? " here" : "") + '">' + String(l).padStart(5, " ") + " " + mark + " " +
			escHtml(t) + "</span>");
	}
	const body = $("src_body");
	body.innerHTML = out.join("\n");
	body.dataset.code = code.size;
	body.dataset.executed = run.size;
	$("src_info").textContent = run.size + " of " + code.size + " lines with code have executed (● ran, ○ not yet)" +
		(stepper ? "" : "; boot a kernel to see coverage") + ". Recorded while stepping.";
}

function initSource() {
	$("src_close").addEventListener("click", () => { $("src_panel").hidden = true; srcPanel = null; });
	for (const id of ["listing", "context"])
		$(id).addEventListener("click", (ev) => {
			const line = ev.target.closest(".src");
			if (line) {
				ev.stopPropagation();
				openSource(Number(line.dataset.addr));
			}
		}, true);
	$("show_src").addEventListener("change", () => {
		inspect();
		renderContext();
	});
	setInterval(() => {
		if (srcPanel && !$("src_panel").hidden && (runState === "stepped"))
			renderSourcePanel();
	}, 1000);
}
