"use strict";

// C source in the listings (objdump -S style), the Debugger's source rows
// and live line coverage. Which file:line (and inlined-at chain) an
// address belongs to comes from the kernel's DWARF (dbginfo.js); the text
// of those lines from kernels/<name>/sources.json (site/gen-source.py,
// shipped for the kernels built here: minified Linux and hello).

let sources = null;       // sources.json of the current kernel, or null
let srcFiles = null;      // path -> index in sources.files
let srcPanel = null;      // { fn, file, first, last, line } shown in the side panel

async function loadSources(entry) {
	sources = null;
	srcFiles = null;
	if (!entry || !entry.sources)
		return;
	try {
		const resp = await fetch(withV(entry.sources));
		sources = resp.ok ? await resp.json() : null;
	} catch (e) {
		console.warn("sources:", e);
	}
	if (sources)
		srcFiles = new Map(sources.files.map((f, i) => [f, i]));
}

const showSource = () => dbgHas() && $("show_src").checked;

// Text of line l of a file (a DWARF path), or null when not shipped.
function srcText(path, l) {
	const f = sources && srcFiles ? srcFiles.get(path) : undefined;
	const rs = f !== undefined && sources.text[f];
	if (!rs)
		return null;
	for (const [first, lines] of rs)
		if (l >= first && l < first + lines.length)
			return lines[l - first];
	return null;
}

