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
// The page's own code is not minified or renamed and not lowered (target
// esnext): the bundle is the modules' own code in import order (React's
// files are minified, below). The same sources and esbuild version give
// the same bytes; CI checks that with --check, which builds again in memory
// and compares with the files on disk.
//
// index.html is made here too: index.src.html with the tabs' static markup
// (markup/static.js: JSX through markup/html.js, run at build time) and the
// Machine tab's build-time render (markup/prerender.jsx: its React components
// through react-dom/server, hydrated at load) written into its empty
// elements, so the browser parses and lays it out while the bundle
// downloads.
//
// Usage: cd site && npm ci && node build.mjs [--check]
import * as esbuild from "esbuild";
import { readFileSync, writeFileSync } from "node:fs";
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

// React's production files are its plain source (react-dom-client is
// 542 kB). The page's own code stays as written (jscov maps it line by
// line); React's is minified before it goes in: V8 parses React's module
// function on the main thread when the bundle first requires it, ~30 ms
// of every page load for the unminified text.
const minifyDeps = {
	name: "mkb-minify-deps",
	setup(build) {
		build.onLoad({ filter: /[\\/]node_modules[\\/].*\.js$/ }, async (args) => {
			const r = await esbuild.transform(readFileSync(args.path, "utf8"), { loader: "js", minify: true, charset: "utf8",
				legalComments: "inline" });
			return { contents: r.code, loader: "js" };
		});
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
	sourcemap: "linked",
	sourcesContent: true,
	legalComments: "eof",
	logLevel: "warning",
	define: { "process.env.NODE_ENV": '"production"' },
	plugins: [keepExternal, minifyDeps],
};

const sha = (b) => createHash("sha256").update(b).digest("hex").slice(0, 16);

// index.html: index.src.html with every element markup/static.js names
// (empty there) filled with its component's HTML.
const PAGE_SRC = "index.src.html", PAGE = "index.html";

// markup/prerender.jsx renders the Machine tab's components in node: the page's
// modules they import for their handlers (runto.js, kernel.js, ...) would
// start the page (window, document, v86) when loaded, so each one but
// these is replaced by a module exporting the same names as functions that
// do nothing (a render calls none of them).
const PRERENDER_REAL = new Set(["markup/prerender.jsx", "machinepanel.jsx", "machtab.js", "ui.jsx", "util.js"]);
const stubPageModules = {
	name: "mkb-stub-page-modules",
	setup(build) {
		build.onResolve({ filter: /^\.\.?\// }, (args) => {
			const file = path.join(args.resolveDir, args.path);
			if (args.importer.includes("node_modules") || PRERENDER_REAL.has(path.relative(SITE, file)))
				return null;
			return { path: file, namespace: "mkb-stub" };
		});
		build.onLoad({ filter: /.*/, namespace: "mkb-stub" }, (args) => {
			const src = readFileSync(args.path, "utf8");
			const names = new Set();
			for (const m of src.matchAll(/^export\s*\{([^}]*)\}/gm))
				for (const n of m[1].split(","))
					if (n.trim())
						names.add(n.trim().split(/\s+as\s+/).pop());
			for (const m of src.matchAll(/^export\s+(?:async\s+)?(?:function\*?|const|let|var|class)\s+([\w$]+)/gm))
				names.add(m[1]);
			return { contents: [...names].map((n) => `export const ${n} = () => {};`).join("\n"), loader: "js" };
		});
	},
};

// A module built for node and loaded: its exports. (react-dom/server's
// CommonJS files require node's modules: the banner gives them require.)
async function runInNode(entry, plugins = []) {
	const r = await esbuild.build({ absWorkingDir: SITE, entryPoints: [entry], bundle: true, format: "esm", platform: "node",
		write: false, charset: "utf8", jsx: "automatic", logLevel: "warning", define: { "process.env.NODE_ENV": '"production"' },
		banner: { js: `import { createRequire } from "node:module"; const require = createRequire(${JSON.stringify(SITE + "/")});` },
		plugins });
	return import("data:text/javascript;base64," + Buffer.from(r.outputFiles[0].contents).toString("base64"));
}

async function pageHtml() {
	const { staticHtml } = await runInNode("markup/static.js");
	const { prerenderHtml } = await runInNode("markup/prerender.jsx", [stubPageModules]);
	let html = readFileSync(path.join(SITE, PAGE_SRC), "utf8");
	for (const [id, markup] of Object.entries({ ...staticHtml(), ...prerenderHtml() })) {
		const re = new RegExp(`(<(section|aside|div)\\b[^>]*\\bid="${id}"[^>]*>)(</\\2>)`);
		if (!re.test(html))
			throw new Error(`${PAGE_SRC}: no empty element with id="${id}" for markup/static.js or markup/prerender.jsx`);
		html = html.replace(re, (m, open, tag, close) => open + markup + close);
	}
	return html;
}

async function main() {
	const check = process.argv.includes("--check");
	const r = await esbuild.build({ ...OPTIONS, write: !check, metafile: true });
	if (r.errors.length)
		process.exit(1);
	const inputs = Object.keys(r.metafile.inputs);
	const own = inputs.filter((p) => !p.startsWith("node_modules/"));
	const page = await pageHtml();
	if (!check)
		writeFileSync(path.join(SITE, PAGE), page);
	if (check) {
		const bad = [];
		const files = r.outputFiles.map((f) => ({ path: f.path, contents: f.contents }));
		files.push({ path: path.join(SITE, PAGE), contents: Buffer.from(page) });
		for (const f of files) {
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
			console.error("build.mjs --check: the bundle or index.html is not what these sources build to:\n" + bad.join("\n"));
			process.exit(1);
		}
		console.log(`build.mjs --check: ${OUT}, its map and ${PAGE} are reproducible (${own.length} own files, ` +
			`${inputs.length - own.length} from node_modules)`);
		return;
	}
	const out = r.metafile.outputs[OUT];
	console.log(`${OUT}: ${out.bytes} bytes, sha256 ${sha(readFileSync(path.join(SITE, OUT)))}; ${own.length} own files, ` +
		`${inputs.length - own.length} from node_modules; external: ${[...EXTERNAL].join(", ")}; ${PAGE}: ${page.length} characters`);
}

main().catch((e) => {
	console.error(e.stack || String(e));
	process.exit(1);
});
