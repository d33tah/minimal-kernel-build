#!/usr/bin/env node
// Merge the raw coverage that site/jscov.py wrote (one or more directories
// of raw-*.jsonl, from every browser-test job) into a report of the site's
// own scripts: an HTML report (MCR's V8 UI), summary.json (totals and per
// file: lines, functions, branches, plus every function that never ran)
// and summary.md (for the job summary). With --baseline, exit 1 when a
// total is below the committed floor. With --uncovered (site/jscov-uncovered.tsv,
// format in listed.js: file, line ranges, code hash, why those lines do not
// run in the tests), the uncovered lines the list does not name
// ("unlisted"), the listed lines that ran or are no code ("stale"), rows
// whose code moved or changed and rows still marked TODO are reported per
// file, and uncovered-candidates.tsv in the output directory is the list
// as it would be true for this run (moved rows renumbered, lines that ran
// taken out, a TODO row per new uncovered block). With --require-listed,
// any of those fails the run.
//
// Usage: node site/jscov/report.js --src site --out jscov-report [--baseline site/jscov-baseline.json]
//        [--uncovered site/jscov-uncovered.tsv [--require-listed]] [--summary $GITHUB_STEP_SUMMARY] RAW_DIR...
// Needs: npm ci in site/jscov (monocart-coverage-reports, pinned by package-lock.json).

"use strict";

const fs = require("fs");
const path = require("path");
const { CoverageReport } = require("monocart-coverage-reports");
const { mergeV8Coverage } = require("monocart-coverage-reports/util");
const L = require("./listed.js");

const METRICS = ["lines", "functions", "branches"];

function args(argv) {
	const a = { raw: [] };
	for (let i = 0; i < argv.length; i++) {
		const k = argv[i];
		if (k === "--src" || k === "--out" || k === "--baseline" || k === "--summary" || k === "--uncovered")
			a[k.slice(2)] = argv[++i];
		else if (k === "--require-listed")
			a.requireListed = true;
		else if (k.startsWith("--"))
			throw new Error("unknown option " + k);
		else
			a.raw.push(k);
	}
	if (!a.src || !a.out || !a.raw.length)
		throw new Error("usage: report.js --src SITE --out DIR [--baseline FILE] [--summary FILE] RAW_DIR...");
	return a;
}

// The page's scripts: every top-level *.js of the site but node tools (#!).
function siteScripts(src) {
	return fs.readdirSync(src).filter((n) => n.endsWith(".js") && fs.statSync(path.join(src, n)).isFile())
		.filter((n) => !fs.readFileSync(path.join(src, n), "utf8").startsWith("#!")).sort();
}

function rawFiles(dirs) {
	const out = [];
	const walk = (d) => {
		for (const e of fs.readdirSync(d, { withFileTypes: true })) {
			const p = path.join(d, e.name);
			if (e.isDirectory())
				walk(p);
			else if (e.name.endsWith(".jsonl"))
				out.push(p);
		}
	};
	dirs.forEach(walk);
	return out.sort();
}

function pct(c, t) {
	return t ? Math.floor((c / t) * 10000) / 100 : 100;
}

function lineOf(source, offset) {
	let n = 1;
	for (let i = 0; i < offset && i < source.length; i++)
		if (source.charCodeAt(i) === 10)
			n++;
	return n;
}

const { lineSet, ranges } = L;

// Uncovered code lines -> blocks: lines with no covered code line between
// them are one block (blank lines and comments do not split it).
function blocks(unc, codeLines) {
	const covered = [...codeLines].filter((n) => !unc.has(n)).sort((x, y) => x - y);
	const out = [];
	let cur = null, k = 0;
	for (const n of [...unc].sort((x, y) => x - y)) {
		let split = !cur;
		while (k < covered.length && covered[k] < n) {
			split = true;
			k++;
		}
		if (split)
			out.push(cur = []);
		cur.push(n);
	}
	return out;
}

