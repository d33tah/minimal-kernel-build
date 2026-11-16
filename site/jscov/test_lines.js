#!/usr/bin/env node
// Self-test of which lines report.js counts as run (lines.js), with real V8
// coverage of a real esbuild bundle (R3):
//
// - a line whose code never ran is not run, also with a trailing comment or
//   with case labels on it (MCR called those "partly run"): the jscov rows
//   that were made "sometimes:" after the bundle came (12, R3) were such
//   lines, run in no CI run before or after;
// - a line that ran only in an unbundled copy (a Worker's) of a file the
//   bundle holds too is run, in a function the bundle never called (MCR's
//   merge of a mapped and a plain copy lost it: unwind.js:418 and 555);
// - a line the bundle ran is run (the bundle's ranges through its map).
//
// Usage: node site/jscov/test_lines.js   (needs npm ci in site and in site/jscov)

"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");
const inspector = require("inspector");
const { pathToFileURL } = require("url");
const { execFileSync } = require("child_process");

const SITE = path.resolve(__dirname, "..");
const esbuild = require(path.join(SITE, "node_modules", "esbuild"));

const MOD = `// a module the bundle and a Worker both run
export function op(x) {
	switch (x) {
	case 1: return 10; // one
	case 2: case 3: return 20;
	case 4: return 40; // four
	}
	return 0;
}

export function onlyWorker(x) {
	if (x > 1)
		return "big";
	return "small";
}

export function neverRun() {
	return -1;
}
`;
// The bundle holds every function of m.js (none shaken out) and calls op.
const APP = `import { op, onlyWorker, neverRun } from "./m.js";
globalThis.out = [op(1), op(9)];
globalThis.fns = [onlyWorker, neverRun];
`;
const WORKER = `import { op, onlyWorker } from "./m.js";
globalThis.out2 = [op(4), onlyWorker(5)];
`;

const line = (text) => MOD.split("\n").indexOf(text) + 1;

// V8's block coverage of the scripts a function loads, as jscov.py writes
// it: { file, len, functions } per script.
async function covered(run, files) {
	const s = new inspector.Session();
	s.connect();
	const post = (m, p) => new Promise((ok, bad) => s.post(m, p || {}, (e, r) => (e ? bad(e) : ok(r))));
	await post("Profiler.enable");
	await post("Profiler.startPreciseCoverage", { callCount: true, detailed: true });
	await run();
	const { result } = await post("Profiler.takePreciseCoverage");
	await post("Profiler.stopPreciseCoverage");
	s.disconnect();
	const out = [];
	for (const r of result)
		for (const [name, p] of Object.entries(files))
			if (r.url === pathToFileURL(p).href)
				out.push({ file: name, len: fs.readFileSync(p, "utf8").length, functions: r.functions });
	return out;
}

async function main() {
	const tmp = fs.mkdtempSync(path.join(process.env.MKB_TMP || os.tmpdir(), "jscov-lines-"));
	const site = path.join(tmp, "site"), raw = path.join(tmp, "raw"), work = path.join(tmp, "work");
	for (const d of [site, raw, work])
		fs.mkdirSync(d);
	fs.writeFileSync(path.join(site, "m.js"), MOD);
	fs.writeFileSync(path.join(site, "app.js"), APP);
	await esbuild.build({ absWorkingDir: site, entryPoints: ["app.js"], outfile: path.join(site, "app.bundle.js"), bundle: true,
		format: "esm", target: "esnext", sourcemap: "linked", sourcesContent: true, logLevel: "warning" });
	// The "Worker": m.js run as it is, from a copy (another URL; the same text).
	fs.writeFileSync(path.join(work, "m.js"), MOD);
	fs.writeFileSync(path.join(work, "w.js"), WORKER);
	const recs = [
		...(await covered(() => import(pathToFileURL(path.join(site, "app.bundle.js")).href), { "app.bundle.js": path.join(site, "app.bundle.js") })),
		...(await covered(() => import(pathToFileURL(path.join(work, "w.js")).href), { "m.js": path.join(work, "m.js") })),
	];
	if (recs.length !== 2)
		throw new Error(`want a record of the bundle and one of m.js, got ${recs.map((r) => r.file)}`);
	fs.writeFileSync(path.join(raw, "raw-selftest.jsonl"), recs.map((r) => JSON.stringify(r)).join("\n") + "\n");
	fs.unlinkSync(path.join(site, "app.js")); // the report's scripts: m.js (app.js's code is the bundle's)
	execFileSync("node", [path.join(__dirname, "report.js"), "--src", site, "--out", path.join(tmp, "out"), raw], { stdio: "inherit" });
	const s = JSON.parse(fs.readFileSync(path.join(tmp, "out", "summary.json"), "utf8"));
	const unc = new Set();
	for (const part of (s.files["m.js"] || {}).uncoveredLines.split(",").filter(Boolean)) {
		const [a, b] = part.split("-").map(Number);
		for (let n = a; n <= (b || a); n++)
			unc.add(n);
	}
	const want = [
		["\tcase 1: return 10; // one", false, "ran in the bundle"],
		["\tcase 2: case 3: return 20;", true, "never ran (two case labels on the line)"],
		["\tcase 4: return 40; // four", false, "ran only in the Worker's copy"],
		["\treturn 0;", false, "ran in the bundle"],
		["\t\treturn \"big\";", false, "ran only in the Worker's copy, in a function the bundle never called"],
		["\treturn \"small\";", true, "never ran"],
		["\treturn -1;", true, "never ran"],
	];
	const bad = want.filter(([t, u]) => unc.has(line(t)) !== u);
	fs.rmSync(tmp, { recursive: true, force: true });
	if (bad.length) {
		for (const [t, u, why] of bad)
			console.error(`m.js:${line(t)} ${JSON.stringify(t.trim())}: ${why}, the report has it ${u ? "run" : "not run"}`);
		console.error("FAIL");
		process.exit(1);
	}
	console.log(`PASS (m.js uncovered lines ${[...unc].sort((a, b) => a - b).join(",")})`);
}

main().catch((e) => {
	console.error(e.stack || String(e));
	process.exit(1);
});
