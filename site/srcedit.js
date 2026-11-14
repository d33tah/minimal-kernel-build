"use strict";

// Editing the kernel's source in the page (E2). The Source panel's Edit
// turns the file shown into a text editor: a plain <textarea> with a
// column of line numbers beside it (no editor library: CodeMirror 6 with C
// highlighting measured 519 KB minified, 167 KB gzip, about 60% more than
// all of the page's own scripts; the textarea gives undo, selection, IME and
// phone keyboards from the browser for nothing). Edits live in an overlay
// over the tree the kernel was built from: per kernel and path the text the
// edit started from (base) and the edited text, in IndexedDB, else in
// memory until a reload. A file whose text is the base again is no longer
// an edit. The Source tab lists the changed files with a diff and a revert
// each, downloads them as one patch for `git apply` (a/ and b/ paths
// relative to the kernel's tree, as the DWARF names them) and applies an
// uploaded patch: every file of it or none.
//
// The listings, the Debugger and the panel's own view keep showing the
// text the kernel was built from: their line numbers and coverage belong to
// that build. srcOverlay is the API for the in-browser build (E3, E4):
// paths() the changed files of the shown kernel, read(path) a file's text
// with the overlay applied, original(path) the text it was built from.

const ED_DB = "mkb-srcedit";
const ED_STORE = "edits";
const ED_SAVE_MS = 300;          // typing pause before the overlay is written
const ED_DIFF_MAX = 2000;        // Myers' D limit: more differences give one hunk of the whole middle
const ED_CONTEXT = 3;            // lines of context in a hunk (diff -u, git)
const ED_WAIT_MS = 60000;        // longest wait for a file's shipped text
const ED_ARM_MS = 5000;          // a revert's second press within this time

const ED = {
	all: new Map(),      // kernel -> Map(path -> { base, text }); base null: a new file, text null: deleted
	db: null,
	why: "",             // why there is no IndexedDB (the edits go with a reload)
	ready: null,
	on: false,           // the panel is in edit mode (it stays on across files until Edit again)
	cur: null,           // { kernel, path, base } of the file in the editor
	timer: 0,
	diffShown: false,    // the panel shows the file's diff instead of the text
	listDiff: null,      // path whose diff the Source tab shows
	armed: null,         // the revert waiting for its second press: "*", "panel" or "file:" + path
	armTimer: 0,
};

const edKernel = () => srcKernelKey();

function edMap(kernel = edKernel()) {
	if (!ED.all.has(kernel))
		ED.all.set(kernel, new Map());
	return ED.all.get(kernel);
}

// ---- the store --------------------------------------------------------------

function edLoad() {
	return new Promise((resolve) => {
		let req;
		try {
			req = indexedDB.open(ED_DB, 1);
		} catch (e) {
			ED.why = "no IndexedDB here (" + e.message + ")";
			resolve();
			return;
		}
		req.onupgradeneeded = () => req.result.createObjectStore(ED_STORE, { keyPath: ["kernel", "path"] });
		req.onerror = () => {
			ED.why = "IndexedDB did not open (" + (req.error ? req.error.message : "error") + ")";
			resolve();
		};
		req.onsuccess = () => {
			ED.db = req.result;
			const get = ED.db.transaction(ED_STORE).objectStore(ED_STORE).getAll();
			get.onsuccess = () => {
				for (const r of get.result)
					edMap(r.kernel).set(r.path, { base: r.base, text: r.text });
				resolve();
			};
			get.onerror = () => {
				ED.why = "the stored edits did not load (" + (get.error ? get.error.message : "error") + ")";
				ED.db = null;
				resolve();
			};
		};
	});
}

// Store one file's state: text equal to base drops the edit.
function edPut(kernel, path, base, text) {
	const m = edMap(kernel);
	const drop = base === text;
	if (drop)
		m.delete(path);
	else
		m.set(path, { base, text });
	if (!ED.db)
		return Promise.resolve();
	return new Promise((resolve, reject) => {
		const tx = ED.db.transaction(ED_STORE, "readwrite");
		const st = tx.objectStore(ED_STORE);
		if (drop)
			st.delete([kernel, path]);
		else
			st.put({ kernel, path, base, text });
		tx.oncomplete = () => resolve();
		tx.onerror = tx.onabort = () => reject(tx.error || new Error("IndexedDB write failed"));
	});
}

