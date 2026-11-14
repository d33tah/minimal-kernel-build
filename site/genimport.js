"use strict";
// An imported vmlinux gets the data files CI makes for the kernels shipped
// here (plan E1): symbols.json, disasm.json, dwarf/ (line shards, files.json
// for the coverage tree, types, locals, function facts, CFI) and, once its
// source tree is picked, src/. genworker.js makes them with kgen.js (the
// generators CI runs) and llvm-nm / llvm-objdump of clang/clang.wasm.
//
// The import first works as before (symimport.js: the ELF's .symtab;
// dbginfo.js: each compile unit decoded by dwarfworker.js when first
// needed); when the files are made the kernel switches to them, as if it
// were a shipped kernel: GEN.files holds them under gen/<sha>/, kfetch()
// serves them to dbginfo.js and source.js like the site's own, and
// IndexedDB keeps them for this bzImage (so a reload has them all).
// Only for an ELF with the "auto" offset: the files place code at the
// ELF's PT_LOAD addresses.

let GEN = { files: new Map(), state: null, note: "" };

// A kernel data file: one made here, or the site's (source.js asks for
// src/<path>.gz with each part of the path URL-encoded).
function kfetch(url) {
	const b = GEN.files.get(url) || (url.startsWith("gen/") ? GEN.files.get(decodeURIComponent(url)) : undefined);
	return b ? Promise.resolve(new Response(b)) : fetch(withV(url));
}

function genStatus(text) {
	const el = $("gen_status");
	el.dataset.state = GEN.state || "";
	el.textContent = text === undefined ? GEN.note : text;
}

// key: symimport.js imageKey() of the bzImage (null without storage).
// Each step is a function of its own, called when the one before it ends
// (no await inside: e2e coverage, site/jscov.py, splits a function's run
// at the takes it awaits across).
function genImport(file, key) {
	if (GEN.worker)
		GEN.worker.terminate();
	const sha = key ? key.slice(MAP_KEY.length, MAP_KEY.length + 16) : String(Date.now());
	const mine = GEN = { files: new Map(), state: "running", note: "", name: file.name, key, image, base: "gen/" + sha + "/" };
	const w = GEN.worker = new ClangWorker(withV("genworker.js"));
	w.onprogress = (m) => { if (mine === GEN) genStatus("Making the kernel's data files in this browser: " + m.progress + "…"); };
	genStatus("Making the kernel's data files in this browser…");
	return w.request({ op: "gen", file, name: file.name, clang: new URL("clang/clang.wasm", location.href).href })
		.then((r) => genMade(mine, r), (e) => genFailed(mine, e));
}

function genFailed(mine, e) {
	if (mine !== GEN)
		return;
	GEN.state = "failed";
	GEN.note = "The data files of " + GEN.name + " were not made (" + e.message + "): file:line and variables come from its DWARF as they are needed.";
	genStatus();
	GEN.worker.terminate();
	GEN.worker = null;
}

function genMade(mine, r) {
	if (mine !== GEN)
		return;
	for (const [p, b] of r.files)
		GEN.files.set(GEN.base + p, b);
	GEN.why = r.why;
	GEN.secs = r.ms / 1000; // the Worker's clock: the page's may be clock.js's virtual one
	genApply();
	genKeep();
}

// IndexedDB keeps the files for this bzImage (dbgForget drops them with the rest).
function genKeep() {
	if (!GEN.key)
		return;
	const files = [...GEN.files].map(([k, b]) => [k.slice(GEN.base.length), b]);
	const rec = { name: GEN.name, why: GEN.why, secs: GEN.secs, files };
	dbgIdbOp("readwrite", (s) => s.put(rec, GEN.key + ":gen"));
}

// After a reload: the files made for this bzImage before.
function genRestore(key) {
	return key ? dbgIdbOp("readonly", (s) => s.get(key + ":gen")).then((rec) => genRestored(key, rec)) : Promise.resolve(false);
}

function genRestored(key, rec) {
	if (!rec || !rec.files)
		return false;
	if (GEN.worker)
		GEN.worker.terminate();
	GEN = { files: new Map(), state: "running", note: "", name: rec.name, key, image, base: "gen/" + key.slice(MAP_KEY.length, MAP_KEY.length + 16) + "/",
		why: rec.why, secs: rec.secs, restored: true };
	for (const [p, b] of rec.files)
		GEN.files.set(GEN.base + p, b);
	genApply();
	return true;
}

