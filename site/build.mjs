// The page's bundle (R3): esbuild bundles app.js, the modules it imports,
// the React components (*.jsx) and React into app.bundle.js, an ES module,
// with a source map (app.bundle.js.map) that names the files it came from.
// index.html loads it; jscov follows the map, so coverage is reported per
// source file (README.md, "Scripts").
//
// clock.js stays its own module: index.html loads it before v86's
// libv86.js (v86 takes its clock when it loads), and the bundle imports the
// same URL, so there is one clock.
//
// Only whitespace is minified: nothing is renamed and the code is not
// lowered (target esnext), so the bundle is the modules' own code in
// import order, without the indentation and line breaks (430 KB of 1.6 MB:
// parsing them cost the page ~30 ms on the way to the boot, R3). The same
// sources and esbuild version give the same bytes; CI checks that with
// --check, which builds again in memory and compares with the files on disk.
//
// Usage: cd site && npm ci && node build.mjs [--check]
import * as esbuild from "esbuild";
import { readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { fileURLToPath } from "node:url";
import path from "node:path";

const SITE = path.dirname(fileURLToPath(import.meta.url));
const OUT = "app.bundle.js";

// The one module the bundle does not hold.
const EXTERNAL = new Set(["./clock.js"]);
const keepExternal = {
	name: "mkb-external",
	setup(build) {
		build.onResolve({ filter: /^\.\/clock\.js$/ }, (args) => (EXTERNAL.has(args.path) ? { path: args.path, external: true } : null));
	},
};

const OPTIONS = {
	absWorkingDir: SITE,
	entryPoints: ["app.js"],
	outfile: OUT,
	bundle: true,
	format: "esm",
	platform: "browser",
	target: "esnext",
	jsx: "automatic",
	charset: "utf8",
	minifyWhitespace: true,
	sourcemap: "linked",
	sourcesContent: true,
	legalComments: "eof",
	logLevel: "warning",
	define: { "process.env.NODE_ENV": '"production"' },
	plugins: [keepExternal],
};

const sha = (b) => createHash("sha256").update(b).digest("hex").slice(0, 16);

async function main() {
	const check = process.argv.includes("--check");
	const r = await esbuild.build({ ...OPTIONS, write: !check, metafile: true });
	if (r.errors.length)
		process.exit(1);
	const inputs = Object.keys(r.metafile.inputs);
	const own = inputs.filter((p) => !p.startsWith("node_modules/"));
	if (check) {
		const bad = [];
		for (const f of r.outputFiles) {
			const name = path.relative(SITE, f.path);
			let disk = null;
			try {
				disk = readFileSync(f.path);
			} catch (e) {
				disk = null;
			}
			if (!disk || Buffer.compare(disk, Buffer.from(f.contents)) !== 0)
				bad.push(`${name}: built ${sha(f.contents)}, on disk ${disk ? sha(disk) : "missing"}`);
		}
		if (bad.length) {
			console.error("build.mjs --check: the bundle is not what these sources build to:\n" + bad.join("\n"));
			process.exit(1);
		}
		console.log(`build.mjs --check: ${OUT} and its map are reproducible (${own.length} own files, ${inputs.length - own.length} from node_modules)`);
		return;
	}
	const out = r.metafile.outputs[OUT];
	console.log(`${OUT}: ${out.bytes} bytes, sha256 ${sha(readFileSync(path.join(SITE, OUT)))}; ${own.length} own files, ` +
		`${inputs.length - own.length} from node_modules; external: ${[...EXTERNAL].join(", ")}`);
}

main().catch((e) => {
	console.error(e.stack || String(e));
	process.exit(1);
});