// ---- the text the kernel was built from ---------------------------------

// The text of path from source.js (shipped or picked tree), or null when
// there is none; waits while it loads.
function edShipped(path) {
	return new Promise((resolve, reject) => {
		const t0 = Date.now();
		const poll = () => {
			const f = srcFile(path);
			if (f.state === "ok")
				resolve(f.lines.join("\n"));
			else if (f.state === "none")
				resolve(null);
			else if (Date.now() - t0 > ED_WAIT_MS)
				reject(new Error(path + ": its text did not load"));
			else
				setTimeout(poll, 50);
		};
		poll();
	});
}

async function edRead(path) {
	await ED.ready;
	const e = edMap().get(path);
	return e ? e.text : edShipped(path);
}

async function edOriginal(path) {
	await ED.ready;
	const e = edMap().get(path);
	return e ? e.base : edShipped(path);
}

// ---- diff -------------------------------------------------------------------

// A text's lines, each with its "\n" (the last one may have none).
const edLines = (t) => t ? t.match(/[^\n]*\n|[^\n]+$/g) : [];

// The edit script from a to b (arrays of lines): [[" ", line] | ["-", line] | ["+", line]].
// Myers' O(ND) after the common head and tail; past ED_DIFF_MAX differences
// the middle becomes one delete and one insert (still a correct patch).
function edDiff(a, b) {
	let s = 0;
	while (s < a.length && s < b.length && a[s] === b[s])
		s++;
	let e = 0;
	while (e < a.length - s && e < b.length - s && a[a.length - 1 - e] === b[b.length - 1 - e])
		e++;
	const A = a.slice(s, a.length - e), B = b.slice(s, b.length - e);
	const out = a.slice(0, s).map((x) => [" ", x]);
	out.push(...edMiddle(A, B));
	out.push(...a.slice(a.length - e).map((x) => [" ", x]));
	return out;
}

function edMiddle(A, B) {
	const N = A.length, M = B.length;
	const all = () => [...A.map((x) => ["-", x]), ...B.map((x) => ["+", x])];
	if (!N || !M)
		return all();
	const max = N + M, off = max + 1;
	const v = new Int32Array(2 * max + 3);
	const trace = [];
	let found = -1;
	for (let d = 0; d <= Math.min(max, ED_DIFF_MAX) && found < 0; d++) {
		trace.push(v.slice(off - d - 1, off + d + 2));
		for (let k = -d; k <= d; k += 2) {
			let x = k === -d || (k !== d && v[off + k - 1] < v[off + k + 1]) ? v[off + k + 1] : v[off + k - 1] + 1;
			let y = x - k;
			while (x < N && y < M && A[x] === B[y]) {
				x++;
				y++;
			}
			v[off + k] = x;
			if (x >= N && y >= M) {
				found = d;
				break;
			}
		}
	}
	if (found < 0)
		return all();
	// Back from (N, M): trace[d] holds v before step d, for k in [-d-1, d+1].
	const ops = [];
	let x = N, y = M;
	for (let d = found; d > 0; d--) {
		const t = trace[d], at = (k) => t[k + d + 1];
		const k = x - y;
		const down = k === -d || (k !== d && at(k - 1) < at(k + 1));
		const pk = down ? k + 1 : k - 1;
		const px = at(pk), py = px - pk;
		while (x > px + (down ? 0 : 1) && y > py + (down ? 1 : 0)) {
			ops.push([" ", A[--x]]);
			y--;
		}
		if (down)
			ops.push(["+", B[--y]]);
		else
			ops.push(["-", A[--x]]);
	}
	while (x > 0 && y > 0) {
		ops.push([" ", A[--x]]);
		y--;
	}
	return ops.reverse();
}

