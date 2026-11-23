"use strict";
// Rebuild and boot (plan E4): the shown kernel built again in this browser
// from its build kit (kit.js, made in CI by gen-kit.py) with your edits
// (srcedit.js's overlay), booted at once, and given the data files a
// shipped kernel has (genimport.js: symbols, disasm, the DWARF shards, and
// the C source of the edited tree), so the listings, the Source panel, the
// Debugger and the coverage follow the edit.
//
// The objects that include an edited file are compiled by clang.wasm
// (kitUsers), the others are CI's; "compile every object" compiles all of
// them. With no edit the bzImage and the ELF are CI's bytes: the status
// compares every product with the kit's SHA-256. The rebuilt kernel is
// image.from's (its edits, its kit, Rebuild again), named "<from>
// (rebuilt here)"; image.edits holds the text of each file it was built
// from. Nothing is kept across a reload: the kernel list loads the shipped
// one.
//
// The box in the Source tab is a React component (R3, RebuildPanel): RB
// holds what it shows and RB.changed() draws it at once; the build log is
// its <pre>, written line by line as the build runs.

import { useCallback, useSyncExternalStore } from "react";
import { flushSync } from "react-dom";
import { withV, httpWhy, $, kib, hex } from "./util.js";
import { image, symbols, setDisasm, kernels, api, provide, runState } from "./page.js";
import { download } from "./graph.js";
import { dbgFresh, dbgOnChange } from "./dbginfo.js";
import { dbgImport } from "./dbgimport.js";
import { srcFresh, SRC, srcChanged, srcFile, SRC_OVER } from "./srctext.js";
import { RB_SUFFIX, buildNew, textKeep, moveRunTo } from "./buildmap.js";
import { covKeep } from "./coverage.js";
import { srcPanel, closeSource } from "./source.js";
import { srcOverlay, edFlush, ED, edState } from "./srcedit.js";
import { parseSymbolFile, applySymbols } from "./symimport.js";
import { ClangWorker } from "./clang.js";
import { kitFetch, kitNow, kitSource, kitBuild } from "./kit.js";
import { genImport, GEN } from "./genimport.js";
import { LOAD_GONE, loadStep, loadDisasm, loadSymbols, shippedDebugFor } from "./kernel.js";
import { Tip, DomNodes } from "./ui.jsx";
import { dbgStatus, runToText, setRunToText } from "./dbgtab.js";
import { MSET, setSettings, setStatus } from "./machtab.js";

const RB = {
	kits: null,      // kits/index.json's kits (name -> sizes), when it loaded
	why: "",         // why it did not
	busy: false,
	last: null,      // the last build booted: { from, bytes (bzImage), elf, elfName, same, text }
	shownFor: null,  // the kernel (image) the status line and the build log are about
	logCmds: 0,      // commands in the build log
	// What RebuildPanel draws (R3): the button, the status line and its
	// state, the progress (a fraction, or null), the compiler's messages, the
	// build log's box and summary, the download buttons.
	can: { ok: false, why: "" },
	title: "",
	status: "",
	state: "",
	progress: null,
	errors: null,    // the failed step's messages, or null
	logShown: false,
	logSum: "Build log",
	files: false,
	all: false,      // "compile every object"
	copied: "",      // "Copied", "Selected: copy it" for a moment
	// The build log's <pre>: written as the build runs (not drawn by React);
	// the module's own node, which the Source tab's box shows once it is
	// mounted (a build started from the Source panel runs before that).
	logEl: rbLogPre(),
	boxEl: null,     // the box (#rb_box) once the Source tab was shown
	rev: 0,
	subs: new Set(),
	subscribe(fn) {
		RB.subs.add(fn);
		return () => RB.subs.delete(fn);
	},
	changed() {
		this.rev++;
		flushSync(() => {
			for (const fn of this.subs)
				fn();
		});
	},
};

const rbFrom = () => (image ? image.from || image.name : "");

function rbLoadIndex() {
	return kitFetch(withV("kits/index.json"))
		.then((r) => r.ok ? r.json() : Promise.reject(new Error("kits/index.json: " + httpWhy(r))))
		.then((ix) => { RB.kits = ix.kits; }, (e) => { RB.why = e.message; })
		.then(rbSync);
}