async function main() {
	const a = args(process.argv.slice(2));
	const src = path.resolve(a.src);
	const out = path.resolve(a.out);
	const names = siteScripts(src);
	const sources = Object.fromEntries(names.map((n) => [n, fs.readFileSync(path.join(src, n), "utf8")]));

	// Group the records by file; a record of a file whose length differs
	// from the source here ran another version of it (a test's modified
	// copy): counted, not used.
	const byFile = {}, foreign = {};
	let records = 0;
	const files = rawFiles(a.raw);
	for (const f of files) {
		for (const line of fs.readFileSync(f, "utf8").split("\n")) {
			if (!line)
				continue;
			const r = JSON.parse(line);
			records++;
			const n = r.file in sources ? sources[r.file].length : -1;
			if (r.len !== undefined ? n !== r.len : !(r.end <= n)) {
				foreign[r.file] = (foreign[r.file] || 0) + 1;
				continue;
			}
			// A take without the top-level function (coverage started after
			// it ran): a root range of count 0, so the code it did not report
			// counts as not run, not as covered by default. A service worker
			// (coverage that started late: the collector without
			// jscov.HOLD_SW_JS, or another client's session) is the
			// exception: its handlers exist only once its top level ran, so
			// a record with a function that ran means the top level ran
			// (sw.js's top level has no branch to miss).
			const ranAny = r.kind === "service_worker" && r.functions.some((f) => f.ranges[0].count > 0);
			const functions = r.functions.some((f) => f.ranges[0].startOffset === 0 && f.ranges[0].endOffset === n) ? r.functions
				: [{ functionName: "", isBlockCoverage: true, ranges: [{ startOffset: 0, endOffset: n, count: ranAny ? 1 : 0 }] }, ...r.functions];
			(byFile[r.file] = byFile[r.file] || []).push({ functions });
		}
	}
	console.log(`${files.length} raw files, ${records} records, ${Object.keys(byFile).length} of ${names.length} scripts ran`);
	if (Object.keys(foreign).length)
		console.log("records of other versions (ignored):", JSON.stringify(foreign));

	const entries = [];
	for (const [n, list] of Object.entries(byFile)) {
		const merged = list.length > 1 ? await mergeV8Coverage(list) : list[0];
		entries.push({ scriptId: String(entries.length + 1), url: "file://" + path.join(src, n), source: sources[n], functions: merged.functions });
	}

	let results;
	const mcr = new CoverageReport({
		name: "Kernel in the browser: e2e coverage of site/*.js",
		outputDir: out,
		baseDir: src,
		logging: "error",
		reports: [["v8"], ["console-summary"]],
		entryFilter: (e) => names.includes(path.basename(e.url)) && path.dirname(e.url.replace(/^file:\/\//, "")) === src,
		sourceFilter: (p) => names.includes(path.basename(p)),
		all: { dir: [src], filter: (p) => names.includes(path.basename(p)) && path.dirname(path.resolve(src, p)) === src },
		onEnd: (r) => { results = r; },
	});
	mcr.cleanCache();
	await mcr.add(entries);
	await mcr.generate();

	const summary = { files: {}, total: {}, scripts: names.length, ran: Object.keys(byFile).length, records, foreign };
	const codeLines = {};
	const sum = Object.fromEntries(METRICS.map((m) => [m, { covered: 0, total: 0 }]));
	for (const f of results.files) {
		const name = path.basename(f.sourcePath);
		const s = {};
		for (const m of METRICS) {
			const { covered, total } = f.summary[m];
			s[m] = { covered, total, pct: pct(covered, total) };
			sum[m].covered += covered;
			sum[m].total += total;
		}
		const source = sources[name] || "";
		s.uncoveredFunctions = (f.data && f.data.functions || []).filter((r) => !r.count && !r.ignored)
			.map((r) => ({ name: r.name || "(anonymous)", line: lineOf(source, r.start) }));
		// Line numbers of the code lines that never ran, as "a-b" ranges.
		s.uncoveredLines = ranges(Object.entries(f.data && f.data.lines || {}).filter(([, c]) => !c).map(([l]) => +l));
		codeLines[name] = new Set(Object.keys(f.data && f.data.lines || {}).map(Number));
		s.sourceLines = source.split("\n").length - (source.endsWith("\n") ? 1 : 0);
		summary.files[name] = s;
	}
	for (const m of METRICS)
		summary.total[m] = { ...sum[m], pct: pct(sum[m].covered, sum[m].total) };
	summary.total.sourceLines = Object.values(summary.files).reduce((t, s) => t + s.sourceLines, 0);
	const missing = names.filter((n) => !(n in summary.files));
	if (missing.length)
		throw new Error("no report for " + missing.join(", "));

	let failed = [];
	if (a.baseline) {
		const base = JSON.parse(fs.readFileSync(a.baseline, "utf8"));
		for (const m of METRICS)
			if (summary.total[m].pct < base[m])
				failed.push(`${m} ${summary.total[m].pct}% < baseline ${base[m]}%`);
		// "fileLines": the floor of every script's own line coverage.
		if (typeof base.fileLines === "number")
			for (const [n, f] of Object.entries(summary.files))
				if (f.lines.pct < base.fileLines)
					failed.push(`${n} lines ${f.lines.pct}% < the per-file floor ${base.fileLines}%`);
		summary.baseline = base;
	}
	let listMsg = [];
	if (a.uncovered) {
		const list = path.basename(a.uncovered);
		const { header, rows } = L.readListed(a.uncovered);
		const { moved, lost } = L.relocate(rows, sources);
		const todo = rows.filter(L.isTodo);
		const listed = {}, sometimes = {};
		for (const r of rows.filter((r) => !lost.includes(r)))
			for (const x of r.lines) {
				(listed[r.file] = listed[r.file] || new Set()).add(x);
				if (L.isSometimes(r))
					(sometimes[r.file] = sometimes[r.file] || new Set()).add(x);
			}
		let unlisted = 0, stale = 0;
		const candidates = [];
		for (const [n, f] of Object.entries(summary.files)) {
			const unc = lineSet(f.uncoveredLines), l = listed[n] || new Set(), sm = sometimes[n] || new Set();
			const un = [...unc].filter((x) => !l.has(x));
			f.unlisted = ranges(un);
			f.stale = ranges([...l].filter((x) => !unc.has(x) && !sm.has(x)));
			unlisted += un.length;
			stale += lineSet(f.stale).size;
			const src = L.splitLines(sources[n]);
			for (const b of blocks(new Set(un), codeLines[n]))
				candidates.push({ file: n, lines: new Set(b), code: L.codeOf(src, b),
					reason: "TODO: add a test that runs these lines, or write why they cannot run in CI (" +
						src[b[0] - 1].trim().replace(/\t/g, " ").slice(0, 70) + ")" });
		}
		// The list made true for this run: rows cut to the lines that are
		// still uncovered (with their hash again), rows whose lines all ran
		// dropped, rows whose code is gone kept as they are.
		for (const r of rows) {
			if (lost.includes(r) || L.isSometimes(r)) {
				candidates.push(r);
				continue;
			}
			const unc = lineSet(summary.files[r.file].uncoveredLines);
			const keep = [...r.lines].filter((x) => unc.has(x));
			if (keep.length)
				candidates.push({ ...r, lines: new Set(keep), code: L.codeOf(L.splitLines(sources[r.file]), keep) });
		}
		const first = (r) => Math.min(...r.lines);
		candidates.sort((x, y) => (x.file < y.file ? -1 : x.file > y.file ? 1 : first(x) - first(y)));
		L.writeListed(path.join(out, "uncovered-candidates.tsv"), header, candidates);
		Object.assign(summary, { unlisted, stale, moved: moved.length, changed: lost.length, todo: todo.length });
		const where = (r) => `${r.file}:${ranges(r.lines)}`;
		const files = (k) => Object.keys(summary.files).sort().filter((n) => summary.files[n][k])
			.map((n) => `${n}:${summary.files[n][k]}`);
		const problems = [
			unlisted && `${unlisted} uncovered lines are not in ${list}: ${files("unlisted").join(" ")}`,
			stale && `${stale} listed lines ran (or are no code): ${files("stale").join(" ")}`,
			moved.length && `${moved.length} rows moved with the code above them: ` +
				moved.map((m) => `${m.row.file}:${m.from}->${ranges(m.row.lines)}`).join(" "),
			lost.length && `${lost.length} rows whose code changed or is gone (${list} lines ${lost.map((r) => r.at).join(", ")}): ` +
				lost.map(where).join(" "),
			todo.length && `${todo.length} rows still say TODO: ${todo.map(where).join(" ")}`,
		].filter(Boolean);
		if (problems.length) {
			const run = process.env.GITHUB_RUN_ID ? ` ${process.env.GITHUB_RUN_ID}` : "";
			listMsg = [
				`${list} is not true for this commit:`,
				...problems.map((p) => "- " + p),
				"",
				"How to make it true (site/README.md, e2e coverage):",
				"- only moved rows: `node site/jscov/relist.js`, commit the list with the code;",
				`- otherwise \`gh run download${run} -n jscov-report\`: its uncovered-candidates.tsv is the list made true for ` +
					"this run (moved rows renumbered, lines that ran taken out, a TODO row per new uncovered block). For each TODO " +
					"row add a test that runs the lines (preferred) or replace TODO by why they cannot run in CI, check rows " +
					"whose code changed, then copy it over site/jscov-uncovered.tsv and commit;",
				"- lines that run only in some runs (timing): a reason starting with \"sometimes:\".",
			];
			if (a.requireListed)
				failed.push(`${list} is not true for this commit (see above)`);
		}
	}
	summary.failed = failed;
	fs.writeFileSync(path.join(out, "summary.json"), JSON.stringify(summary, null, 1) + "\n");

	const t = summary.total;
	const row = (n, s) => `| ${n} | ${METRICS.map((m) => `${s[m].pct}% (${s[m].covered}/${s[m].total})`).join(" | ")} |`;
	const md = [
		"## e2e coverage of the site's JavaScript",
		"",
		`${t.lines.covered} of ${t.lines.total} code lines ran (${t.sourceLines} lines of source in ${names.length} scripts; ` +
			`${summary.ran} scripts ran). Baseline: ${a.baseline ? METRICS.map((m) => `${m} ${summary.baseline[m]}%`).join(", ") +
				(typeof summary.baseline.fileLines === "number" ? `, every file's lines ${summary.baseline.fileLines}%` : "") : "none"}.`,
		failed.length ? `\n**FAIL:** ${failed.join("; ")}` : "",
		"",
		"| file | lines | functions | branches |",
		"| --- | --- | --- | --- |",
		row("**total**", t),
		...Object.keys(summary.files).sort().map((n) => row(n, summary.files[n])),
		"",
		...(a.uncovered ? [
			`Uncovered lines ${path.basename(a.uncovered)} does not explain: ${summary.unlisted}; listed lines that ran ` +
				`or are no code: ${summary.stale}; rows moved ${summary.moved}, changed ${summary.changed}, TODO ${summary.todo}.`,
			"",
			...(listMsg.length ? [`**${listMsg[0]}**`, "", ...listMsg.slice(1)] : []),
			"",
		] : []),
	].join("\n");
	fs.writeFileSync(path.join(out, "summary.md"), md);
	if (a.summary)
		fs.appendFileSync(a.summary, md);
	console.log(`total: ${METRICS.map((m) => `${m} ${t[m].pct}% (${t[m].covered}/${t[m].total})`).join(", ")}; ${t.sourceLines} source lines` +
		(a.uncovered ? `; ${summary.unlisted} uncovered lines unlisted, ${summary.stale} listed lines stale, rows moved ` +
			`${summary.moved}, changed ${summary.changed}, TODO ${summary.todo}` : ""));
	if (listMsg.length)
		console.log((a.requireListed ? "FAIL: " : "") + listMsg.join("\n"));
	if (failed.length) {
		console.error("FAIL: " + failed.join("; "));
		process.exit(1);
	}
}

main().catch((e) => {
	console.error(e.stack || String(e));
	process.exit(1);
});
