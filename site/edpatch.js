"use strict";

// Diffs and patches of the source editor (srcedit.js): unified diffs of
// the edited files, and reading and applying a patch (test_srcedit.mjs
// checks both against git).

const ED_DIFF_MAX = 2000;        // Myers' D limit: more differences give one hunk of the whole middle
const ED_CONTEXT = 3;            // lines of context in a hunk (diff -u, git)

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

export { ED_DIFF_MAX, edLines, edDiff, edFilePatch, edParse, edApplyHunks };