// { ok } or { ok: false, why }: whether the shown kernel can be rebuilt.
function rbCan() {
	if (RB.busy)
		return { ok: false, why: "A rebuild is running." };
	if (!image)
		return { ok: false, why: "No kernel is loaded." };
	if (!RB.kits)
		return { ok: false, why: RB.why ? "No build kits on this site (" + RB.why + ")." : "Reading the list of build kits…" };
	if (!RB.kits[rbFrom()])
		return { ok: false, why: rbFrom() + " has no build kit: the page rebuilds " + Object.keys(RB.kits).sort().join(", ") + "." };
	return { ok: true };
}

function rbSync() {
	const can = rbCan();
	const n = can.ok ? srcOverlay.paths().length : 0;
	RB.can = can;
	RB.title = can.ok ? "Build " + rbFrom() + " again in this browser" + (n ? " with your " + n + " changed file" + (n === 1 ? "" : "s") : " (no edits: CI's bytes)") +
		" and boot it" : can.why;
	// A result belongs to the kernel it was shown for.
	if (!RB.busy && RB.shownFor !== image) {
		RB.state = "";
		RB.errors = null;
		RB.files = RB.logShown = false;
		RB.progress = null;
	}
	if (!RB.busy && !RB.state)
		RB.status = can.ok ? "Rebuild and boot builds " + rbFrom() + " in this browser" +
			(n ? " with your " + n + " changed file" + (n === 1 ? "" : "s") : ", with no edit: CI's bytes") + ", then boots it." : can.why;
	RB.changed();
}

// The status line, the progress bar and the panel button's label (RB).
function rbShow(text, state, frac) {
	RB.status = text;
	RB.state = state || "";
	RB.shownFor = image;
	RB.progress = frac === undefined ? null : frac;
	RB.changed();
}

// The progress of kitBuild's onstep events as [text, fraction]: loading
// clang.wasm 0-0.1, fetching 0.1-0.2, compiling 0.2-0.7, the steps 0.7-0.95.
function rbStepText(e) {
	if (e.kind === "fetch")
		return ["Fetching the kit: " + e.done + " of " + e.of + " files…", 0.1 + 0.1 * e.done / e.of];
	if (e.kind === "gen")
		return ["Making " + e.out + " (" + (e.tool === "cpio" ? "gen_init_cpio" : e.tool) + ", " + (e.done + 1) + " of " + e.of + ")…", 0.2];
	if (e.kind === "compile")
		return ["Compiling " + e.out + " (" + (e.done + 1) + " of " + e.of + ")…", 0.2 + 0.5 * e.done / e.of];
	if (e.kind === "object")
		return ["Compiled " + e.out + " (" + e.done + " of " + e.of + ").", 0.2 + 0.5 * e.done / e.of];
	return ["Linking: step " + e.done + " of " + e.of + " (" + e.cmd + ") done.", 0.7 + 0.25 * e.done / e.of];
}

// --- the build log (E4a) ---
// Every command the build runs, written as a shell would take it, then its
// exit code, its time and its output; "# " lines are the page's notes. It
// fills while the build runs, follows its end while it is scrolled to the
// end (or closed), and stays until the next build. Written into its <pre>
// as it comes (thousands of lines: no React drawing per line).

// The log as DomNodes shows it (the same array at every render).
const RB_LOG = [RB.logEl];

function rbLogPre() {
	const pre = document.createElement("pre");
	pre.id = "rb_buildlog";
	pre.className = "rbbuildlog";
	pre.tabIndex = 0;
	pre.setAttribute("aria-label", "Build log");
	return pre;
}