// Unified hunks of an edit script: [{ os, oc, ns, nc, ops }], ED_CONTEXT
// lines of context, hunks closer than twice that joined.
function edHunks(ops) {
	const ch = [];
	ops.forEach((o, i) => o[0] !== " " && ch.push(i));
	const hunks = [];
	const aBefore = new Int32Array(ops.length + 1), bBefore = new Int32Array(ops.length + 1);
	ops.forEach((o, i) => {
		aBefore[i + 1] = aBefore[i] + (o[0] !== "+");
		bBefore[i + 1] = bBefore[i] + (o[0] !== "-");
	});
	for (let g = 0; g < ch.length;) {
		let last = g;
		while (last + 1 < ch.length && ch[last + 1] - ch[last] - 1 <= 2 * ED_CONTEXT)
			last++;
		const from = Math.max(0, ch[g] - ED_CONTEXT), to = Math.min(ops.length, ch[last] + ED_CONTEXT + 1);
		const oc = aBefore[to] - aBefore[from], nc = bBefore[to] - bBefore[from];
		hunks.push({ os: oc ? aBefore[from] + 1 : aBefore[from], oc, ns: nc ? bBefore[from] + 1 : bBefore[from], nc, ops: ops.slice(from, to) });
		g = last + 1;
	}
	return hunks;
}

const edRange = (s, c) => c === 1 ? String(s) : s + "," + c;

// One file's part of a git patch (base or text null: a new or a deleted file);
// "" when they are equal. Also counts its added and deleted lines.
function edFilePatch(path, base, text) {
	if (base === text)
		return { patch: "", add: 0, del: 0 };
	const ops = edDiff(edLines(base), edLines(text));
	let add = 0, del = 0;
	for (const [k] of ops)
		if (k === "+")
			add++;
		else if (k === "-")
			del++;
	let p = "diff --git a/" + path + " b/" + path + "\n";
	if (base === null)
		p += "new file mode 100644\n";
	else if (text === null)
		p += "deleted file mode 100644\n";
	if (!add && !del) // an empty file made or removed: git takes the header alone
		return { patch: p, add, del };
	p += "--- " + (base === null ? "/dev/null" : "a/" + path) + "\n+++ " + (text === null ? "/dev/null" : "b/" + path) + "\n";
	for (const h of edHunks(ops)) {
		p += "@@ -" + edRange(h.os, h.oc) + " +" + edRange(h.ns, h.nc) + " @@\n";
		for (const [k, line] of h.ops)
			p += k + line + (line.endsWith("\n") ? "" : "\n\\ No newline at end of file\n");
	}
	return { patch: p, add, del };
}

function edPatch(kernel = edKernel()) {
	const m = edMap(kernel);
	return [...m.keys()].sort().map((p) => edFilePatch(p, m.get(p).base, m.get(p).text).patch).join("");
}

// ---- applying a patch -------------------------------------------------------

