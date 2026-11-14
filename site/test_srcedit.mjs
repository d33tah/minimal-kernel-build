// srcedit.js's diff, patch writer and patch reader against git (E2):
// random texts and random edits (lines added, removed, changed, the last
// newline taken or given, new and deleted files, empty files). Each patch
// srcedit.js writes must `git apply` to the edited text, and each patch git
// writes must give the edited text through srcedit.js's reader.
// Usage: node site/test_srcedit.mjs [ROUNDS]
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import vm from "node:vm";

const here = path.dirname(new URL(import.meta.url).pathname);
const ctx = vm.createContext({});
vm.runInContext(fs.readFileSync(path.join(here, "srcedit.js"), "utf8") +
	"\nthis.T = { edLines, edDiff, edFilePatch, edParse, edApplyHunks };", ctx);
const { edFilePatch, edParse, edApplyHunks } = ctx.T;

const rounds = Number(process.argv[2] || 300);
let seed = 12345;
const rnd = (n) => {
	seed = (seed * 1103515245 + 12345) & 0x7fffffff;
	return seed % n;
};
const WORDS = ["{", "}", "return 0;", "int x;", "", "\tfoo();", "/* c */", "#include <a.h>", "x++;", "if (x)"];
const text = (n, eol) => {
	const l = [];
	for (let i = 0; i < n; i++)
		l.push(WORDS[rnd(WORDS.length)]);
	return l.join("\n") + (n && eol ? "\n" : "");
};
const edit = (t) => {
	const l = t.split("\n");
	const n = 1 + rnd(6);
	for (let i = 0; i < n; i++) {
		const at = rnd(l.length + 1), what = rnd(3);
		if (what === 0)
			l.splice(at, 0, "added " + rnd(1000));
		else if (what === 1 && l.length > 1)
			l.splice(Math.min(at, l.length - 1), 1);
		else if (l.length)
			l[Math.min(at, l.length - 1)] += " changed";
	}
	let s = l.join("\n");
	if (rnd(5) === 0)
		s = s.endsWith("\n") ? s.slice(0, -1) : s + "\n";
	return s;
};

const dir = fs.mkdtempSync(path.join(os.tmpdir(), "srcedit-"));
const git = (...a) => execFileSync("git", ["-c", "user.name=t", "-c", "user.email=t@t", ...a], { cwd: dir, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] });
let bad = 0, n = 0;
const fail = (what) => {
	bad++;
	if (bad <= 5)
		console.log("FAIL " + what);
};
try {
	git("init", "-q");
	for (let r = 0; r < rounds; r++) {
		const kind = rnd(10);  // 0: new file, 1: deleted file, else changed
		const base = kind === 0 ? null : text(rnd(40), rnd(4) !== 0);
		const after = kind === 1 ? null : kind === 0 ? text(rnd(10), rnd(4) !== 0) : edit(base);
		const p = "d" + r + "/f.c";
		// srcedit.js's patch through git apply
		fs.rmSync(path.join(dir, "d" + r), { recursive: true, force: true });
		if (base !== null) {
			fs.mkdirSync(path.join(dir, "d" + r));
			fs.writeFileSync(path.join(dir, p), base);
		}
		const mine = edFilePatch(p, base, after).patch;
		if (base === after)
			continue;
		n++;
		fs.writeFileSync(path.join(dir, "mine.patch"), mine);
		try {
			git("apply", "mine.patch");
			const got = fs.existsSync(path.join(dir, p)) ? fs.readFileSync(path.join(dir, p), "utf8") : null;
			if (got !== after)
				fail(`round ${r}: git apply of srcedit.js's patch gives other text\n${mine}`);
		} catch (e) {
			fail(`round ${r}: git apply refused srcedit.js's patch: ${e.stderr}\n${mine}`);
		}
		// git's patch through srcedit.js's reader
		fs.rmSync(path.join(dir, "d" + r), { recursive: true, force: true });
		if (base !== null) {
			fs.mkdirSync(path.join(dir, "d" + r));
			fs.writeFileSync(path.join(dir, p), base);
			git("add", p);
			git("commit", "-q", "-m", "r" + r);
		}
		if (after === null)
			fs.rmSync(path.join(dir, p));
		else {
			fs.mkdirSync(path.join(dir, "d" + r), { recursive: true });
			fs.writeFileSync(path.join(dir, p), after);
			if (base === null)
				git("add", "-N", p);
		}
		const theirs = git("diff", "--", p);
		const files = edParse(theirs);
		const f = files[0];
		const got = f.to === null ? null : edApplyHunks(p, base || "", f.hunks);
		if (files.length !== 1 || f.path !== p || got !== after || (f.from === null) !== (base === null))
			fail(`round ${r}: srcedit.js read git's patch wrong (${files.length} files, path ${f.path})\n${theirs}`);
		git("add", "-A", "d" + r);
		git("commit", "-q", "--allow-empty", "-m", "s" + r);
	}
	// A hunk that does not match is refused, naming the file.
	try {
		edApplyHunks("x.c", "a\nb\nc\n", edParse("--- a/x.c\n+++ b/x.c\n@@ -1,2 +1,2 @@\n a\n-q\n+r\n")[0].hunks);
		fail("a hunk that does not match was applied");
	} catch (e) {
		if (!/x\.c: hunk 1/.test(e.message))
			fail("the refusal does not name the file and hunk: " + e.message);
	}
	// The same hunk moved by lines added above it still applies (offset).
	const moved = edApplyHunks("x.c", "new\nnew\na\nb\nc\n", edParse("--- a/x.c\n+++ b/x.c\n@@ -1,3 +1,3 @@\n a\n-b\n+B\n c\n")[0].hunks);
	if (moved !== "new\nnew\na\nB\nc\n")
		fail("an offset hunk: " + JSON.stringify(moved));
	for (const junk of ["", "hello\n", "@@ -1 +1 @@\n-a\n+b\n", "--- a/x\n+++ b/x\n@@ -1,3 +1,3 @@\n a\n"])
		try {
			edParse(junk);
			if (junk.includes("@@ -1,3"))
				fail("a patch cut inside a hunk was read");
			else
				fail("not a patch, read as one: " + JSON.stringify(junk));
		} catch (e) {
			// refused, as it should be
		}
} finally {
	fs.rmSync(dir, { recursive: true, force: true });
}
console.log(`${n} patches each way, ${bad} failed`);
process.exit(bad ? 1 : 0);