const srcName = (f) => f.file && f.line ? f.file.replace(/^.*\//, "") + ":" + f.line : "no line information";

// A C line for the listing when pa starts a new source location; `state`
// keeps the last one printed in this listing.
function srcLineHtml(pa, state) {
	if (!showSource())
		return null;
	const r = dbgFrames(pa);
	if (r.state === "loading") {
		if (state.key === "loading")
			return null;
		state.key = "loading";
		return '<span class="src dim" data-addr="' + pa + '">reading DWARF…</span>';
	}
	if (r.state !== "ok") {
		// Inside the image's code but no line here (padding, assembly): say
		// so once after code that had a line.
		if (state.key === null || state.key === "none")
			return null;
		state.key = "none";
		return '<span class="src nol" data-addr="' + pa + '" title="' + escHtml(r.why) + '">(no line information)</span>';
	}
	const fr = r.frames;
	const f = fr[0];
	const key = fr.map((x) => x.func + "@" + x.file + ":" + x.line).join("<");
	if (key === state.key)
		return null;
	state.key = key;
	if (!f.file || !f.line)
		return '<span class="src nol" data-addr="' + pa + '" title="' + escHtml(dbgChainText(fr)) + '">(no line information' +
			(f.func ? " in " + escHtml(f.func) : "") + ")</span>";
	const text = srcText(f.file, f.line);
	const inl = fr.length > 1 ? " [" + escHtml(f.func || "?") + " inlined into " + escHtml(fr[fr.length - 1].func || "?") + "]" : "";
	return '<span class="src" data-addr="' + pa + '" title="' + escHtml(dbgChainText(fr)) + '">' +
		escHtml(srcName(f)) + "  " + (text === null ? '<span class="dim">' + escHtml(f.file) + "</span>" : escHtml(text.trim())) +
		inl + "</span>";
}

// "file.c:123 text" of the instruction at pa, or "".
function srcHere(pa) {
	const r = dbgFrames(pa);
	if (r.state !== "ok" || !r.frames[0].file || !r.frames[0].line)
		return "";
	const f = r.frames[0];
	const t = srcText(f.file, f.line);
	return srcName(f) + (t ? "  " + t.trim() : "");
}

// Open the side panel with the whole function around the C line of pa.
function openSource(pa) {
	const r = dbgFrames(pa);
	if (!sources || r.state !== "ok")
		return;
	const loc = r.frames[0];
	const file = srcFiles.get(loc.file);
	if (file === undefined || !loc.line)
		return;
	const fn = sources.funcs.indexOf(loc.func);
	const rg = sources.ranges.find(([f, fl, a, b]) => f === fn && fl === file && loc.line >= a - 2 && loc.line <= b) ||
		[fn, file, Math.max(1, loc.line - 5), loc.line + 5];
	srcPanel = { fn: loc.func || "?", file, path: loc.file, first: Math.max(1, rg[2] - 2), last: rg[3], line: loc.line };
	renderSourcePanel();
	$("src_panel").hidden = false;
	const cur = $("src_body").querySelector(".here");
	if (cur)
		$("src_body").scrollTop = cur.offsetTop - $("src_body").clientHeight / 3;
}

// Instructions of the build with a line of the panel (directly or through
// code inlined from it): [[pa, [line, ...]], ...]. Kept per panel.
function panelInsns() {
	if (srcPanel.insns && srcPanel.dbgVersion === DBG.version)
		return srcPanel.insns;
	const out = [];
	const { path, first, last } = srcPanel;
	for (const [pa] of disasm ? disasm.insns : []) {
		const r = dbgFrames(pa);
		if (r.state !== "ok")
			continue;
		const ls = [];
		for (const f of r.frames)
			if (f.file === path && f.line >= first && f.line <= last)
				ls.push(f.line);
		if (ls.length)
			out.push([pa, ls]);
	}
	srcPanel.insns = out;
	srcPanel.dbgVersion = DBG.version;
	return out;
}

// Lines of the panel's file that have code, and those whose code has run
// (any instruction of that line, or of code inlined from it, executed).
function lineCoverage() {
	const code = new Set(), run = new Set();
	for (const [pa, ls] of panelInsns()) {
		const done = stepper && stepper.executed(pa);
		for (const l of ls) {
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
	const { fn, file, path, first, last, line } = srcPanel;
	const { code, run } = lineCoverage();
	$("src_title").textContent = fn + " — " + path + ":" + first + "–" + last;
	const out = [];
	for (let l = first; l <= last; l++) {
		const t = srcText(path, l);
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
	body.dataset.file = file;
	$("src_info").textContent = run.size + " of " + code.size + " lines with code have executed (● ran, ○ not yet)" +
		(stepper ? "" : "; boot a kernel to see coverage") + ". Recorded while stepping.";
}

// The Debugger's "source" rows: the C line at EIP and, for inlined code,
// each function it was inlined into with the line of the call.
function renderSourceRows(box, phys, sym) {
	const row = document.createElement("div");
	row.id = "cur_src";
	const b = document.createElement("b");
	b.textContent = "source";
	row.append(b, " ");
	const r = phys >= 0 ? dbgFrames(phys) : { state: "none", why: "EIP is not mapped" };
	row.dataset.state = r.state;
	row.dataset.frames = JSON.stringify(r.state === "ok" ? r.frames : []);
	row.dataset.src = srcHere(phys);
	if (r.state === "loading")
		row.append("reading DWARF…");
	else if (r.state === "none")
		row.append(r.why);
	else {
		r.frames.forEach((f, k) => {
			const line = document.createElement("div");
			line.className = "frame" + (k ? " inl" : "");
			const where = dbgWhere(f);
			const t = f.file && f.line ? srcText(f.file, f.line) : null;
			const fn = f.func || sym || "?";
			const btn = symButton(k ? "inlined into " + fn + (f.file && f.line ? " at " + where : " (no line information)") : where + " in " + fn + (r.frames.length > 1 ? " (inlined)" : ""), -1,
				t !== null ? "Show the whole function with its executed lines" : f.file ? "The text of " + f.file + " is not shipped for this kernel" : "");
			if (k === 0 && t !== null) {
				btn.disabled = false;
				btn.addEventListener("click", () => openSource(phys));
			}
			line.append(btn);
			if (t !== null) {
				const s = document.createElement("span");
				s.className = "srctext mono";
				s.textContent = " " + t.trim();
				line.append(s);
			}
			row.append(line);
		});
	}
	box.append(row);
}

function initSource() {
	$("src_close").addEventListener("click", () => { $("src_panel").hidden = true; srcPanel = null; });
	for (const id of ["listing", "context"])
		$(id).addEventListener("click", (ev) => {
			const line = ev.target.closest(".src");
			if (line && sources) {
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
