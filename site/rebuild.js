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

import { withV, httpWhy, $, kib } from "./util.js";
import { provideRebuild, image, showTab, setDisasm, useImage, boot, loadKernel, kernels, setStatus } from "./page.js";
import { download } from "./graph.js";
import { dbgFresh, dbgImport, dbgOnChange } from "./dbginfo.js";
import { srcFresh, SRC, srcChanged, srcPanel, closeSource } from "./source.js";
import { srcOverlay, edFlush, ED, edState } from "./srcedit.js";
import { parseSymbolFile, applySymbols } from "./symimport.js";
import { ClangWorker } from "./clang.js";
import { kitFetch, kitNow, kitSource, kitBuild } from "./kit.js";
import { genImport, GEN } from "./genimport.js";

const RB = {
	kits: null,      // kits/index.json's kits (name -> sizes), when it loaded
	why: "",         // why it did not
	busy: false,
	last: null,      // the last build booted: { from, bytes (bzImage), elf, elfName, same, text }
	shownFor: null,  // the kernel (image) the status line and the build log are about
	logCmds: 0,      // commands in the build log
};

const RB_SUFFIX = " (rebuilt here)";

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
	for (const id of ["rb_go", "rb_go_panel"]) {
		$(id).disabled = !can.ok;
		$(id).title = can.ok ? "Build " + rbFrom() + " again in this browser" + (n ? " with your " + n + " changed file" + (n === 1 ? "" : "s") : " (no edits: CI's bytes)") +
			" and boot it" : can.why;
	}
	$("rb_back").hidden = !(image && image.from) || RB.busy;
	const st = $("rb_status");
	// A result belongs to the kernel it was shown for.
	if (!RB.busy && RB.shownFor !== image) {
		st.dataset.state = "";
		$("rb_log").hidden = $("rb_files").hidden = $("rb_progress").hidden = $("rb_buildlog_box").hidden = true;
	}
	if (!RB.busy && !st.dataset.state)
		st.textContent = can.ok ? "Rebuild and boot builds " + rbFrom() + " in this browser" +
			(n ? " with your " + n + " changed file" + (n === 1 ? "" : "s") : ", with no edit: CI's bytes") + ", then boots it." : can.why;
}

// The status line, the progress bar and the panel button's label.
function rbShow(text, state, frac) {
	const st = $("rb_status");
	st.textContent = text;
	st.dataset.state = state || "";
	RB.shownFor = image;
	const bar = $("rb_progress");
	bar.hidden = frac === undefined;
	if (frac !== undefined)
		bar.value = frac;
	$("rb_go_panel").querySelector(".rblabel").textContent = state === "running" ?
		"Rebuilding… " + Math.round((frac || 0) * 100) + "%" : "Rebuild and boot";
	$("rb_go_panel").setAttribute("aria-busy", state === "running");
}

