"use strict";

// C source in the listings (objdump -S style), the Debugger's source rows
// and the source panel with live line coverage. Which file:line (and
// inlined-at chain) an address belongs to comes from the kernel's DWARF
// (dbginfo.js); the text of those lines from one of:
//   shipped  kernels/<name>/src/ (site/gen-src.js in CI): index.json and
//            <path>.gz per file the DWARF names, fetched the first time a
//            line of it is shown
//   picked   a source folder or .tar the user picked (an imported vmlinux,
//            or a kernel without shipped source): read from the File the
//            first time a line of it is shown; for a vmlinux whose files
//            genimport.js made, it makes src/ from the pick and the kernel
//            reads that as shipped
// srcFile() answers synchronously from what is loaded and starts the load
// of the rest; dbgChanged() re-renders when it arrives.

let SRC = { mode: null, why: "no C source for this kernel", files: new Map() };
let srcPanel = null;      // { pa, fn, path, line } shown in the side panel

function srcFresh(mode, why) {
	SRC = { mode, why, files: new Map(), missing: {}, notes: {} };
}

// A shipped kernel: kernels.json "src" (a directory with index.json).
async function srcUseShipped(entry) {
	if (!entry || !entry.src) {
		srcFresh(null, "the C source is not shipped for this kernel");
		return;
	}
	srcFresh("shipped", "");
	const mine = SRC;
	SRC.base = entry.src.replace(/\/?$/, "/");
	try {
		const r = await kfetch(SRC.base + "index.json");
		if (!r.ok)
			throw new Error("HTTP " + r.status);
		const ix = await r.json();
		if (mine !== SRC)
			return;
		SRC.have = new Set(ix.files);
		SRC.missing = ix.missing || {};
		SRC.notes = ix.notes || {};
		SRC.info = ix.origin;
	} catch (e) {
		if (mine !== SRC)
			return;
		srcFresh(null, "the C source did not load (" + e.message + ")");
	}
	srcChanged();
}

function srcChanged() {
	if (typeof dbgChanged === "function")
		dbgChanged();
	srcStatus();
}

const isGzip = (b) => b.length > 2 && b[0] === 0x1f && b[1] === 0x8b;

async function srcDecode(buf) {
	return new TextDecoder().decode(await dbgGunzip(buf)).split("\n");
}

// { state: "ok", lines } (lines[0] is line 1), { state: "loading" } or
// { state: "none", why }.
function srcFile(path) {
	if (!path)
		return { state: "none", why: "no line information" };
	const got = SRC.files.get(path);
	if (got)
		return got;
	let load = null;
	if (SRC.mode === "shipped") {
		if (!SRC.have)
			return { state: "loading" };
		if (!SRC.have.has(path))
			return { state: "none", why: path + ": " + (SRC.missing[path] || "not shipped") };
		load = () => kfetch(SRC.base + path.split("/").map(encodeURIComponent).join("/") + ".gz")
			.then((r) => r.ok ? r.arrayBuffer() : Promise.reject(new Error("HTTP " + r.status)));
	} else if (SRC.mode === "picked") {
		if (!SRC.entries)
			return { state: "loading" };
		const f = srcPickedFile(path);
		if (!f)
			return { state: "none", why: path + " is not in " + SRC.name };
		load = () => f.arrayBuffer();
	} else
		return { state: "none", why: SRC.why };
	const mine = SRC;
	SRC.files.set(path, { state: "loading" });
	load().then(srcDecode).then((lines) => {
		if (mine === SRC)
			SRC.files.set(path, { state: "ok", lines });
	}, (e) => {
		if (mine === SRC)
			SRC.files.set(path, { state: "none", why: path + " did not load: " + e.message });
	}).then(() => mine === SRC && srcChanged());
	return { state: "loading" };
}

// Text of line l of a file (a DWARF path), or null when not (yet) there.
function srcText(path, l) {
	const f = srcFile(path);
	return f.state === "ok" && l >= 1 && l <= f.lines.length ? f.lines[l - 1] : null;
}

// ---- a source folder or .tar picked by the user ------------------------------

// The DWARF paths are relative to the kernel tree; the picked folder or
// tarball may hold the tree one level down (linux-6.1.176/...) or more: the
// prefix is found from the first path that matches and kept.
function srcPickedFile(path) {
	const all = SRC.entries;
	if (SRC.prefix === undefined) {
		for (const name of all.keys())
			if (name === path || name.endsWith("/" + path)) {
				SRC.prefix = name.slice(0, name.length - path.length);
				break;
			}
		if (SRC.prefix === undefined)
			return null;
	}
	return all.get(SRC.prefix + path) || null;
}

function srcPickFolder(files) {
	srcFresh("picked", "");
	SRC.name = "the folder " + (files[0] ? files[0].webkitRelativePath.split("/")[0] : "");
	SRC.entries = new Map([...files].map((f) => [f.webkitRelativePath || f.name, f]));
	SRC.info = SRC.name + " (" + SRC.entries.size.toLocaleString("en") + " files)";
	srcChanged();
	genSrcPicked();
}