// One argument as a POSIX shell word.
function rbWord(a) {
	return /^[\w@%+=:,./-]+$/.test(a) ? a : "'" + a.replace(/'/g, "'\\''") + "'";
}

function rbLogStart() {
	RB.logCmds = 0;
	RB.logEl.textContent = "";
	RB.logShown = true;
	rbLogSum(true);
}

function rbLogSum(running) {
	RB.logSum = "Build log (" + RB.logCmds + " command" + (RB.logCmds === 1 ? "" : "s") + (running ? ", running" : "") + ")";
	RB.changed();
}

// Text added at the end, in a span of class cls (none: plain).
function rbLogAdd(text, cls) {
	const log = RB.logEl;
	const atEnd = log.scrollHeight - log.clientHeight - log.scrollTop <= 4;
	const n = cls ? document.createElement("span") : document.createTextNode(text);
	if (cls) {
		n.className = cls;
		n.textContent = text;
	}
	log.appendChild(n);
	if (atEnd)
		log.scrollTop = log.scrollHeight;
}

function rbLogNote(text) {
	rbLogAdd("# " + text + "\n", "rbnote");
}

// A kitBuild event of the log: "command" or "output".
function rbLogEvent(e) {
	if (e.kind === "command") {
		RB.logCmds++;
		rbLogSum(true);
		rbLogAdd("$ " + e.argv.map(rbWord).join(" ") + (e.stdin ? " < (the previous step's output)" : e.list ? " <<EOF" : "") + "\n", "rbcmd");
		if (e.list)
			rbLogAdd(e.list.join("\n") + "\nEOF\n");
		return;
	}
	if (e.stdout)
		rbLogAdd(e.stdout.endsWith("\n") ? e.stdout : e.stdout + "\n");
	if (e.stderr)
		rbLogAdd(e.stderr.endsWith("\n") ? e.stderr : e.stderr + "\n", e.code ? "rbbad" : "");
	rbLogAdd("  exit " + e.code + ", " + (e.ms / 1000).toFixed(2) + " s" +
		(e.to ? ", stdout to " + e.to + " (" + e.bytes + " bytes)" : e.to === "" ? ", stdout to the next step (" + e.bytes + " bytes)" : "") + "\n",
	e.code ? "rbbad" : "rbnote");
}

function rbLogCopy() {
	const text = RB.logEl.textContent;
	const said = (t) => {
		RB.copied = t;
		RB.changed();
		setTimeout(() => {
			RB.copied = "";
			RB.changed();
		}, 1500);
	};
	(navigator.clipboard ? navigator.clipboard.writeText(text) : Promise.reject(new Error("no clipboard"))).then(() => said("Copied"), () => {
		// No clipboard access: the text selected, for the user to copy.
		getSelection().selectAllChildren(RB.logEl);
		said("Selected: copy it");
	});
}

// The edits as kitBuild's: { path: text }; a deleted file or one the kit
// does not have refuses the build.
async function rbEdits(kit) {
	const have = new Set(kit.files.map((f) => f[0]));
	const edits = {}, bad = [];
	for (const p of srcOverlay.paths()) {
		const t = await srcOverlay.read(p);
		if (t === null)
			bad.push(p + " (deleted)");
		else if (!have.has(p))
			bad.push(p + " (not a file of the build kit)");
		else
			edits[p] = t;
	}
	if (bad.length)
		throw new Error("the build cannot use your edits of " + bad.join(", ") + ": revert them in the list above");
	return edits;
}

// The tree the kernel was built from, as a picked source tree (source.js):
// the kit's files in the tree with the edits in place; genimport.js makes
// src/ from it.
async function rbTree(kit, src, edits) {
	const files = kit.files.filter((f) => f[3] !== 2 && !f[0].startsWith("/"));
	const out = new Map();
	for (let i = 0; i < files.length; i += 32) {
		const part = files.slice(i, i + 32);
		const data = await Promise.all(part.map((f) => edits[f[0]] !== undefined ? new TextEncoder().encode(edits[f[0]]) : src.get(f[1])));
		part.forEach((f, k) => out.set(f[0], new Blob([data[k]])));
	}
	return out;
}

// --- places across builds (E8) ---
// The files whose text may differ between the kernel shown and the new
// build: the edits of both, and the file of a Run to file:line target.
function rbPaths(kit, edits) {
	const have = kit.files.filter((f) => f[3] !== 2).map((f) => f[0]);
	const paths = new Set([...Object.keys(edits), ...Object.keys(image && image.build ? image.build.files : {}), ...SRC_OVER.keys()]);
	const m = /^(.*\D.*):(\d+)$/.exec(runToText());
	if (m) {
		const q = m[1].replace(/^\.?\//, "");
		const p = have.find((x) => x === q) || have.find((x) => x.endsWith("/" + q));
		if (p)
			paths.add(p);
	}
	return [...paths].filter((p) => have.includes(p));
}

// The kit's own text (the shipped tree) of each path: { path: text }.
async function rbShipped(kit, src, paths) {
	const out = {};
	for (const p of paths) {
		const f = kit.files.find((x) => x[0] === p && x[3] !== 2);
		out[p] = new TextDecoder().decode(await src.get(f[1]));
	}
	return out;
}

// The text of path the panel shows for the kernel shown (its build, or a
// hot patch's), once loaded; null without one.
async function rbShown(path) {
	const t0 = Date.now();
	let f = srcFile(path);
	while (f.state === "loading" && Date.now() - t0 < 20000) {
		await new Promise((ok) => setTimeout(ok, 50));
		f = srcFile(path);
	}
	return f.state === "ok" ? f.lines.join("\n") : null;
}

// A Run to address (hex) of the kernel shown, kept as the function holding
// it and the offset: a function for the new kernel's symbols that gives
// the new address and what to say, or null.
function rbRunToAddr() {
	const v = runToText();
	if (!/^(0x)?[0-9a-f]+$/i.test(v) || !symbols || symbols.symbols.some((s) => s.name === v))
		return null;
	const n = parseInt(v.replace(/^0x/i, ""), 16);
	const koff = symbols.symbols.length ? symbols.symbols[0].vaddr - symbols.symbols[0].paddr : 0;
	const pa = koff > 0 && n >= koff ? n - koff : n;
	const s = api.symbols.symbolAt(symbols.symbols.filter((x) => !x.patch), pa);
	if (!s || pa - s.paddr >= Math.max(1, s.size))
		return null;
	const off = pa - s.paddr;
	return () => {
		const t = symbols && symbols.symbols.find((x) => x.name === s.name && !x.patch);
		if (!t || off >= Math.max(1, t.size))
			return "Run to " + v + " (" + s.name + "+" + hex(off) + ") has no place in this build: " + (t ? s.name + " is shorter now" : "no " + s.name + " here") + ".";
		if (t.paddr === s.paddr)
			return "";
		setRunToText("0x" + (n + t.paddr - s.paddr).toString(16));
		return "Run to " + v + " is " + runToText() + " now: " + s.name + "+" + hex(off) + " moved with the build" +
			(t.size !== s.size ? " (" + s.name + " changed size: the offset may be another instruction)" : "") + ".";
	};
}

// The buttons are disabled while rbCan() says no (a build running included).
async function rbRun() {
	edFlush(); // what was typed last
	const from = rbFrom(), shown = image, t0 = kitNow();
	RB.busy = true;
	RB.errors = null;
	RB.files = false;
	rbSync();
	rbShow("Reading the build kit of " + from + "…", "running", 0);
	const all = RB.all;
	rbLogStart();
	rbLogNote("Rebuild of " + from + " in this browser, " + (all ? "every object compiled" : "the objects that include an edited file compiled") + ".");
	// The machine is paused while the page builds: a VM at full speed in
	// the same tab made Debian's link 10x slower on 2 cores (CI run
	// 44094339542). It runs again if the build fails, and the built kernel
	// boots anyway.
	const wasRunning = runState === "jit" || runState === "stepped";
	if (wasRunning) {
		await api.step.pause("Paused for the rebuild.");
		rbLogNote("The machine is paused while the page builds.");
	}
	const resumeIfFailed = () => {
		if (wasRunning && image === shown && runState === "paused")
			api.step.resume();
	};
	const cc = new ClangWorker();
	let r, kit, edits;
	try {
		const src = kitSource("kits/");
		kit = await src.kit(from);
		edits = await rbEdits(kit);
		const names = Object.keys(edits);
		rbLogNote("Build kit kits/" + from + ".json: " + kit.objects.length + " objects, " + kit.steps.length + " link steps; edited: " +
			(names.length ? names.join(", ") : "nothing") + ".");
		rbShow("Loading the compiler (clang.wasm, 23 MB to download the first time)…", "running", 0.02);
		const tl = kitNow();
		await cc.load();
		rbLogNote("clang.wasm loaded in " + ((kitNow() - tl) / 1000).toFixed(2) + " s.");
		const tf = kitNow();
		r = await kitBuild(cc, kit, src, {
			compile: all ? "all" : "changed",
			edits,
			verify: false,
			keep: kit.products.map((p) => p[0]),
			onstep: (e) => {
				if (e.kind === "command" || e.kind === "output")
					return rbLogEvent(e);
				if (e.kind === "fetch" && e.done === e.of)
					rbLogNote("Fetched " + e.of + " files of the kit in " + ((kitNow() - tf) / 1000).toFixed(2) + " s.");
				const [text, frac] = rbStepText(e);
				rbShow(text, "running", frac);
			},
		});
		r.tree = await rbTree(kit, src, edits);
		r.shipped = await rbShipped(kit, src, rbPaths(kit, edits));
		rbLogNote("Done in " + ((kitNow() - t0) / 1000).toFixed(2) + " s: fetching " + (r.fetchMs / 1000).toFixed(2) + " s, compiling " +
			(r.compileMs / 1000).toFixed(2) + " s (" + r.objects.length + " object" + (r.objects.length === 1 ? "" : "s") + "), linking " + (r.linkMs / 1000).toFixed(2) + " s (" +
			r.steps.length + " steps). Products: " + r.products.map((p) => p[0] + " " + p[2] + " bytes, SHA-256 " + p[1]).join("; ") + ".");
	} catch (e) {
		RB.busy = false;
		cc.terminate();
		rbLogNote("Failed: " + e.message.replace(/\n[\s\S]*/, ""));
		rbLogSum(false);
		rbFailed(from, e);
		resumeIfFailed();
		return;
	}
	rbLogSum(false);
	cc.terminate();
	RB.busy = false;
	if (image !== shown) {
		rbShow("Built, not booted: another kernel was chosen meanwhile.", "failed");
		rbSync();
		return;
	}
	await rbBoot(from, kit, edits, r, kitNow() - t0).catch((e) => {
		rbShow(e === LOAD_GONE ? "Built, not booted: another kernel was chosen meanwhile." : "Built, but not booted: " + e.message + ".", "failed");
		rbSync();
	});
}

function rbFailed(from, e) {
	const m = /^(.*?) exited with (\d+): ([\s\S]*)$/.exec(e.message);
	rbShow(m ? "Not built: " + m[1] + " failed (its messages below). The machine still runs " + image.name + "." :
		"Not built: " + e.message + ". The machine still runs " + image.name + ".", "failed");
	RB.errors = m ? m[3] : null;
	rbSync();
	// Where it says why: the Source tab (also when the editor's button was pressed).
	api.show.showTab("source");
	if (RB.boxEl)
		RB.boxEl.scrollIntoView({ block: "start" });
	if (ED.cur)
		edState("not built, the Source tab says why");
}

// The products as the kernel of the page: boot the bzImage, import the ELF
// (symbols at once, the files made in a Worker), the edited tree as its
// source.
async function rbBoot(from, kit, edits, r, ms) {
	// The newest kernel asked for (A3c): a load still running ends at its
	// next step, and a kernel picked from here on ends this boot.
	const step = loadStep();
	const files = new Map(r.files);
	const imgPath = kit.shipped.path;
	const elfPath = kit.products.map((p) => p[0]).find((p) => p !== imgPath);
	const bytes = files.get(imgPath), elf = files.get(elfPath);
	const same = r.products.every((p) => p[1] === p[3]);
	const nEdits = Object.keys(edits).length;
	const elfName = elfPath.replace(/^.*\//, "");
	RB.last = { from, bytes, elf, elfName, same };
	const mem = MSET.ram;
	// A kit whose objects have no debug sections (Debian): with no edit the
	// ELF has no DWARF and is CI's bytes, so the shipped kernel's data files
	// are this kernel's and are used as they are; with edits the ELF has the
	// DWARF of the compiled units only, and the shipped kernel's DWARF covers
	// the others, moved to their new addresses (genimport.js, dbgmerge.js).
	const shipped = kit.objects.some((o) => o.gsha256) ? kernels.find((x) => x.name === from) : null;
	const entry = same ? shipped : null;
	// What the kernel shown leaves to the next one (E8): what ran in it (the
	// coverage tree compares), the texts its places were counted on (the Run
	// to target moves with the edit) and its Run to address.
	covKeep();
	const moveAddr = rbRunToAddr();
	const texts = new Map();
	for (const [p, t] of Object.entries(r.shipped)) {
		const was = await step(rbShown(p));
		if (was !== null)
			texts.set(p, [was, edits[p] !== undefined ? edits[p] : t]);
	}
	const [disasm, syms] = entry ? await step(Promise.all([loadDisasm(entry), loadSymbols(entry)])) : [null, undefined];
	setSettings({ kernel: from }); // the menu may show the load this boot overtook
	setDisasm(disasm);
	dbgFresh(null, "reading the rebuilt kernel's DWARF…");
	api.machine.useImage(from + RB_SUFFIX, bytes.slice().buffer, mem, syms);
	image.from = from;
	image.edits = edits;
	image.build = buildNew(from, edits);
	image.build.base = Object.fromEntries(Object.entries(r.shipped).map(([p, t]) => [p, textKeep(t)]));
	for (const [a, b] of texts.values())
		textKeep(a), textKeep(b);
	RB.shownFor = image; // the build log and the status stay with it
	let gen = null;
	if (entry) {
		await shippedDebugFor(entry);
	} else {
		const elfFile = new File([elf], elfName);
		const map = await parseSymbolFile(elfFile, "auto");
		applySymbols(map);
		srcFresh("picked", "");
		SRC.name = "the edited tree of " + from;
		SRC.entries = r.tree;
		// What the kit assembles (the minified Linux's elo/init.nasm, plan
		// E9): listed though no DWARF names it.
		SRC.extra = (kit.gen || []).filter((g) => g.tool === "nasm").flatMap((g) => g.inputs);
		SRC.info = from + "'s tree with your " + nEdits + " edit" + (nEdits === 1 ? "" : "s") + " (" + r.tree.size + " files)";
		srcChanged();
		dbgImport(elfFile, map.offset, null);
		gen = genImport(elfFile, null, shipped && shipped.dwarf && shipped.symbols ? shipped : null);
	}
	// The Run to target (the Debugger's breakpoint) in this build's terms.
	const moved = [moveRunTo(texts), moveAddr ? moveAddr() : ""].filter(Boolean).join(" ");
	if (moved)
		dbgStatus(moved);
	rbSync();
	const sha = (h) => h.slice(0, 12);
	const s = (x) => (x / 1000).toFixed(1) + " s";
	const how = r.objects.length + " object" + (r.objects.length === 1 ? "" : "s") + " compiled, " + r.steps.length + " link steps, " +
		s(ms) + " (fetching " + s(r.fetchMs) + ", compiling " + s(r.compileMs) + ", linking " + s(r.linkMs) + ")";
	const which = same ? "identical to CI's build (" + kit.products.map((p) => p[0].replace(/^.*\//, "") + " " + sha(p[1])).join(", ") + ")" +
		(kit.shipped.differs.length ? "; the page's shipped bzImage is the build without debug info, the same but for its " +
			kit.shipped.differs.reduce((n, d) => n + d[1], 0) + "-byte build-id" : "") :
		"built from your " + nEdits + " changed file" + (nEdits === 1 ? "" : "s") + ": bzImage " + sha(r.products.find((p) => p[0] === imgPath)[1]) +
		", CI's is " + sha(kit.shipped.sha256);
	RB.last.text = "Booted " + image.name + ": " + kib(bytes.length) + " bzImage " + which + "; " + how + "." + (moved ? " " + moved : "");
	rbShow(RB.last.text + (entry ? " Its data files are the shipped kernel's (the same bytes)." :
		" Making its data files (symbols, disassembly, DWARF, source)…"), entry ? "done" : "booted", entry ? undefined : 0.97);
	RB.files = true;
	RB.changed();
	// The screen: in the Machine tab, under the Source panel's sheet on a phone.
	api.show.showTab("machine");
	if (srcPanel && matchMedia("(max-width: 640px)").matches)
		closeSource();
	$("screen_container").scrollIntoView({ block: "nearest" });
	await api.machine.boot();
	if (!gen)
		return;
	await gen;
	await GEN.srcDone;
	if (image.from !== from || RB.busy)
		return;
	rbShow(RB.last.text + (GEN.state === "done" ? " Its data files are made: the listings, the Source panel and the Debugger show it." :
		" " + GEN.note), "done");
}

// The buttons show once a build is booted (RB.last).
function rbDownload(what) {
	const L = RB.last;
	const name = L.from + "-rebuilt-" + (what === "elf" ? L.elfName : "bzImage");
	download(name, what === "elf" ? L.elf : L.bytes, "application/octet-stream");
}

function initRebuild() {
	dbgOnChange(rbSync);
	// hotpatch.jsx waits for the same list (one fetch of kits/index.json).
	RB.indexLoad = rbLoadIndex();
}

// Back to the shipped kernel: what ran in this build is kept to compare (E8).
// After the click is handled: covKeep may count a big kernel's lines for a while.
const rbBack = () => setTimeout(() => {
	covKeep();
	api.machine.loadKernel(kernels.find((x) => x.name === rbFrom())).catch((e) => setStatus(String(e)));
}, 0);

// The Rebuild and boot box of the Source tab (#rb_box, under Your edits).
function RebuildPanel() {
	useSyncExternalStore(RB.subscribe, () => RB.rev);
	const keepBox = useCallback((el) => { RB.boxEl = el; }, []);
	return (
		<div id="rb_box" className="rbbox" ref={keepBox}>
			<div className="scan"><button id="rb_go" type="button" className="rbgo" disabled={!RB.can.ok} title={RB.title}
				onClick={rbRun}><span aria-hidden="true">⟳</span> Rebuild and boot</button>{" "}<label className="rball"><input id="rb_all"
				type="checkbox" checked={RB.all} onChange={(e) => { RB.all = e.target.checked; RB.changed(); }} /> compile every object</label>
			<Tip text="Builds this kernel again in this browser, with your edits, and boots it: the build kit CI made for it (its objects, sources, headers and the link steps) is fetched, what the kit makes from your files first is made again (the minified Linux's /init from elo/init.nasm by nasm, then its initramfs), the objects that include a file you changed are compiled by clang.wasm (the page's clang 22.1.0, fetched once, 77 MB), the others are CI's, and the link steps (ld.lld, objcopy, the kernel's own tools as wasm) make the bzImage and the ELF with debug info. The kernel boots at once; its symbols, disassembly, DWARF (lines, types, locals, call frames) and the C source of your edited tree are then made in this browser, so the listings, the Source panel, the Debugger and the coverage follow the edit. With no edit the bzImage and the ELF are CI's bytes, which the status checks. Compile every object: compile all of them from the sources, not only the changed ones. Nothing is uploaded." />
			{" "}<button id="rb_back" type="button" hidden={!(image && image.from) || RB.busy} onClick={rbBack}>Back to the shipped kernel</button></div>
			<progress id="rb_progress" max="1" value={RB.progress || 0} hidden={RB.progress === null}></progress>
			<p id="rb_status" className="note" role="status" data-state={RB.state}>{RB.status}</p>
			<pre id="rb_log" className="rblog" hidden={RB.errors === null}>{RB.errors}</pre>
			<details id="rb_buildlog_box" className="rblogbox" hidden={!RB.logShown}><summary id="rb_buildlog_sum">{RB.logSum}</summary>
				<div className="scan"><button id="rb_buildlog_copy" type="button" onClick={rbLogCopy}>{RB.copied || "Copy the log"}</button>
					<Tip text="Every command of the build as it runs: the compiles (clang.wasm) and the link steps (ld.lld, objcopy, the kernel's own tools as wasm, and sed and cat done by the page), each with its exit code, its time and what it printed. Lines starting with # are the page's own notes (fetching the kit, the phases' times). It stays after the build, also after an error, until the next build." /></div>
				<DomNodes nodes={RB_LOG} /></details>
			<div id="rb_files" className="scan" hidden={!RB.files}><button id="rb_dl_image" type="button" onClick={() => rbDownload("image")}>Download bzImage</button>{" "}
				<button id="rb_dl_elf" type="button" onClick={() => rbDownload("elf")}>{"Download " + (RB.last ? RB.last.elfName : "ELF")}</button></div>
		</div>
	);
}

// The Source panel's Rebuild and boot button (in the editor's bar): the
// same build as the box's button, its label the progress while it runs;
// while it is disabled, why is said under it as text.
function RebuildPanelButton() {
	useSyncExternalStore(RB.subscribe, () => RB.rev);
	const running = RB.state === "running";
	return (
		<>
			<button id="rb_go_panel" type="button" className="rbgo" disabled={!RB.can.ok} title={RB.title || undefined}
				aria-busy={RB.shownFor ? String(running) : undefined} onClick={rbRun}>
				<span aria-hidden="true">⟳</span>{" "}<span className="rblabel">{running ? "Rebuilding… " + Math.round((RB.progress || 0) * 100) + "%" :
					"Rebuild and boot"}</span>
			</button>
			{!RB.can.ok && !running && RB.can.why ? <span id="rb_go_why" className="note rbwhy">{"Rebuild and boot: " + RB.can.why}</span> : null}
		</>
	);
}

provide({ rbSync });
export { RB, initRebuild, RebuildPanel, RebuildPanelButton };