const genGet = (p) => GEN.files.get(GEN.base + p);
const genJson = (p) => JSON.parse(new TextDecoder().decode(genGet(p)));

// The kernel uses the files made: symbols, disasm, DWARF shards, and the
// C source when a tree is picked (or was, with the files).
function genApply() {
	if (image !== GEN.image || !symbols || !symbols.imported || symbols.imported.source !== "ELF" || symbols.imported.file !== GEN.name) {
		GEN.state = null; // another kernel since
		return;
	}
	const S = genJson("symbols.json");
	S.symbols.sort((a, b) => a.paddr - b.paddr || a.name.localeCompare(b.name));
	symbols = { name: S.name, sections: S.sections, symbols: S.symbols, imported: symbols.imported };
	symbols.imported.generated = true;
	disasm = genGet("disasm.json") ? genJson("disasm.json") : null;
	const made = "made in this browser from " + GEN.name + (GEN.restored ? " (kept for this bzImage)" : " in " + GEN.secs.toFixed(1) + " s");
	if (genGet("dwarf/index.json"))
		dbgUseShards({ dwarf: GEN.base + "dwarf/", made });
	if (genGet("src/index.json"))
		srcUseShipped({ src: GEN.base + "src/" });
	GEN.state = "done";
	const n = (p) => [...GEN.files.keys()].filter((k) => k.startsWith(GEN.base + p)).length;
	GEN.note = "Data files " + made + ": symbols.json (" + symbols.symbols.length.toLocaleString("en") + " symbols)" +
		(disasm ? ", disasm.json (" + disasm.insns.length.toLocaleString("en") + " instructions)" : "") +
		(genGet("dwarf/index.json") ? ", dwarf/ (" + n("dwarf/") + " files)" : "") +
		(genGet("src/index.json") ? ", src/ (" + (n("src/") - 1) + " C files)" : "") + "." +
		(GEN.why && GEN.why.disasm ? " No disasm.json: " + GEN.why.disasm + "." : "") +
		(GEN.why && GEN.why.dwarf ? " " + GEN.why.dwarf + "." : "");
	genStatus();
	$("symbol_index").dataset.page = "";
	syncSrcToggle();
	showImported();
	drawMaps();
	drawGrid();
	dbgChanged();
	if (SRC.mode === "picked" && SRC.entries)
		genSrcPicked();
}

// A source tree picked (source.js) for a kernel whose files were made
// here: src/ from it, as gen-src.js makes it in CI.
function genSrcPicked() {
	if (GEN.state !== "done" || image !== GEN.image || !genGet("dwarf/index.json") || SRC.mode !== "picked" || !SRC.entries)
		return;
	const mine = GEN, pick = SRC;
	const dwarf = [...GEN.files].filter(([k]) => k.startsWith(GEN.base + "dwarf/")).map(([k, b]) => [k.slice(GEN.base.length + 6), b]);
	const w = GEN.worker || (GEN.worker = new ClangWorker(withV("genworker.js")));
	w.onprogress = (m) => { if (mine === GEN) genStatus("Indexing the C source in this browser: " + m.progress + "…"); };
	genStatus("Indexing the C source in this browser…");
	return w.request({ op: "src", dwarf, tree: [...pick.entries], origin: pick.info + ", indexed in this browser" })
		.then((r) => genSrcMade(mine, pick, r), (e) => {
			if (mine === GEN)
				genStatus(GEN.note + " The C source was not indexed (" + e.message + "): its lines are read from the pick as they are shown.");
		});
}

function genSrcMade(mine, pick, r) {
	if (mine !== GEN || pick !== SRC)
		return;
	if (r.pastEnd.length) {
		genStatus(GEN.note + " " + pick.name + " is not the tree the kernel was built from: " + r.pastEnd.length +
			" files are shorter than the lines its DWARF names (" + r.pastEnd.slice(0, 3).join(", ") + "); its lines are read from the pick as they are shown.");
		return;
	}
	for (const k of [...GEN.files.keys()])
		if (k.startsWith(GEN.base + "src/"))
			GEN.files.delete(k);
	for (const [p, b] of r.files)
		GEN.files.set(GEN.base + p, b);
	genApply();
	genKeep();
}
