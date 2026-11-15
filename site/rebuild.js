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

const RB = {
	kits: null,      // kits/index.json's kits (name -> sizes), when it loaded
	why: "",         // why it did not
	busy: false,
	last: null,      // the last build booted: { from, bytes (bzImage), elf, elfName, same, text }
	shownFor: null,  // the kernel (image) the status line is about
};

const RB_SUFFIX = " (rebuilt here)";

const rbFrom = () => (image ? image.from || image.name : "");

function rbLoadIndex() {
	return fetch(withV("kits/index.json"))
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
		$("rb_log").hidden = $("rb_files").hidden = $("rb_progress").hidden = true;
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
	const cc = new ClangWorker();
	let r, kit, edits;
	try {
		const src = kitSource("kits/");
		kit = await src.kit(from);
		edits = await rbEdits(kit);
		rbShow("Loading the compiler (clang.wasm, 23 MB to download the first time)…", "running", 0.02);
		await cc.load();
		r = await kitBuild(cc, kit, src, {
			compile: $("rb_all").checked ? "all" : "changed",
			edits,
			verify: false,
			keep: kit.products.map((p) => p[0]),
			onstep: (e) => {
				const [text, frac] = rbStepText(e);
				rbShow(text, "running", frac);
			},
		});
		r.tree = await rbTree(kit, src, edits);
	} catch (e) {
		RB.busy = false;
		cc.terminate();
		rbFailed(from, e);
		return;
	}
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
	disasm = null;
	dbgFresh(null, "reading the rebuilt kernel's DWARF…");
	useImage(from + RB_SUFFIX, bytes.slice().buffer, mem);
	image.from = from;
	image.edits = edits;
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
	dbgOnChange(rbSync);
	rbLoadIndex();
}