// A unified diff (git or diff -u, a/ b/ prefixes or none) as
// [{ path, from (old path or null), to (new path or null), hunks }].
function edParse(text) {
	const lines = edLines(text.replace(/\r\n/g, "\n"));
	const files = [];
	let f = null;
	const name = (s) => {
		s = s.replace(/\n$/, "").replace(/\t.*$/, "");
		if (/^".*"$/.test(s))
			throw new Error("quoted file names are not supported (" + s + ")");
		return s === "/dev/null" ? null : s;
	};
	const strip = (x) => {
		if (!x)
			return;
		if (x.from !== null && x.to !== null && x.from.startsWith("a/") && x.to.startsWith("b/")) {
			x.from = x.from.slice(2);
			x.to = x.to.slice(2);
		} else if (x.git) {
			x.from = x.from && x.from.replace(/^a\//, "");
			x.to = x.to && x.to.replace(/^b\//, "");
		}
		x.path = x.to !== null ? x.to : x.from;
	};
	for (let i = 0; i < lines.length; i++) {
		const l = lines[i];
		if (l.startsWith("diff --git ")) {
			strip(f);
			const m = /^diff --git a\/(.*) b\/(.*)\n?$/.exec(l);
			f = { git: true, from: m ? "a/" + m[1] : null, to: m ? "b/" + m[2] : null, hunks: [], seen: false };
			files.push(f);
		} else if (l.startsWith("--- ") && lines[i + 1] && lines[i + 1].startsWith("+++ ")) {
			if (!f || f.seen || f.hunks.length) {
				strip(f);
				f = { git: false, hunks: [] };
				files.push(f);
			}
			f.from = name(l.slice(4));
			f.to = name(lines[++i].slice(4));
			f.seen = true;
		} else if (f && l.startsWith("new file mode")) {
			f.from = null;
		} else if (f && l.startsWith("deleted file mode")) {
			f.to = null;
		} else if (/^(rename|copy) (from|to) |^similarity index |^GIT binary patch|^Binary files /.test(l)) {
			throw new Error("renames, copies and binary files are not supported (" + l.trim() + ")");
		} else if (l.startsWith("@@ ")) {
			const m = /^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@/.exec(l);
			if (!f || !m)
				throw new Error("a hunk without a file header: " + l.trim());
			const h = { os: +m[1], oc: m[2] === undefined ? 1 : +m[2], ns: +m[3], nc: m[4] === undefined ? 1 : +m[4], ops: [], head: l.trim() };
			let o = 0, n = 0;
			// "\ No newline at end of file" follows the line it is about
			// (the old side's last line may come before the new side's).
			const noEol = () => {
				while (lines[i + 1] && lines[i + 1].startsWith("\\")) {
					i++;
					if (h.ops.length)
						h.ops[h.ops.length - 1][1] = h.ops[h.ops.length - 1][1].replace(/\n$/, "");
				}
			};
			while (o < h.oc || n < h.nc) {
				const x = lines[++i];
				if (x === undefined)
					throw new Error((f.to || f.from) + ": the patch ends inside " + h.head);
				const k = x === "\n" ? " " : x[0]; // a blank context line whose space a mailer took
				if (k !== " " && k !== "-" && k !== "+")
					throw new Error((f.to || f.from) + ": a line of " + h.head + " starts with neither space, - nor +");
				h.ops.push([k, x === "\n" ? "\n" : x.slice(1)]);
				if (k !== "+")
					o++;
				if (k !== "-")
					n++;
				noEol();
			}
			f.hunks.push(h);
		}
	}
	strip(f);
	if (!files.length)
		throw new Error("no file in it (not a unified diff?)");
	return files;
}

// The hunks applied to a text's lines, each at its line or the nearest
// place where its old lines are (as patch's offset, no fuzz).
function edApplyHunks(path, base, hunks) {
	const lines = edLines(base);
	const out = [];
	let at = 0;
	hunks.forEach((h, n) => {
		const old = h.ops.filter((o) => o[0] !== "+").map((o) => o[1]);
		const want = h.oc ? h.os - 1 : h.os;
		const fits = (p) => old.every((x, i) => lines[p + i] === x);
		const hi = lines.length - old.length;
		let p = -1;
		for (let d = 0; p < 0; d++) {
			const up = want + d, dn = want - d;
			if (up > hi && dn < at)
				break;
			if (up >= at && up <= hi && fits(up))
				p = up;
			else if (d && dn >= at && dn <= hi && fits(dn))
				p = dn;
		}
		if (p < 0)
			throw new Error(path + ": hunk " + (n + 1) + " (" + h.head + ") does not match the text");
		out.push(...lines.slice(at, p), ...h.ops.filter((o) => o[0] !== "-").map((o) => o[1]));
		at = p + old.length;
	});
	out.push(...lines.slice(at));
	return out.join("");
}

// Apply a patch to the shown kernel's overlay: every file or none.
// Returns [{ path, base, text }] as stored.
async function edApply(text) {
	await ED.ready;
	const kernel = edKernel();
	const files = edParse(text);
	const done = [];
	for (const f of files) {
		if (!f.path)
			throw new Error("a file without a name");
		const e = edMap(kernel).get(f.path);
		const cur = e ? e.text : await edShipped(f.path);
		const base = e ? e.base : cur;
		if (f.from === null) {
			if (cur !== null)
				throw new Error(f.path + ": the patch makes it, but it is in the tree");
			done.push({ path: f.path, base, text: edApplyHunks(f.path, "", f.hunks) });
		} else {
			if (cur === null)
				throw new Error(f.path + ": not in this kernel's source" + (SRC.missing && SRC.missing[f.path] ? " (" + SRC.missing[f.path] + ")" : ""));
			const after = edApplyHunks(f.path, cur, f.hunks);
			if (f.to === null && after !== "")
				throw new Error(f.path + ": the patch deletes it, but its lines are not the whole file");
			done.push({ path: f.path, base, text: f.to === null ? null : after });
		}
	}
	for (const d of done)
		await edPut(kernel, d.path, d.base, d.text);
	edChanged();
	return done;
}

// ---- the editor in the Source panel ----------------------------------------

const edTa = () => $("ed_text");

// After srcShown (another file in the panel) and closeSource (srcPanel null).
function edPanelShown() {
	if (ED.cur && (!srcPanel || srcPanel.path !== ED.cur.path))
		edLeaveFile();
	if (!srcPanel)
		ED.on = ED.diffShown = false;
	edSync();
	if (ED.on && srcPanel && !ED.cur)
		edEnterFile();
}

// What the panel can do with its file: { ok, why }.
function edCan() {
	if (!srcPanel)
		return { ok: false, why: "" };
	if (edMap().has(srcPanel.path))
		return { ok: true };
	const f = srcFile(srcPanel.path);
	return f.state === "ok" ? { ok: true } : f.state === "loading" ? { ok: false, why: "reading " + srcPanel.path + "…" } :
		{ ok: false, why: "No text to edit: " + f.why };
}

// The Edit button, the bar under the title and which view the panel shows.
function edSync() {
	const panel = $("src_panel"), b = $("src_edit");
	const can = edCan();
	const e = srcPanel ? edMap().get(srcPanel.path) : null;
	b.disabled = !ED.on && !can.ok;
	b.setAttribute("aria-pressed", ED.on);
	b.title = ED.on ? "Stop editing: back to the text the kernel was built from, with its coverage" :
		can.ok ? "Edit this file. Your edits are kept in this browser (Source tab: the changed files, a patch). " +
			"Tab inserts a tab, Escape leaves the text, Ctrl+S saves at once." : can.why;
	panel.classList.toggle("editing", ED.on && !!ED.cur);
	panel.classList.toggle("diffing", ED.on && !!ED.cur && ED.diffShown);
	const bar = $("ed_bar");
	bar.hidden = !(ED.on && ED.cur) && !e;
	$("ed_diffbtn").setAttribute("aria-pressed", ED.diffShown);
	$("ed_diffbtn").disabled = !e;
	$("ed_revert").disabled = !e;
	if (!ED.on && e) {
		const s = edStats(srcPanel.path);
		$("ed_state").textContent = "You changed this file (+" + s.add + " -" + s.del + "): the panel shows the text the kernel was built from; Edit shows yours.";
	} else if (ED.on && ED.cur)
		edState();
	if (ED.diffShown && srcPanel)
		edShowPanelDiff();
}

async function edEnterFile() {
	const path = srcPanel.path, kernel = edKernel();
	const e = edMap(kernel).get(path);
	const base = e ? e.base : await edShipped(path).catch(() => null);
	if (!srcPanel || srcPanel.path !== path || !ED.on || ED.cur)
		return;
	if (!e && base === null) {
		ED.on = false;
		edSync();
		return;
	}
	ED.cur = { kernel, path, base };
	const ta = edTa();
	ta.value = e ? e.text || "" : base;
	ta.setAttribute("aria-label", "Text of " + path);
	edGutter(true);
	edSync();
	// At the line the panel was opened at or picked.
	const line = srcPanel.pick || srcPanel.line || 1;
	const lines = ta.value.split("\n");
	const pos = lines.slice(0, Math.max(0, line - 1)).reduce((n, l) => n + l.length + 1, 0);
	ta.focus({ preventScroll: true });
	ta.setSelectionRange(Math.min(pos, ta.value.length), Math.min(pos, ta.value.length));
	const lh = parseFloat(getComputedStyle(ta).lineHeight) || 15;
	ta.scrollTop = Math.max(0, (line - 1) * lh - ta.clientHeight / 3);
	edState();
}

// Out of the editor for this file: what was typed is stored first.
function edLeaveFile() {
	edFlush();
	ED.cur = null;
	ED.diffShown = false;
}

function edToggle() {
	if (ED.on) {
		edLeaveFile();
		ED.on = false;
		edSync();
		renderSourcePanel();
		$("src_edit").focus({ preventScroll: true });
		edChanged();
		return;
	}
	if (!edCan().ok)
		return;
	ED.on = true;
	edEnterFile();
}

// Line numbers: one per line of the text, redrawn when the count changes.
function edGutter(force) {
	const ta = edTa(), g = $("ed_gutter");
	let n = 1;
	for (let i = ta.value.indexOf("\n"); i >= 0; i = ta.value.indexOf("\n", i + 1))
		n++;
	if (force || Number(g.dataset.lines) !== n) {
		g.dataset.lines = n;
		let s = "";
		for (let i = 1; i <= n; i++)
			s += i + "\n";
		g.textContent = s;
	}
	g.scrollTop = ta.scrollTop;
}

function edCaret() {
	const ta = edTa(), at = ta.selectionStart;
	const before = ta.value.lastIndexOf("\n", at - 1);
	let line = 1;
	for (let i = ta.value.indexOf("\n"); i >= 0 && i < at; i = ta.value.indexOf("\n", i + 1))
		line++;
	return { line, col: at - before };
}

function edState(note) {
	if (!ED.cur)
		return;
	const { line, col } = edCaret();
	const e = edMap(ED.cur.kernel).get(ED.cur.path);
	const where = "line " + line + ", column " + col;
	const saved = ED.timer ? "saving…" : !e ? "the text the kernel was built from" :
		ED.db ? "saved in this browser" : "kept until a reload (" + ED.why + ")";
	$("ed_state").textContent = ED.cur.path + ": " + (note || saved) + "; " + where;
}

function edInput() {
	edGutter(false);
	clearTimeout(ED.timer);
	ED.timer = setTimeout(edFlush, ED_SAVE_MS);
	edState();
}

function edFlush() {
	if (!ED.timer || !ED.cur)
		return;
	clearTimeout(ED.timer);
	ED.timer = 0;
	const { kernel, path, base } = ED.cur;
	edPut(kernel, path, base, edTa().value).then(() => {
		edState();
		edChanged();
	}, (e) => edState("not saved: " + e.message));
}

function edKeys(ev) {
	if (ev.key === "Tab" && !ev.shiftKey && !ev.altKey && !ev.ctrlKey && !ev.metaKey) {
		ev.preventDefault();
		ev.stopPropagation();
		const ta = edTa();
		if (!document.execCommand("insertText", false, "\t")) // keeps the browser's undo
			ta.setRangeText("\t", ta.selectionStart, ta.selectionEnd, "end");
		edInput();
	} else if (ev.key === "Escape") {
		ev.preventDefault();
		ev.stopPropagation();
		$("src_edit").focus();
	} else if ((ev.ctrlKey || ev.metaKey) && (ev.key === "s" || ev.key === "S")) {
		ev.preventDefault();
		edFlush();
	}
}

// ---- diff views, revert -----------------------------------------------------

function edStats(path) {
	const e = edMap().get(path);
	if (!e)
		return { add: 0, del: 0 };
	if (e.statsOf !== e.text) {
		const r = edFilePatch(path, e.base, e.text);
		e.stats = { add: r.add, del: r.del };
		e.statsOf = e.text;
	}
	return e.stats;
}

// A patch as coloured lines in pre.
function edDiffInto(pre, patch) {
	pre.textContent = "";
	for (const l of edLines(patch)) {
		const s = document.createElement("span");
		s.className = l.startsWith("@@") ? "edh" : /^(diff |--- |\+\+\+ |new file|deleted file)/.test(l) ? "edf" :
			l[0] === "+" ? "eda" : l[0] === "-" ? "edd" : "";
		s.textContent = l;
		pre.append(s);
	}
}

function edShowPanelDiff() {
	const e = srcPanel && edMap().get(srcPanel.path);
	if (ED.cur)
		edFlush();
	const text = ED.cur ? edTa().value : e && e.text;
	const base = ED.cur ? ED.cur.base : e && e.base;
	const p = srcPanel ? edFilePatch(srcPanel.path, base, text).patch : "";
	edDiffInto($("ed_pdiff"), p || "No change: the text is the one the kernel was built from.\n");
}

function edDiffToggle() {
	ED.diffShown = !ED.diffShown;
	if (ED.diffShown && !ED.on) {
		ED.on = true;
		edEnterFile();
	}
	edSync();
	if (!ED.diffShown && ED.cur)
		edTa().focus({ preventScroll: true });
}

// A revert acts on its second press within ED_ARM_MS. Which one is armed is
// kept here, not on the button: the list is drawn again when the DWARF or
// the source arrives, which replaces its buttons.
function edArmed(key, fn) {
	if (ED.armed !== key) {
		clearTimeout(ED.armTimer);
		ED.armed = key;
		ED.armTimer = setTimeout(() => {
			ED.armed = null;
			edArmLabels();
		}, ED_ARM_MS);
		edArmLabels();
		return;
	}
	clearTimeout(ED.armTimer);
	ED.armed = null;
	edArmLabels();
	fn();
}

function edArmLabels() {
	$("ed_revert_all").textContent = ED.armed === "*" ? "Revert all? Press again" : "Revert all";
	$("ed_revert").textContent = ED.armed === "panel" ? "Revert? Press again" : "Revert";
	for (const b of $("ed_list").querySelectorAll("button.edrevert"))
		b.textContent = ED.armed === "file:" + b.closest("li").dataset.path ? "Revert? Press again" : "Revert";
}

// The overlay in memory and the views change at once; the store after.
function edRevert(paths) {
	const kernel = edKernel(), writes = [];
	for (const p of paths) {
		const e = edMap(kernel).get(p);
		if (e)
			writes.push(edPut(kernel, p, e.base, e.base));
		if (ED.cur && ED.cur.path === p && ED.cur.kernel === kernel) {
			clearTimeout(ED.timer);
			ED.timer = 0;
			if (e && e.base === null) { // a file the patch made: gone
				ED.cur = null;
				ED.on = ED.diffShown = false;
			} else {
				edTa().value = ED.cur.base;
				edGutter(true);
			}
		}
	}
	if (ED.listDiff && paths.includes(ED.listDiff))
		ED.listDiff = null;
	edStatus("Reverted " + paths.join(", ") + ".", "reverted");
	edChanged();
	if (srcPanel && !$("src_panel").hidden)
		renderSourcePanel();
	return Promise.all(writes).catch((e) => edStatus("Reverted here, but not in the store: " + e.message, "reverted"));
}

// ---- the Source tab: the changed files ----------------------------------

function edStatus(text, state) {
	const s = $("ed_status");
	s.textContent = text;
	s.dataset.state = state;
}

function edChanged() {
	edRender();
	if (srcPanel && !$("src_panel").hidden)
		edSync();
}

function edRender() {
	const m = edMap(), paths = [...m.keys()].sort();
	const where = ED.db ? "kept in this browser" : "kept until a reload: " + (ED.why || "IndexedDB not open yet");
	$("ed_count").textContent = paths.length ? paths.length + " changed file" + (paths.length === 1 ? "" : "s") + ", " + where :
		"none for this kernel (Edit in the Source panel, or upload a patch)" + (ED.db ? "" : "; " + where);
	$("ed_download").disabled = !paths.length;
	$("ed_revert_all").disabled = !paths.length;
	const ul = $("ed_list");
	ul.textContent = "";
	for (const p of paths) {
		const e = m.get(p), s = edStats(p);
		const li = document.createElement("li");
		li.dataset.path = p;
		li.dataset.add = s.add;
		li.dataset.del = s.del;
		const open = document.createElement("button");
		open.type = "button";
		open.className = "edopen";
		open.textContent = p;
		open.title = "Open " + p + " in the Source panel's editor";
		open.disabled = e.text === null;
		open.addEventListener("click", () => {
			ED.on = true;
			openSourceFile(p);
		});
		const n = document.createElement("span");
		n.className = "note";
		n.textContent = (e.base === null ? " new," : e.text === null ? " deleted," : "") + " +" + s.add + " -" + s.del + " ";
		const diff = document.createElement("button");
		diff.type = "button";
		diff.className = "eddiffbtn";
		diff.textContent = ED.listDiff === p ? "Hide diff" : "Diff";
		diff.setAttribute("aria-pressed", ED.listDiff === p);
		diff.addEventListener("click", () => {
			ED.listDiff = ED.listDiff === p ? null : p;
			edRender();
		});
		const rev = document.createElement("button");
		rev.type = "button";
		rev.className = "edrevert";
		rev.textContent = "Revert";
		rev.title = "Throw away your edits of " + p + " (press twice)";
		rev.addEventListener("click", () => edArmed("file:" + p, () => edRevert([p])));
		li.append(open, n, diff, " ", rev);
		ul.append(li);
	}
	edArmLabels();
	const pre = $("ed_diff");
	pre.hidden = !ED.listDiff || !m.has(ED.listDiff);
	if (!pre.hidden)
		edDiffInto(pre, edFilePatch(ED.listDiff, m.get(ED.listDiff).base, m.get(ED.listDiff).text).patch);
}

function edDownload() {
	edFlush();
	const kernel = edKernel(), text = edPatch(kernel);
	if (!text)
		return;
	const a = document.createElement("a");
	a.href = URL.createObjectURL(new Blob([text], { type: "text/x-diff" }));
	a.download = kernel.replace(/[^\w.-]+/g, "_") + "-edits.patch";
	a.click();
	setTimeout(() => URL.revokeObjectURL(a.href), 10000);
	const n = edMap(kernel).size;
	edStatus("Downloaded " + a.download + ": " + n + " file" + (n === 1 ? "" : "s") + "; in the kernel's tree: git apply " + a.download, "downloaded");
}

async function edUpload() {
	const inp = $("ed_upload"), f = inp.files[0];
	if (!f)
		return;
	inp.value = "";
	edFlush();
	try {
		const done = await edApply(await f.text());
		if (ED.cur && done.some((d) => d.path === ED.cur.path)) {
			const d = done.find((x) => x.path === ED.cur.path);
			if (d.text === null)
				edPanelShown();
			else {
				edTa().value = d.text;
				edGutter(true);
			}
		}
		edStatus("Applied " + f.name + ": " + done.map((d) => d.path + (d.base === null ? " (new)" : d.text === null ? " (deleted)" : "")).join(", ") + ".", "applied");
		if (srcPanel && !$("src_panel").hidden)
			renderSourcePanel();
	} catch (e) {
		edStatus("Not applied, no file changed: " + e.message, "refused");
	}
}

// ---- the API for the build (E3, E4) ------------------------------------------

const srcOverlay = {
	ready: null,
	// The changed files of the shown kernel (deleted ones too), sorted.
	paths: () => [...edMap().keys()].sort(),
	// A file's text with the overlay applied (null: deleted or not in the tree).
	read: edRead,
	// Its text in the tree the kernel was built from (null: a new file).
	original: edOriginal,
	// Set a file's text (null deletes it); equal to the original drops the edit.
	async write(path, text) {
		const base = await edOriginal(path);
		await edPut(edKernel(), path, base, text);
		edChanged();
	},
	revert: (path) => edRevert([path]),
	// Every edit as one git patch (a/ b/ paths in the kernel's tree).
	patch: () => edPatch(),
	apply: edApply,
};

function initSrcEdit() {
	ED.ready = srcOverlay.ready = edLoad().then(() => edChanged());
	$("src_edit").addEventListener("click", edToggle);
	$("ed_diffbtn").addEventListener("click", edDiffToggle);
	$("ed_revert").addEventListener("click", () => edArmed("panel", () => srcPanel && edRevert([srcPanel.path])));
	const ta = edTa();
	ta.addEventListener("input", edInput);
	ta.addEventListener("keydown", edKeys);
	ta.addEventListener("scroll", () => { $("ed_gutter").scrollTop = ta.scrollTop; });
	ta.addEventListener("blur", edFlush);
	for (const t of ["click", "keyup", "select"])
		ta.addEventListener(t, () => edState());
	$("ed_download").addEventListener("click", edDownload);
	$("ed_upload_btn").addEventListener("click", () => $("ed_upload").click());
	$("ed_upload").addEventListener("change", edUpload);
	$("ed_revert_all").addEventListener("click", () => edArmed("*", () => edRevert(srcOverlay.paths())));
	window.addEventListener("pagehide", edFlush);
	dbgOnChange(() => {
		edRender();
		if (srcPanel && !$("src_panel").hidden) {
			edSync();
			if (ED.on && !ED.cur)
				edEnterFile();
		}
	});
}