// An uncompressed .tar: only the 512-byte headers are read (ustar, GNU long
// names, pax path records), each file is a slice of the File read when
// first shown. A compressed tarball would have to be decompressed whole.
async function srcPickTar(file) {
	srcFresh("picked", "");
	const mine = SRC;
	SRC.name = file.name;
	SRC.info = "indexing " + file.name + "…";
	srcStatus();
	const head = new Uint8Array(await file.slice(0, 512).arrayBuffer());
	if (isGzip(head) || (head[0] === 0xfd && head[1] === 0x37) || (head[0] === 0x42 && head[1] === 0x5a) || (head[0] === 0x28 && head[1] === 0xb5)) {
		srcFresh(null, file.name + " is compressed: unpack it and pick the folder, or pick the .tar (only the files shown are read from a .tar)");
		srcChanged();
		return;
	}
	const entries = new Map();
	const CHUNK = 1 << 20;
	let buf = new Uint8Array(0), bufAt = 0, off = 0, longName = null;
	const td = new TextDecoder();
	const str = (b, p, n) => { let e = p; while (e < p + n && b[e]) e++; return td.decode(b.subarray(p, e)); };
	try {
		while (off + 512 <= file.size) {
			if (off < bufAt || off + 512 > bufAt + buf.length) {
				bufAt = off;
				buf = new Uint8Array(await file.slice(off, Math.min(file.size, off + CHUNK)).arrayBuffer());
				if (mine !== SRC)
					return;
			}
			const p = off - bufAt;
			// End of archive: a zero block. A header's name field alone may
			// be empty: ustar puts a directory's whole path in the prefix
			// field when it is longer than 100 bytes.
			if (buf.subarray(p, p + 512).every((x) => x === 0))
				break;
			if (str(buf, p + 257, 5) !== "ustar" && !/^[0-7 ]+\0?$/.test(str(buf, p + 148, 8)))
				throw new Error("not a tar archive (no header at byte " + off + ")");
			const size = parseInt(str(buf, p + 124, 12).trim() || "0", 8);
			const type = String.fromCharCode(buf[p + 156] || 0x30);
			const data = off + 512;
			let name = str(buf, p, 100);
			const prefix = str(buf, p + 345, 155);
			if (prefix && str(buf, p + 257, 5) === "ustar")
				name = prefix + "/" + name;
			if (type === "L" || type === "x") {
				const meta = td.decode(new Uint8Array(await file.slice(data, data + size).arrayBuffer()));
				longName = type === "L" ? meta.replace(/\0.*$/s, "") : (/(?:^|\n)\d+ path=([^\n]*)\n/.exec(meta) || [])[1] || null;
			} else {
				if (longName)
					name = longName;
				longName = null;
				if (type === "0" || type === "\0" || type === "7")
					entries.set(name.replace(/^\.\//, ""), file.slice(data, data + size));
			}
			off = data + Math.ceil(size / 512) * 512;
		}
	} catch (e) {
		if (mine === SRC) {
			srcFresh(null, "could not read " + file.name + ": " + e.message);
			srcChanged();
		}
		return;
	}
	if (mine !== SRC)
		return;
	SRC.entries = entries;
	SRC.info = file.name + " (" + entries.size.toLocaleString("en") + " files)";
	srcChanged();
	genSrcPicked();
}

// The picker shows where there is DWARF but no shipped source (an imported
// vmlinux, a bzImage from disk with one).
function srcStatus() {
	const show = dbgHas() && SRC.mode !== "shipped";
	$("src_pick").hidden = !show;
	const el = $("src_status");
	el.dataset.state = SRC.mode === "picked" && SRC.entries ? "picked" : SRC.mode === "picked" ? "indexing" : "";
	el.textContent = !show ? "" : SRC.mode === "picked" ? "C source: " + SRC.info :
		"file:line only: pick the kernel's source folder or .tar for the text of the lines (" + SRC.why + ")";
}

// ---- statements spanning several lines --------------------------------------

// The line rows of a C statement written over several lines name only some
// of its lines, usually the first (`asm volatile("..."\n "..." : ...);`,
// a call with its arguments on the next lines): the others are parts of
// that statement, not lines without code. srcStatements(file) finds every
// statement of more than one line from the C text, once per loaded file:
// { of: Int32Array (line -> first line of its statement, 0 when the line
// holds no token of a multi-line statement), end: Map(first -> last line) }.
// A statement ends at `;`, `{` or `}` outside parentheses and brackets, after
// the `)` of an if/for/while/switch header, after a label's `:`, and after
// else/do, and outside functions before a line starting with a name after a
// `)` (macro calls without `;`); a `{` after `=` or `,`, or inside
// parentheses, is an initializer or a statement expression and part of it. Strings, characters, comments
// and preprocessor lines (with backslash continuations) are skipped.
function srcStatements(file) {
	if (file.stmts)
		return file.stmts;
	const text = file.lines.join("\n"), n = text.length;
	const of = new Int32Array(file.lines.length + 2), end = new Map();
	let line = 1, bol = true;           // at the start of a line (only blanks so far)
	let start = 0, last = 0, toks = 0, first = "", prev = "";
	let depth = 0, init = 0, header = false, seen = [], blocks = 0;
	const finish = () => {
		if (start && last > start) {
			for (const l of seen)
				of[l] = start;
			end.set(start, last);
		}
		start = last = toks = depth = init = 0;
		first = prev = "";
		header = false;
		seen = [];
	};
	const token = (t) => {
		if (!start)
			start = line;
		if (last !== line)
			seen.push(line);
		last = line;
		if (!toks++)
			first = t;
		prev = t;
	};
	const skipTo = (i, close) => {     // past the closing quote of a string or char
		for (i++; i < n && text[i] !== close; i++)
			if (text[i] === "\\")
				i++;
			else if (text[i] === "\n")
				break;
		return i;
	};
	for (let i = 0; i < n; i++) {
		const c = text[i];
		if (c === "\n") {
			line++;
			bol = true;
			continue;
		}
		if (c === " " || c === "\t" || c === "\r" || c === "\f" || c === "\v")
			continue;
		if (c === "/" && text[i + 1] === "/") {
			while (i + 1 < n && text[i + 1] !== "\n")
				i++;
			continue;
		}
		if (c === "/" && text[i + 1] === "*") {
			const e = text.indexOf("*/", i + 2), stop = e < 0 ? n : e + 2;
			for (let k = i; k < stop; k++)
				if (text[k] === "\n")
					line++;
			i = stop - 1;
			continue;
		}
		if (c === "#" && bol) {          // a preprocessor line and its continuations
			while (i + 1 < n && !(text[i + 1] === "\n" && text[i] !== "\\"))
				if (text[++i] === "\n")
					line++;
			continue;
		}
		bol = false;
		if (c === '"' || c === "'") {
			token(c);
			i = skipTo(i, c);
			continue;
		}
		if (/[A-Za-z0-9_]/.test(c)) {
			let j = i;
			while (j + 1 < n && /[A-Za-z0-9_]/.test(text[j + 1]))
				j++;
			const w = text.slice(i, j + 1);
			i = j;
			// Outside functions, macro calls without a `;` (DEFINE_X(a) on
			// one line, the next on the following one) are statements each.
			if (start && !blocks && !depth && !init && prev === ")" && last !== line)
				finish();
			token(w);
			if (toks === 1 && (w === "else" || w === "do"))
				finish();
			else if (toks === 1 && /^(if|for|while|switch)$/.test(w))
				header = true;
			continue;
		}
		const p = prev;
		token(c);
		if (c === "(" || c === "[")
			depth++;
		else if (c === ")" || c === "]") {
			depth = Math.max(0, depth - 1);
			if (c === ")" && header && !depth && !init)
				finish();
		} else if (c === "{") {
			if (depth || init || p === "=" || p === ",")
				init++;
			else {
				finish();
				blocks++;
			}
		} else if (c === "}") {
			if (init)
				init--;
			else {
				finish();
				blocks = Math.max(0, blocks - 1);
			}
		} else if (c === ";" && !depth && !init)
			finish();
		else if (c === ":" && !depth && !init && text[i + 1] !== ":" &&
			(first === "case" || first === "default" || (toks === 2 && /^[A-Za-z_]\w*$/.test(first))))
			finish();
	}
	finish();
	file.stmts = { of, end };
	return file.stmts;
}

// The panel's lines first..last widened to whole statements, and the
// continuation lines: those of a multi-line statement without code of
// their own, shown as the statement (ran when any of its lines ran). They
// are not counted: the counts here, in the Coverage tab and in the lcov
// export are lines with code of their own in the line table.
function srcStatementView(file, first, last, code, run) {
	const { of, end } = srcStatements(file);
	const max = file.lines.length;
	let lo = first, hi = Math.min(last, max);
	for (const l of code.keys())
		if (l >= first && l <= hi && of[l]) {
			lo = Math.min(lo, of[l]);
			hi = Math.max(hi, end.get(of[l]));
		}
	const cont = new Map(); // line -> { start, end, ran }
	const status = new Map(); // first line -> ran (true/false), only statements with code
	for (let l = lo; l <= hi; l++) {
		const st = of[l];
		if (!st || !code.has(l))
			continue;
		status.set(st, status.get(st) || run.has(l));
	}
	for (let l = lo; l <= hi; l++) {
		const st = of[l];
		if (st && !code.has(l) && status.has(st))
			cont.set(l, { start: st, end: end.get(st), ran: status.get(st) });
	}
	return { first: lo, last: hi, cont };
}

// ---- rendering --------------------------------------------------------------

const showSource = () => dbgHas() && $("show_src").checked;

const srcName = (f) => f.file && f.line ? f.file.replace(/^.*\//, "") + ":" + f.line : "no line information";

// Every view showing C lines colours the location (file:line, line number)
// with .srcloc and the C text with .srctext (style.css).
const srcLocHtml = (s) => '<span class="srcloc">' + escHtml(s) + "</span>";

function srcLocEl(s) {
	const e = document.createElement("span");
	e.className = "srcloc";
	e.textContent = s;
	return e;
}

// A file:line that opens the Source panel at that line (srcOpenAt), as an
// element or as HTML for the listings built as strings. Every view showing
// a C location outside the Source panel makes it with one of these two;
// initSource handles a click, Enter, Space or tap on any .srclink once for
// all of them. pa (-1: none) is an address whose code belongs to that line
// or to the function declared there. text defaults to the short name.
const srcLinkTitle = (path, line) => "Open " + path + ":" + line + " in the Source panel";

function srcLinkEl(path, line, text, pa = -1) {
	const b = document.createElement("button");
	b.type = "button";
	b.className = "srcloc srclink";
	b.textContent = text || srcName({ file: path, line });
	b.dataset.tip = srcLinkTitle(path, line);
	Object.assign(b.dataset, { file: path, line, pa });
	return b;
}

const srcLinkHtml = (path, line, text, pa = -1) => '<button type="button" class="srcloc srclink" data-file="' + escHtml(path) +
	'" data-line="' + line + '" data-pa="' + pa + '" data-tip="' + escHtml(srcLinkTitle(path, line)) + '">' + escHtml(text || srcName({ file: path, line })) + "</button>";

// Views are drawn again when DWARF or source text arrives and at every
// stop, which replaces their links. srcFocusKeep(draw) keeps the keyboard
// focus on the same file:line link (by its place among equal ones), and
// srcLinkAgain finds the drawn-again copy of a link marked by srcLinkMark.
const srcLinksLike = (a) => [...document.querySelectorAll(".srclink")].filter((e) =>
	e.dataset.file === a.dataset.file && e.dataset.line === a.dataset.line && e.dataset.pa === a.dataset.pa);

function srcLinkMark(a) {
	a.srcNth = srcLinksLike(a).indexOf(a);
}

function srcLinkAgain(a) {
	return !a || a.isConnected ? a : srcLinksLike(a)[a.srcNth] || null;
}

function srcFocusKeep(draw) {
	const a = document.activeElement;
	if (!a || !a.classList || !a.classList.contains("srclink"))
		return draw();
	srcLinkMark(a);
	const out = draw();
	const b = srcLinkAgain(a);
	if (b && b !== a)
		b.focus({ preventScroll: true });
	return out;
}

// Open the Source panel at path:line, highlighted and scrolled into view.
// Where pa's inline chain has that line (pa's own line, or the call site
// in a function it was inlined into), the panel shows that function with
// its executed lines; else the whole file (its lines with code from
// files.json); an imported vmlinux has no file list, so a function's
// declaration then opens in the function around pa.
function srcOpenAt(path, line, pa) {
	navJump(() => {
		const r = pa >= 0 ? dbgFrames(pa) : { state: "none" };
		const k = r.state === "ok" ? r.frames.findIndex((f) => f.file === path && f.line === line) : -1;
		if (k >= 0)
			openSource(pa, k);
		else if (pa >= 0 && r.state === "ok" && dbgFiles().state === "none") {
			openSource(pa);
			srcPanel.pick = srcPanel.scrollTo = line;
			renderSourcePanel();
		} else
			openSourceLine(path, line);
	});
}

// "func at file:line; inlined into ..." for pa as nodes, file:line a link.
function srcChainNodes(pa, sym) {
	const r = pa >= 0 ? dbgFrames(pa) : { state: "none", why: "" };
	if (r.state !== "ok")
		return [r.state === "loading" ? "reading DWARF…" : r.why];
	const out = [];
	r.frames.forEach((f, k) => {
		out.push((k ? "; inlined into " : "") + (f.func || sym || "?") + (f.file && f.line ? " at " : " (no line information)"));
		if (f.file && f.line)
			out.push(srcLinkEl(f.file, f.line, dbgWhere(f), pa));
	});
	return out;
}

// Why the text of frame f's line is not shown (title text), or "".
function srcWhyNot(f) {
	const s = srcFile(f.file);
	if (s.state === "loading")
		return "reading " + f.file + "…";
	if (s.state === "none")
		return s.why;
	return f.line > s.lines.length ? f.file + " has only " + s.lines.length + " lines" : "";
}

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
		return '<span class="src nol" data-addr="' + pa + '" data-tip="' + escHtml(r.why) + '">(no line information)</span>';
	}
	const fr = r.frames;
	const f = fr[0];
	const key = fr.map((x) => x.func + "@" + x.file + ":" + x.line).join("<");
	if (key === state.key)
		return null;
	state.key = key;
	if (!f.file || !f.line)
		return '<span class="src nol" data-addr="' + pa + '" data-tip="' + escHtml(dbgChainText(fr)) + '">(no line information' +
			(f.func ? " in " + escHtml(f.func) : "") + ")</span>";
	const text = srcText(f.file, f.line);
	const why = text === null ? srcWhyNot(f) : "";
	const inl = fr.length > 1 ? " [" + escHtml(f.func || "?") + " inlined into " + escHtml(fr[fr.length - 1].func || "?") + "]" : "";
	return '<span class="src" data-addr="' + pa + '" data-tip="' + escHtml(dbgChainText(fr) + (why ? "\n" + why : "")) + '">' +
		srcLinkHtml(f.file, f.line, srcName(f), pa) + "  " + (text === null ? '<span class="dim">' + escHtml(f.file) + "</span>" : '<span class="srctext">' + escHtml(text.trim()) + "</span>") +
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

// Open the side panel with the whole function around the C line of pa:
// frame k of its inline chain (0: the innermost, the line of pa itself;
// above: the call site in the function it was inlined into). A file
// without text opens too: the panel says why.
function openSource(pa, k = 0) {
	const r = dbgFrames(pa);
	if (r.state !== "ok")
		return;
	const loc = r.frames[k];
	if (!loc || !loc.file || !loc.line)
		return;
	navJump(() => {
		srcPanel = { pa, fn: loc.func, path: loc.file, line: loc.line, pick: loc.line, scrollTo: loc.line };
		srcShown();
	});
}

// After srcPanel changed: shown, drawn, remembered, in the hash.
function srcShown() {
	srcSwClose(false);
	$("src_panel").hidden = false;
	renderSourcePanel();
	srcRemember(srcPanel.path);
	writeHash();
}

// Open the side panel with a whole file (the Coverage tab): every line of
// it with code, in any function, and which of them have executed.
function openSourceFile(path) {
	navJump(() => srcShowFile(path, 0));
}

// The whole file without a history entry (openSourceFile, openSourceLine,
// a reload's hash): at line, else at its first line that ran (or has code).
function srcShowFile(path, line) {
	srcPanel = { pa: -1, fn: null, path, line, whole: true, pick: line || undefined, scrollTo: line || "first" };
	srcShown();
}

// Open the side panel with a whole file at one line, picked (a function's
// declaration): its addresses are listed as for a tapped line.
function openSourceLine(path, line) {
	navJump(() => srcShowFile(path, line));
}

// A whole file's lines with code: the line rows of every shard that has
// code of it (files.json), each frame naming the file, like the Coverage
// tab counts them.
function fileLines(path) {
	const fl = dbgFiles();
	if (fl.state !== "ok")
		return fl;
	const F = fl.F, fi = F.fileIx.get(path);
	// A file without code (data only: "defined at" of a variable) is shown with none marked.
	if (fi === undefined)
		return { state: "ok", first: 1, last: Infinity, code: new Map(), fns: new Map(), incl: new Set() };
	const size = 2 ** F.shardBits;
	const tables = F.files[fi][2].map((s) => [s, dbgTableAt(s * size)]);
	const wait = tables.find(([, r]) => r.state !== "ok");
	if (wait)
		return wait[1];
	const code = new Map();
	const fns = new Map();   // function -> its first line in this file
	const incl = new Set();  // files of code inlined into this file's lines
	for (const [s, r] of tables)
		dwarfEachLine(r.t, s * size, (s + 1) * size, (a, b, frames) => {
			let mine = -1;
			frames.forEach((f, k) => {
				if (f.file === path && f.line) {
					if (!code.has(f.line))
						code.set(f.line, []);
					code.get(f.line).push([a, b]);
					if (mine < 0)
						mine = k;
					if (f.func && !(fns.get(f.func) <= f.line))
						fns.set(f.func, f.line);
				}
			});
			for (let k = 0; k < mine; k++)
				if (frames[k].file && frames[k].file !== path)
					incl.add(frames[k].file);
		});
	return { state: "ok", first: 1, last: Infinity, code, fns, incl };
}

// The panel's lines from the DWARF of the function around srcPanel.pa:
// first/last are the lines of srcPanel.fn in its file (directly, not
// through code inlined into it), widened by two lines; code has every
// line of that span with code (also as the call site of inlined code) and
// the address ranges of each. Kept per DWARF version.
function panelLines() {
	if (srcPanel.lines && srcPanel.dbgVersion === DBG.version)
		return srcPanel.lines;
	if (srcPanel.whole) {
		const fl = fileLines(srcPanel.path);
		if (fl.state === "ok") {
			srcPanel.lines = fl;
			srcPanel.dbgVersion = DBG.version;
		}
		return fl;
	}
	const fn = dbgFunction(srcPanel.pa);
	if (fn.state !== "ok")
		return fn;
	const { path } = srcPanel;
	let lo = srcPanel.line, hi = srcPanel.line;
	for (const [, , frames] of fn.rows)
		for (const f of frames)
			if (f.func === srcPanel.fn && f.file === path && f.line) {
				lo = Math.min(lo, f.line);
				hi = Math.max(hi, f.line);
			}
	const first = Math.max(1, lo - 2), last = hi;
	const code = new Map(); // line -> [[a, b], ...]
	for (const [a, b, frames] of fn.rows)
		for (const f of frames)
			if (f.file === path && f.line >= first && f.line <= last) {
				if (!code.has(f.line))
					code.set(f.line, []);
				code.get(f.line).push([a, b]);
			}
	srcPanel.lines = { state: "ok", first, last, code, lo: fn.lo, hi: fn.hi };
	srcPanel.dbgVersion = DBG.version;
	return srcPanel.lines;
}

// Lines that have code, and those whose code has run (an instruction
// started in one of its address ranges while stepping).
function lineCoverage(pl) {
	const run = new Set();
	if (stepper)
		for (const [l, spans] of pl.code)
			if (spans.some(([a, b]) => stepper.countIn(a, b) > 0))
				run.add(l);
	return { code: new Set(pl.code.keys()), run };
}

function renderSourcePanel() {
	if (!srcPanel)
		return;
	const { fn, path, line } = srcPanel;
	const body = $("src_body");
	let pl = panelLines();
	const file = srcFile(path);
	// A whole file without the file list (an imported vmlinux): its text,
	// no line marked.
	if (srcPanel.whole && pl.state === "none" && file.state === "ok")
		pl = { state: "ok", first: 1, last: Infinity, code: new Map(), why: pl.why };
	body.dataset.file = path;
	srcTitle(pl.state === "ok" ? ":" + pl.first + "-" + pl.last : "");
	srcOutline();
	if (pl.state !== "ok" || file.state !== "ok") {
		$("src_addrs").hidden = true;
		body.textContent = pl.state === "loading" || file.state === "loading" ? "reading " + (pl.state === "loading" ? "DWARF" : path) + "…" :
			pl.state === "none" ? pl.why : file.why;
		body.dataset.code = body.dataset.executed = 0;
		body.dataset.state = pl.state === "none" || file.state === "none" ? "none" : "loading";
		$("src_info").textContent = "";
		return;
	}
	const { code, run } = lineCoverage(pl);
	const sv = srcStatementView(file, pl.first, pl.last, code, run);
	srcTitle(":" + sv.first + "-" + sv.last);
	const out = [];
	for (let l = sv.first; l <= sv.last; l++) {
		const c = sv.cont.get(l);
		const ran = run.has(l) || (c && c.ran);
		const cls = (ran ? "exec" : code.has(l) || c ? "code" : "") + (c ? " cont" : "");
		const mark = c ? "┆" : run.has(l) ? "●" : code.has(l) ? "○" : " ";
		const title = c ? ' title="' + escHtml("Line " + l + " is part of the statement at lines " + c.start + "–" + c.end + ", which " +
			(c.ran ? "ran" : "has not run") + ". Not counted: only lines with code of their own in the line table are counted, here, in the Coverage tab and in the lcov export.") + '"' : "";
		out.push('<span class="' + cls + (l === line ? " here" : "") + (l === srcPanel.pick ? " pick" : "") + '" data-line="' + l + '"' + title + ">" + srcLocHtml(String(l).padStart(5, " ")) + " " + mark + " " +
			'<span class="srctext">' + escHtml(file.lines[l - 1]) + "</span>" + bmLineStar(path, l) + "</span>");
	}
	body.innerHTML = out.join("\n");
	srcPanel.cont = sv.cont;
	renderLineAddrs();
	// Opened at a line: scroll there once the file and the address list
	// above it are drawn (the list changes the body's height). The row's
	// offsetParent is the fixed panel, not the body: measure against the body.
	if (srcPanel.scrollPx !== undefined) {
		body.scrollTop = srcPanel.scrollPx; // Back to this view: where it was left
		delete srcPanel.scrollPx;
		delete srcPanel.scrollTo;
	} else if (srcPanel.scrollTo) {
		const row = srcPanel.scrollTo === "first" ? body.querySelector(".exec") || body.querySelector(".code") :
			body.querySelector('[data-line="' + srcPanel.scrollTo + '"]');
		if (row)
			body.scrollTop += row.getBoundingClientRect().top - body.getBoundingClientRect().top - body.clientHeight / 3;
		else if (srcPanel.scrollTo === "first")
			body.scrollTop = 0;
		delete srcPanel.scrollTo;
	}
	body.dataset.state = "ok";
	body.dataset.code = code.size;
	body.dataset.executed = run.size;
	const note = SRC.notes && SRC.notes[path] ? " " + path + ": " + SRC.notes[path] + "." : "";
	const cov = stepper ? coverageFor(stepper.kernelLo) : null;
	$("src_info").textContent = pl.why ? "No line is marked: " + pl.why + ". Source: " + (SRC.info || SRC.mode) + "." :
		run.size + " of " + code.size + " lines with code have executed (● ran, ○ not yet, ┆ more lines of the statement above or below, not counted)" +
		(!stepper ? "; boot a kernel to see coverage" : !cov.tint ? "; " + cov.why : cov.partial ? "; v86's JIT has run since the kernel entry and records nothing, so ○ lines may have run" : "") +
		". Recorded while stepping" + (srcPanel.whole ? ", every function of the file" : "") + ". Source: " + (SRC.info || SRC.mode) + "." + note;
}

// ---- another file: breadcrumb, outline, file switcher ------------------------

// The title: "fn, dir/dir/file.c:first-last" or "dir/dir/file.c (whole
// file)", each directory of the path a button that lists its files in the
// switcher, the file name one that opens the whole file. Drawn again only
// when it changes (the panel is drawn every second while stepping, and a
// focused button would lose the focus).
function srcTitle(range) {
	const { fn, path, whole } = srcPanel;
	const key = [whole, fn, path, range].join("\0");
	const t = $("src_title");
	if (t.dataset.key === key)
		return;
	t.dataset.key = key;
	t.textContent = "";
	if (!whole)
		t.append((fn || "?") + ", ");
	const parts = path.split("/");
	parts.forEach((p, k) => {
		const b = document.createElement("button");
		b.type = "button";
		b.className = "crumb";
		if (k < parts.length - 1) {
			const dir = parts.slice(0, k + 1).join("/");
			b.textContent = p + "/";
			b.dataset.dir = dir;
			b.title = "List the files in " + dir + "/";
			b.addEventListener("click", () => srcSwOpen(dir));
		} else if (!whole) {
			b.textContent = p;
			b.dataset.path = path;
			b.title = "Open the whole of " + path;
			b.addEventListener("click", () => openSourceFile(path));
		} else {
			t.append(p);
			return;
		}
		t.append(b);
	});
	t.append(whole ? " (whole file)" : range);
}

// The functions of the panel's file (from its line rows, each at its first
// line in the file, and every function files.json puts in it) and the
// files of code inlined into its lines (headers), each one pick away.
function srcFileFacts(path) {
	const c = srcPanel.facts;
	if (c && c.path === path && c.ver === DBG.version)
		return c.r;
	const r = fileLines(path);
	if (r.state === "ok")
		srcPanel.facts = { path, ver: DBG.version, r };
	return r;
}

function srcOutline() {
	const { path } = srcPanel;
	const fl = dbgFiles(), r = srcFileFacts(path);
	const key = [path, r.state, fl.state, DBG.version].join("\0");
	const sel = $("src_outline"), inc = $("src_incl");
	if (sel.dataset.key === key)
		return;
	sel.dataset.key = key;
	const fns = new Map(r.state === "ok" ? r.fns : []);
	const los = new Map();
	if (fl.state === "ok") {
		const k = fl.F.fileIx.get(path);
		for (const fn of fl.F.funcs)
			if (k !== undefined && fn[1] === k && !los.has(fn[0]))
				los.set(fn[0], fn[4]);
	}
	for (const name of los.keys())
		if (!fns.has(name))
			fns.set(name, 0);
	const list = [...fns].sort((x, y) => (x[1] || Infinity) - (y[1] || Infinity) || x[0].localeCompare(y[0]));
	const incl = r.state === "ok" ? [...r.incl].sort() : [];
	sel.textContent = inc.textContent = "";
	sel.append(new Option(list.length ? "ƒ " + list.length + " function" + (list.length === 1 ? "" : "s") + " in this file…" :
		r.state === "loading" ? "ƒ reading the functions…" : "ƒ no functions", ""));
	for (const [name, line] of list) {
		// A function without a line of its own here (only its address):
		// the Page tab at its code.
		const o = new Option(name + "()" + (line ? "  line " + line : "  (no line: opens its code)"), line ? String(line) : "@" + los.get(name));
		o.dataset.fn = name;
		sel.append(o);
	}
	sel.disabled = !list.length;
	inc.append(new Option(incl.length + " header" + (incl.length === 1 ? "" : "s") + " inlined here…", ""));
	for (const p of incl)
		inc.append(new Option(p, p));
	inc.hidden = !incl.length;
	$("src_nav").hidden = !list.length && !incl.length;
}

// Recently opened files, per kernel, newest first (localStorage; without
// it, until the page is reloaded).
const SRC_RECENT_KEY = "mkb-src-recent";
const SRC_RECENT_MAX = 8;
let srcRecentMem = {};

const srcKernelKey = () => (typeof image !== "undefined" && image ? image.name : "");

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
	if (typeof covRun !== "function")
		return { run: null, code };
	const run = covRun(fl.F);
	return { run: run.tint && run.linesKnown ? run.fileRun[info.k].size : null, code, why: run.tint ? "" : run.why };
}

// The files of the call stack at a pause: EIP's inline chain and every
// unwound frame's (app.js unwindHere), innermost first.
function srcStackFiles() {
	const out = [];
	if (!stepper || (runState !== "paused" && runState !== "stepped"))
		return out;
	const pcs = [stepper.nextPhys()];
	try {
		for (const f of unwindHere().frames)
			if (!f.user)
				pcs.push(insnStart(f.lookup));
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

function srcSwOpen(dir = null) {
	srcSw.dir = dir;
	srcSw.q = "";
	srcSw.at = 0;
	$("src_q").value = "";
	$("src_switch").hidden = false;
	$("src_switch").dataset.dir = dir === null ? "" : dir;
	$("src_files").setAttribute("aria-expanded", "true");
	$("src_q").setAttribute("aria-expanded", "true");
	srcSwRender();
	$("src_q").focus({ preventScroll: true });
}

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

function srcSwRender() {
	if ($("src_switch").hidden)
		return;
	const rows = srcSwRows();
	srcSw.rows = rows;
	srcSw.at = Math.min(srcSw.at, Math.max(0, rows.length - 1));
	const ul = $("src_list");
	ul.textContent = "";
	let group = null;
	rows.forEach((x, i) => {
		const head = srcSw.dir !== null ? (i === 0 ? "dir" : null) : x.group;
		if (head && head !== group) {
			group = head;
			const h = document.createElement("li");
			h.setAttribute("role", "presentation");
			h.className = "srcswhead";
			h.textContent = head === "dir" ? (srcSw.dir || "the source tree") + "/" : SRC_SW_GROUPS[head];
			ul.append(h);
		}
		const li = document.createElement("li");
		li.setAttribute("role", "option");
		li.id = "src_opt_" + i;
		li.className = "srcopt" + (i === srcSw.at ? " active" : "") + (x.path && srcPanel && x.path === srcPanel.path ? " current" : "");
		li.setAttribute("aria-selected", i === srcSw.at);
		li.dataset.i = i;
		li.dataset.group = x.group;
		if (x.path) {
			li.dataset.path = x.path;
			const c = srcFileCounts(x.info);
			li.dataset.code = c.code === null ? "" : c.code;
			li.dataset.executed = c.run === null ? "" : c.run;
			const k = x.path.lastIndexOf("/");
			const b = document.createElement("b");
			b.textContent = x.path.slice(k + 1);
			const dir = document.createElement("span");
			dir.className = "dim";
			dir.textContent = k >= 0 ? " " + x.path.slice(0, k + 1) : "";
			const n = document.createElement("span");
			n.className = "srcswn";
			const text = SRC.mode === "shipped" && SRC.have && !SRC.have.has(x.path) ? "; no text: " + (SRC.missing[x.path] || "not shipped") : "";
			n.textContent = c.code === null ? (x.info.k < 0 && dbgFiles().state === "ok" ? "no code" : "") + text :
				(c.run === null ? "" : c.run + " of ") + c.code + " line" + (c.code === 1 ? "" : "s") + (c.run === null ? " with code" : " ran") + text;
			li.title = x.path + (c.code === null ? "" : ": " + c.code + " lines with code" + (c.run === null ? (c.why ? "; " + c.why : "") : ", " + c.run + " of them have executed")) +
				(text ? text.replace(/^;/, ".") : "");
			li.append(b, dir, " ", n);
		} else {
			li.dataset.dir = x.dir;
			li.textContent = x.group === "up" ? "↑ " + (x.dir || "the source tree") + "/" : x.dir.slice(x.dir.lastIndexOf("/") + 1) + "/";
			li.title = "List the files in " + (x.dir || "the source tree") + "/";
		}
		ul.append(li);
	});
	if (srcSw.more || srcSw.none) {
		const m = document.createElement("li");
		m.setAttribute("role", "presentation");
		m.className = "note";
		m.textContent = srcSw.none ? (srcSw.q ? "No file matches." : "No file list: " + (dbgFiles().why || SRC.why || "no debug info")) :
			"… and " + srcSw.more.toLocaleString("en") + " more: type to narrow.";
		ul.append(m);
	}
	$("src_q").setAttribute("aria-activedescendant", rows.length ? "src_opt_" + srcSw.at : "");
	if (!rows.length)
		$("src_q").removeAttribute("aria-activedescendant");
}

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

function srcSwPick(i) {
	const x = srcSw.rows[i];
	if (!x)
		return;
	if (x.path) {
		srcSwClose(false);
		openSourceFile(x.path);
		$("src_body").focus({ preventScroll: true });
		return;
	}
	srcSwOpen(x.dir);
}

function initSourceFiles() {
	$("src_files").addEventListener("click", () => $("src_switch").hidden ? srcSwOpen() : srcSwClose());
	$("src_q").addEventListener("input", () => {
		srcSw.q = $("src_q").value.trim();
		srcSw.at = 0;
		srcSwRender();
	});
	$("src_q").addEventListener("keydown", (ev) => {
		const page = Math.max(1, Math.floor($("src_list").clientHeight / 24));
		const to = { ArrowDown: srcSw.at + 1, ArrowUp: srcSw.at - 1, PageDown: srcSw.at + page, PageUp: srcSw.at - page }[ev.key];
		if (to !== undefined)
			srcSwMove(to);
		else if (ev.key === "Enter")
			srcSwPick(srcSw.at);
		else if (ev.key === "Escape")
			srcSwClose();
		else
			return;
		ev.preventDefault();
		ev.stopPropagation();
	});
	$("src_list").addEventListener("click", (ev) => {
		const li = ev.target.closest(".srcopt");
		if (li)
			srcSwPick(Number(li.dataset.i));
	});
	$("src_outline").addEventListener("change", () => {
		const v = $("src_outline").value;
		$("src_outline").value = "";
		if (!srcPanel || !v)
			return;
		if (v[0] === "@")
			jumpTo(Number(v.slice(1)));
		else
			openSourceLine(srcPanel.path, Number(v));
	});
	$("src_incl").addEventListener("change", () => {
		const v = $("src_incl").value;
		$("src_incl").value = "";
		if (v)
			openSourceFile(v);
	});
	// v86 takes every key on the window for the VM while the Machine tab
	// shows its screen (preventDefault): the panel's buttons get Enter and
	// Space here, before it.
	$("src_panel").addEventListener("keydown", (ev) => {
		if ((ev.key === "Enter" || ev.key === " ") && ev.target.tagName === "BUTTON") {
			ev.preventDefault();
			ev.stopPropagation();
			ev.target.click();
		}
	});
}

// The panel's picked line (a click on a line, or the line a listing's C
// line opened): every address of its code (each inlined copy and each
// inlined call made on it, dbgLineCode) with a jump to it, and "Run to
// file:line". A line that continues a statement shows its first line's.
const SRC_ADDRS_SHOWN = 40;

function renderLineAddrs() {
	const box = $("src_addrs");
	const pick = srcPanel && srcPanel.pick;
	box.hidden = !pick;
	if (!pick)
		return;
	const c = srcPanel.cont && srcPanel.cont.get(pick);
	const line = c ? c.start : pick;
	const r = dbgLineCode(srcPanel.path, line);
	const key = [srcPanel.path, pick, r.state, DBG.version, !!stepper].join("\0");
	if (box.dataset.key === key)
		return;
	box.dataset.key = key;
	box.textContent = "";
	box.dataset.state = r.state;
	box.dataset.line = line;
	box.dataset.path = r.path || srcPanel.path;
	const where = (r.path || srcPanel.path) + ":" + line;
	const head = document.createElement("div");
	head.append(srcLocEl(where), c ? " (line " + pick + " continues this statement): " : ": ");
	box.append(head);
	if (r.state !== "ok") {
		head.append(r.state === "loading" ? r.why || "reading DWARF…" : r.why);
		box.dataset.bps = box.dataset.rows = box.dataset.calls = "[]";
		return;
	}
	box.dataset.bps = JSON.stringify(r.bps);
	box.dataset.rows = JSON.stringify(r.rows.map(([a, b]) => [a, b]));
	box.dataset.calls = JSON.stringify(r.calls.map(([a, b, name]) => [a, b, name]));
	const outer = (pa) => {
		const f = dbgFrames(pa, true);
		return f.state === "ok" ? f.frames : null;
	};
	const fns = new Set(r.rows.map(([a]) => { const f = outer(a); return f ? f[f.length - 1].func : "?"; }));
	head.append(r.rows.length + " range" + (r.rows.length === 1 ? "" : "s") + " of its own code" +
		(fns.size > 1 ? " in " + fns.size + " functions (inlined copies)" : "") +
		(r.calls.length ? ", " + r.calls.length + " range" + (r.calls.length === 1 ? "" : "s") + " of code inlined by a call on it" : "") + ". ");
	const run = document.createElement("button");
	run.type = "button";
	run.id = "src_runto";
	run.textContent = "Run to " + where;
	run.disabled = !stepper;
	run.title = !stepper ? "Boot a kernel first" : "Run until the kernel reaches the first of this line's " + r.bps.length +
		" breakpoint address" + (r.bps.length === 1 ? "" : "es") + " (the is_stmt rows of the line table, and where each inlined call starts)" +
		(r.noStmt ? ". This line table has no is_stmt flags: every range's start counts" : "");
	run.addEventListener("click", () => runToLine(where));
	head.append(run);
	const list = document.createElement("ul");
	const item = (a, b, text, title) => {
		const li = document.createElement("li");
		const btn = symButton(hex(a), a, "Show this code in the Page tab");
		btn.dataset.addr = a;
		const s = symbols && symbolAt(symbols.symbols, a);
		li.append(btn, " " + (s ? s.name + (a > s.paddr ? "+0x" + (a - s.paddr).toString(16) : "") : "") + " (" + (b - a) + " byte" + (b - a === 1 ? "" : "s") + ")" + text);
		li.title = title;
		list.append(li);
	};
	const all = [...r.rows.map((x) => ["row", x]), ...r.calls.map((x) => ["call", x])].sort((x, y) => x[1][0] - y[1][0]);
	for (const [kind, x] of all.slice(0, SRC_ADDRS_SHOWN)) {
		if (kind === "row") {
			const [a, b, bp] = x;
			const f = outer(a);
			const into = f && f.length > 1 ? "; inlined into " + (f[f.length - 1].func || "?") : "";
			item(a, b, into + (bp.length ? "; breakpoint" + (bp.length > 1 ? "s at " + bp.map(hex).join(", ") : bp[0] === a ? " here" : " at " + hex(bp[0])) :
				"; no is_stmt row here"), "Code of " + where + (f ? "\n" + dbgChainText(f) : ""));
		} else {
			const [a, b, name] = x;
			item(a, b, "; " + (name || "?") + "() inlined by the call on this line; breakpoint at its start", "Code of " + (name || "?") + ", inlined at " + where);
		}
	}
	if (all.length > SRC_ADDRS_SHOWN) {
		const li = document.createElement("li");
		li.textContent = "… and " + (all.length - SRC_ADDRS_SHOWN) + " more (Run to sets all " + r.bps.length + " breakpoints)";
		list.append(li);
	}
	box.append(list);
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
			const t = f.file && f.line ? srcText(f.file, f.line) : null;
			const fn = f.func || sym || "?";
			const loc = f.file && f.line ? srcLinkEl(f.file, f.line, dbgWhere(f), phys) : null;
			if (loc && t === null)
				loc.title += "\n" + srcWhyNot(f);
			if (k)
				line.append("inlined into " + fn, ...(loc ? [" at ", loc] : [" (no line information)"]));
			else
				line.append(loc || "no line information", " in " + fn + (r.frames.length > 1 ? " (inlined)" : ""));
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

// The .srclink that opened the panel from the keyboard: Escape or Close
// gives it the focus back.
let srcOpener = null;

function closeSource() {
	srcSwClose(false);
	$("src_panel").hidden = true;
	srcPanel = null;
	writeHash();
	const a = srcLinkAgain(srcOpener);
	if (a)
		a.focus();
	srcOpener = null;
}

function initSource() {
	// Every .srclink (srcLinkEl, srcLinkHtml) in any view; before the
	// listings' own handlers. A keyboard click (detail 0) moves the focus
	// into the panel.
	document.addEventListener("click", (ev) => {
		const a = ev.target.closest ? ev.target.closest(".srclink") : null;
		if (!a)
			return;
		ev.stopPropagation();
		ev.preventDefault();
		srcOpenAt(a.dataset.file, Number(a.dataset.line), Number(a.dataset.pa));
		srcOpener = ev.detail === 0 ? a : null;
		if (srcOpener) {
			srcLinkMark(a);
			$("src_body").focus({ preventScroll: true });
		}
	}, true);
	// v86 takes every key on the window for the VM (preventDefault), which
	// stops a button's own Enter and Space: click the link here first.
	document.addEventListener("keydown", (ev) => {
		const a = ev.target.closest ? ev.target.closest(".srclink") : null;
		if (a && (ev.key === "Enter" || ev.key === " ")) {
			ev.preventDefault();
			ev.stopPropagation();
			a.click();
		}
	}, true);
	$("src_panel").addEventListener("keydown", (ev) => {
		if (ev.key === "Escape")
			closeSource();
	});
	initSourceFiles();
	$("src_close").addEventListener("click", closeSource);
	$("src_body").addEventListener("click", (ev) => {
		const row = ev.target.closest("[data-line]");
		if (!row || !srcPanel || window.getSelection().toString())
			return;
		srcPanel.pick = Number(row.dataset.line);
		for (const x of $("src_body").querySelectorAll(".pick"))
			x.classList.remove("pick");
		row.classList.add("pick");
		renderLineAddrs();
		writeHash();
		row.scrollIntoView({ block: "nearest" }); // the address list above can push it out of view
	});
	for (const id of ["listing", "context"])
		$(id).addEventListener("click", (ev) => {
			const line = ev.target.closest(".src");
			if (line && !line.classList.contains("nol")) {
				ev.stopPropagation();
				openSource(Number(line.dataset.addr));
			}
		}, true);
	$("show_src").addEventListener("change", () => {
		inspect();
		renderContext();
	});
	$("src_tar").addEventListener("change", () => {
		const f = $("src_tar").files[0];
		if (f)
			srcPickTar(f);
	});
	$("src_dir_btn").addEventListener("click", () => $("src_dir").click());
	$("src_dir").addEventListener("change", () => {
		if ($("src_dir").files.length)
			srcPickFolder($("src_dir").files);
	});
	dbgOnChange(() => {
		srcStatus();
		if (srcPanel && !$("src_panel").hidden) {
			renderSourcePanel();
			srcSwRender();
		}
	});
	setInterval(() => {
		if (srcPanel && !$("src_panel").hidden && (runState === "stepped"))
			renderSourcePanel();
	}, 1000);
}
