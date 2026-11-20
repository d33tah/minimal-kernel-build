"use strict";

// The C source text the Source panel shows (source.js): the shipped
// files, a folder or .tar picked by the user, statements over lines.

import { $ } from "./util.js";
import { api } from "./page.js";
import { dbgChanged, dbgHas } from "./dbginfo.js";
import { dbgGunzip } from "./dbgmerge.js";

let SRC = { mode: null, why: "no C source for this kernel", files: new Map() };
// Texts shown over the build's (E8): a file the machine runs another text
// of, hot patched (hotpatch.jsx): path -> { state: "ok", lines, over }, over
// what the panel says about it. The line numbers of the build's DWARF are
// moved to this text (dbginfo.js DBG.remap).
const SRC_OVER = new Map();

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
		const r = await api.source.kfetch(SRC.base + "index.json");
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
	dbgChanged();
	srcStatus();
}

const isGzip = (b) => b.length > 2 && b[0] === 0x1f && b[1] === 0x8b;

async function srcDecode(buf) {
	return new TextDecoder().decode(await dbgGunzip(buf)).split("\n");
}

// The text the machine runs instead of the build's (null: the build's again).
function srcOverride(path, text, over) {
	if (text === null)
		SRC_OVER.delete(path);
	else
		SRC_OVER.set(path, { state: "ok", lines: text.split("\n"), over });
}

// { state: "ok", lines } (lines[0] is line 1), { state: "loading" } or
// { state: "none", why }. built: the text the kernel was built from, not
// the one a hot patch put in (SRC_OVER).
function srcFile(path, built = false) {
	if (!path)
		return { state: "none", why: "no line information" };
	if (!built && SRC_OVER.has(path))
		return SRC_OVER.get(path);
	const got = SRC.files.get(path);
	if (got)
		return got;
	let load = null;
	if (SRC.mode === "shipped") {
		if (!SRC.have)
			return { state: "loading" };
		if (!SRC.have.has(path))
			return { state: "none", why: path + ": " + (SRC.missing[path] || "not shipped") };
		load = () => api.source.kfetch(SRC.base + path.split("/").map(encodeURIComponent).join("/") + ".gz")
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
	api.source.genSrcPicked();
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
	api.source.genSrcPicked();
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

export { SRC, SRC_OVER, srcOverride, srcFresh, srcUseShipped, srcChanged, srcFile, srcText, srcPickFolder, srcPickTar, srcStatus,
	srcStatements, srcStatementView };