// The progress of kitBuild's onstep events as [text, fraction]: loading
// clang.wasm 0-0.1, fetching 0.1-0.2, compiling 0.2-0.7, the steps 0.7-0.95.
function rbStepText(e) {
	if (e.kind === "fetch")
		return ["Fetching the kit: " + e.done + " of " + e.of + " files…", 0.1 + 0.1 * e.done / e.of];
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
// end (or closed), and stays until the next build.

// One argument as a POSIX shell word.
function rbWord(a) {
	return /^[\w@%+=:,./-]+$/.test(a) ? a : "'" + a.replace(/'/g, "'\\''") + "'";
}

function rbLogStart() {
	RB.logCmds = 0;
	$("rb_buildlog").textContent = "";
	$("rb_buildlog_box").hidden = false;
	rbLogSum(true);
}

function rbLogSum(running) {
	$("rb_buildlog_sum").textContent = "Build log (" + RB.logCmds + " command" + (RB.logCmds === 1 ? "" : "s") + (running ? ", running" : "") + ")";
}

// Text added at the end, in a span of class cls (none: plain).
function rbLogAdd(text, cls) {
	const log = $("rb_buildlog");
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
		rbLogAdd("$ " + e.argv.map(rbWord).join(" ") + (e.stdin ? " < (the previous step's output)" : "") + "\n", "rbcmd");
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
	const text = $("rb_buildlog").textContent, b = $("rb_buildlog_copy");
	const said = (t) => {
		b.textContent = t;
		setTimeout(() => { b.textContent = "Copy the log"; }, 1500);
	};
	(navigator.clipboard ? navigator.clipboard.writeText(text) : Promise.reject(new Error("no clipboard"))).then(() => said("Copied"), () => {
		// No clipboard access: the text selected, for the user to copy.
		getSelection().selectAllChildren($("rb_buildlog"));
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

// The buttons are disabled while rbCan() says no (a build running included).
async function rbRun() {
	edFlush(); // what was typed last
	const from = rbFrom(), shown = image, t0 = kitNow();
	RB.busy = true;
	$("rb_log").hidden = true;
	$("rb_files").hidden = true;
	rbSync();
	rbShow("Reading the build kit of " + from + "…", "running", 0);
	const all = $("rb_all").checked;
	rbLogStart();
	rbLogNote("Rebuild of " + from + " in this browser, " + (all ? "every object compiled" : "the objects that include an edited file compiled") + ".");
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
		rbLogNote("Done in " + ((kitNow() - t0) / 1000).toFixed(2) + " s: fetching " + (r.fetchMs / 1000).toFixed(2) + " s, compiling " +
			(r.compileMs / 1000).toFixed(2) + " s (" + r.objects.length + " object" + (r.objects.length === 1 ? "" : "s") + "), linking " + (r.linkMs / 1000).toFixed(2) + " s (" +
			r.steps.length + " steps). Products: " + r.products.map((p) => p[0] + " " + p[2] + " bytes, SHA-256 " + p[1]).join("; ") + ".");
	} catch (e) {
		RB.busy = false;
		cc.terminate();
		rbLogNote("Failed: " + e.message.replace(/\n[\s\S]*/, ""));
		rbLogSum(false);
		rbFailed(from, e);
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
		rbShow("Built, but not booted: " + e.message + ".", "failed");
		rbSync();
	});
}

function rbFailed(from, e) {
	const m = /^(.*?) exited with (\d+): ([\s\S]*)$/.exec(e.message);
	rbShow(m ? "Not built: " + m[1] + " failed (its messages below). The machine still runs " + image.name + "." :
		"Not built: " + e.message + ". The machine still runs " + image.name + ".", "failed");
	$("rb_log").textContent = m ? m[3] : "";
	$("rb_log").hidden = !m;
	rbSync();
	// Where it says why: the Source tab (also when the editor's button was pressed).
	showTab("source");
	$("rb_box").scrollIntoView({ block: "start" });
	if (ED.cur)
		edState("not built, the Source tab says why");
}

// The products as the kernel of the page: boot the bzImage, import the ELF
// (symbols at once, the files made in a Worker), the edited tree as its
// source.
async function rbBoot(from, kit, edits, r, ms) {
	const files = new Map(r.files);
	const imgPath = kit.shipped.path;
	const elfPath = kit.products.map((p) => p[0]).find((p) => p !== imgPath);
	const bytes = files.get(imgPath), elf = files.get(elfPath);
	const same = r.products.every((p) => p[1] === p[3]);
	const nEdits = Object.keys(edits).length;
	const elfName = elfPath.replace(/^.*\//, "");
	RB.last = { from, bytes, elf, elfName, same };
	const mem = $("memory").value;
	setDisasm(null);
	dbgFresh(null, "reading the rebuilt kernel's DWARF…");
	useImage(from + RB_SUFFIX, bytes.slice().buffer, mem);
	image.from = from;
	image.edits = edits;
	RB.shownFor = image; // the build log and the status stay with it
	const elfFile = new File([elf], elfName);
	const map = await parseSymbolFile(elfFile, "auto");
	applySymbols(map);
	srcFresh("picked", "");
	SRC.name = "the edited tree of " + from;
	SRC.entries = r.tree;
	SRC.info = from + "'s tree with your " + nEdits + " edit" + (nEdits === 1 ? "" : "s") + " (" + r.tree.size + " files)";
	srcChanged();
	dbgImport(elfFile, map.offset, null);
	const gen = genImport(elfFile, null);
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
	RB.last.text = "Booted " + image.name + ": " + kib(bytes.length) + " bzImage " + which + "; " + how + ".";
	rbShow(RB.last.text + " Making its data files (symbols, disassembly, DWARF, source)…", "booted", 0.97);
	$("rb_files").hidden = false;
	$("rb_dl_elf").textContent = "Download " + elfName;
	// The screen: in the Machine tab, under the Source panel's sheet on a phone.
	showTab("machine");
	if (srcPanel && matchMedia("(max-width: 640px)").matches)
		closeSource();
	$("screen_container").scrollIntoView({ block: "nearest" });
	await boot();
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
	$("rb_go").addEventListener("click", rbRun);
	$("rb_go_panel").addEventListener("click", rbRun);
	$("rb_back").addEventListener("click", () => loadKernel(kernels.find((x) => x.name === rbFrom())).catch((e) => setStatus(String(e))));
	$("rb_dl_image").addEventListener("click", () => rbDownload("image"));
	$("rb_dl_elf").addEventListener("click", () => rbDownload("elf"));
	$("rb_buildlog_copy").addEventListener("click", rbLogCopy);
	dbgOnChange(rbSync);
	rbLoadIndex();
}

provideRebuild({ rbSync });
export { rbSync, initRebuild };
