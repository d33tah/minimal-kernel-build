"use strict";
// Patch running kernel (plan E7): an edit of one C file put into the
// paused machine without a reboot. The edited file's unit is compiled by
// clang.wasm from the kernel's build kit twice, with the text the machine
// runs and with the edit (hpelf.js hpPlan compares the two), the changed
// functions are copied into memory reserved for patches and each old
// entry gets a jump to its copy. While the machine is paused and one C
// file differs from what it runs, the page makes the plan in the
// background, so the button is enabled only for an edit a patch can
// apply, and says why not otherwise; pressing it writes the plan.
//
// Reserved memory: physical 0x60000-0x7FFFF (128 KiB), in conventional
// memory below the EBDA. Linux reserves the whole first megabyte from
// its allocator (memblock_reserve(0, SZ_1M) in setup_arch; the minified
// tree keeps that) and maps it at PAGE_OFFSET + address with its other
// low memory; v86 loads the real-mode setup and the command line at
// 0x90000 and above, the BIOS keeps its data below 0x1000 and from the
// EBDA up, Linux's check for BIOS memory corruption scans only the first
// 64 KiB, and hello uses only its own image and stack at 1 MiB. So no
// kernel binary or command line changed; suite hotpatch checks the
// range is untouched (zero) after each kernel's boot.
//
// A patch is logged in the run at the instruction count where it was
// made (replay.js inputPatch: its bytes, as J4 logs keys): a replay
// writes it at the same count, stepping back over it undoes it with the
// memory and stepping forward again redoes it, and a snapshot carries
// the log. Which patches the machine has is always its log's (hpSync):
// the reserved bytes in use, the patched text of each file, and the
// symbols of the copies ("name (patch N)").

import { useSyncExternalStore } from "react";
import { flushSync } from "react-dom";
import { hex } from "./util.js";
import { image, emulator, stepper, runState, symbols, setSymbols, state, provide, api } from "./page.js";
import { DBG, dbgChanged, dbgTypes, dbgFnFacts, dbgSetRemap } from "./dbginfo.js";
import { dwarfPack } from "./dwarftables.js";
import { srcOverlay } from "./srcedit.js";
import { SRC_OVER, srcOverride, srcChanged } from "./srctext.js";
import { lineMap, moveRunTo } from "./buildmap.js";
import { ClangWorker } from "./clang.js";
import { kitSource, kitCompile, kitNow } from "./kit.js";
import { RB } from "./rebuild.jsx";
import { hpPlan, hpLines, hpLookup, hpArgv } from "./hpelf.js";
import { pageWalk } from "./cpustate.js";
import { INPUT, inputPatch } from "./replay.js";
import { Tip } from "./ui.jsx";
import { dbgStatus } from "./dbgtab.js";

const HP_PA = 0x60000, HP_SIZE = 0x20000;

const HP = {
	kits: null,      // kits/index.json's kits, when it loaded
	why: "",
	busy: false,
	can: { ok: false, why: "" },
	title: "",
	status: "",
	state: "",       // "", "checking", "ready", "refused", "done", "failed"
	plan: null,      // the background check's: { key, path, text, plan } or null
	gen: 0,          // checks started (a newer one wins)
	timer: 0,
	cc: null,        // the ClangWorker kept between patches (clang.wasm loaded once)
	kit: null,       // [name, kit JSON]
	objs: new Map(), // "kernel:path:text" -> object bytes
	last: null,      // the last patch made here: { n, ms, ... } for the status
	viewImg: null,   // the kernel hpView showed the patched texts of
	rev: 0,
	subs: new Set(),
	subscribe(fn) {
		HP.subs.add(fn);
		return () => HP.subs.delete(fn);
	},
	changed() {
		this.rev++;
		flushSync(() => {
			for (const fn of this.subs)
				fn();
		});
	},
};

const hpFrom = () => (image ? image.from || image.name : "");
// The patches in the machine: its log's.
const hpPatches = () => INPUT.log.filter((e) => e.patch).map((e) => ({ at: e.at, ...e.patch }));
const hpUsed = () => hpPatches().reduce((n, p) => Math.max(n, p.used), 0);
// Virtual - physical of the kernel's addresses (0 for hello).
const hpKoff = () => {
	const s = symbols ? symbols.symbols.find((x) => !x.patch) : null;
	return s ? s.vaddr - s.paddr : 0;
};

// The text of path the machine runs: its last patch's, else the rebuilt
// kernel's edit, else the kit's (the shipped tree).
async function hpRunning(path) {
	const ps = hpPatches().filter((p) => p.path === path);
	if (ps.length)
		return ps[ps.length - 1].text;
	return hpBuilt(path);
}

async function hpKit() {
	const from = hpFrom();
	if (!HP.kit || HP.kit[0] !== from)
		HP.kit = [from, await kitSource("kits/").kit(from)];
	return HP.kit[1];
}

// The kits list is rebuild.jsx's (one fetch of kits/index.json).
function hpLoadIndex() {
	return Promise.resolve(RB.indexLoad).then(() => {
		HP.kits = RB.kits;
		HP.why = RB.why;
		hpSync();
	});
}

function hpShow(can, status, st) {
	HP.can = can;
	HP.title = can.ok ? status : can.why;
	HP.status = status;
	HP.state = st;
	HP.changed();
}

// The symbols of the copies (name (patch N)) next to the kernel's.
function hpSymbols() {
	if (!symbols)
		return;
	const base = symbols.symbols.filter((s) => !s.patch);
	const koff = hpKoff();
	const add = [];
	for (const p of hpPatches())
		for (const f of p.funcs)
			add.push({ name: f.name + " (patch " + p.n + ")", vaddr: f.va, paddr: f.va - koff, size: f.size, type: "t", section: ".hotpatch", patch: p.n });
	const had = symbols.symbols.filter((s) => s.patch).map((s) => s.name + s.vaddr).join();
	if (had === add.map((s) => s.name + s.vaddr).join())
		return;
	setSymbols({ ...symbols, symbols: base.concat(add).sort((a, b) => a.paddr - b.paddr) });
}

// The line table of the patches' code (DBG.patch, read by dbgTableAt):
// their line rows (from the compiled object's DWARF) and one range per
// copied function, so frames, the Calls and file:line links name the
// edited text's lines.
function hpDebug() {
	const ps = hpPatches();
	const key = ps.map((p) => p.n).join();
	if ((DBG.patch ? DBG.patch.key : "") === key)
		return;
	if (!ps.length) {
		DBG.patch = null;
		dbgChanged();
		return;
	}
	const koff = hpKoff(), strings = [], six = new Map(), rows = [], rg = [];
	const sid = (s) => {
		if (!six.has(s)) {
			six.set(s, strings.length);
			strings.push(s);
		}
		return six.get(s);
	};
	// A file patched again: the earlier patches' copies that still run name
	// lines of their own text, moved to the last one's (the text shown, E8).
	const last = new Map(ps.map((p) => [p.path, p]));
	for (const p of ps) {
		const m = last.get(p.path) !== p ? lineMap(p.text, last.get(p.path).text) : null;
		for (const [va, f, line] of p.lines ? p.lines.rows : [])
			rows.push([va - koff, f < 0 ? -1 : sid(p.lines.files[f]), m && f >= 0 && p.lines.files[f] === p.path && line < m.length ? m[line] : line]);
		for (const f of p.funcs)
			rg.push([f.va - koff, f.va - koff + f.size, 0, sid(f.name), -1, 0]);
	}
	rows.sort((a, b) => a[0] - b[0]);
	rg.sort((a, b) => a[0] - b[0]);
	const t = dwarfPack(strings, rows.map((r) => r[0]), rows.map((r) => r[1]), rows.map((r) => r[2]), rg, rows.map(() => 1), []);
	DBG.patch = { lo: HP_PA, hi: HP_PA + HP_SIZE, t, key };
	DBG.lineCode = new Map(); // Run to file:line includes the copies (dbgFileCode)
	dbgChanged();
}

// The Source panel shows the text the machine runs (E8): each patched
// file's last patch's text (srctext.js SRC_OVER), the kernel's own line
// numbers of that file moved to it (dbginfo.js DBG.remap), so a line
// tapped in the panel, the frames of the copies and of the old code, Run
// to file:line and the line bookmarks all count lines of one text.
let hpViewGen = 0;
async function hpView() {
	const gen = ++hpViewGen;
	// Another kernel: its machine's patches only (the old run's log goes at
	// its boot), and its Run to target was moved by the build already.
	const same = HP.viewImg === image;
	HP.viewImg = image;
	const ps = same ? hpPatches() : [];
	const last = new Map(ps.map((p) => [p.path, p]));
	let changed = false;
	const was = new Map(); // path -> the text shown before (null: the build's)
	for (const path of [...SRC_OVER.keys()])
		if (!last.has(path)) {
			was.set(path, SRC_OVER.get(path).lines.join("\n"));
			srcOverride(path, null);
			changed = true;
		}
	for (const [path, p] of last) {
		const names = [...new Set(ps.filter((x) => x.path === path).flatMap((x) => x.funcs.map((f) => f.name)))];
		const over = "the text the machine runs since patch " + p.n + " (" + names.join(", ") + " run" + (names.length === 1 ? "s" : "") +
			" from the patch's copy; the kernel's other code of this file is counted on this text's lines)";
		const had = SRC_OVER.get(path);
		if (!had || had.over !== over || had.lines.join("\n") !== p.text) {
			was.set(path, had ? had.lines.join("\n") : null);
			srcOverride(path, p.text, over);
			changed = true;
		}
	}
	if (changed)
		srcChanged();
	const files = new Map(), texts = new Map();
	for (const path of new Set([...last.keys(), ...was.keys()])) {
		const base = await hpBuilt(path);
		if (gen !== hpViewGen)
			return;
		if (base === null)
			continue;
		if (last.has(path))
			files.set(path, lineMap(base, last.get(path).text));
		if (was.has(path))
			texts.set(path, [was.get(path) === null ? base : was.get(path), last.has(path) ? last.get(path).text : base]);
	}
	dbgSetRemap(files);
	// The Run to target (the Debugger's breakpoint) counts on the text shown.
	const moved = same ? moveRunTo(texts) : "";
	if (moved)
		dbgStatus(moved);
}

// The text path was built from (no patch): the rebuilt kernel's edit, else the kit's.
async function hpBuilt(path) {
	if (image && image.edits && image.edits[path] !== undefined)
		return image.edits[path];
	return srcOverlay.original(path);
}

// The state changed (an edit, a pause, a patch, a restore, a kernel):
// what the button says now, and the background check of the edit.
function hpSync() {
	hpSymbols();
	hpDebug();
	hpView().catch((e) => console.warn("hot patch view:", e));
	clearTimeout(HP.timer);
	HP.gen++;
	if (HP.busy)
		return;
	HP.changed(); // the list of patches is the log's now
	const no = (why) => hpShow({ ok: false, why }, why, "");
	if (!image || !emulator || !stepper)
		return no("No kernel is running.");
	if (!HP.kits)
		return no(HP.why ? "No build kits on this site (" + HP.why + ")." : "Reading the list of build kits…");
	if (!HP.kits[hpFrom()])
		return no(hpFrom() + " has no build kit: the page cannot compile its code.");
	if (runState !== "paused")
		return no("Pause the machine first: a patch goes into the stopped machine, then Continue runs it.");
	const gen = HP.gen;
	HP.timer = setTimeout(() => hpCheck(gen).catch((e) => {
		if (gen === HP.gen)
			hpShow({ ok: false, why: "The check failed: " + e.message }, "The check failed: " + e.message, "refused");
	}), 300);
}

// One object of the unit with text, compiled once per text.
async function hpObject(kit, o, path, text, which) {
	const key = kit.name + ":" + path + ":" + which + ":" + text;
	if (!HP.objs.has(key)) {
		if (!HP.cc) {
			HP.cc = new ClangWorker();
			await HP.cc.load();
		}
		const r = await kitCompile(HP.cc, kit, kitSource("kits/"), o, hpArgv(o.argv, o.out + ".hp-" + which + ".o"), { [path]: text });
		if (r.code !== 0)
			throw new Error((which === "after" ? "your edit does not compile: " : "the running text does not compile: ") + r.stderr.slice(0, 600));
		HP.objs.set(key, r.bytes);
		if (HP.objs.size > 8) // the texts of the last few checks only
			HP.objs.delete(HP.objs.keys().next().value);
	}
	return HP.objs.get(key);
}

// The plan of the current edit, or { why }: path, texts, plan.
async function hpMake(gen) {
	const diff = [];
	for (const p of srcOverlay.paths()) {
		const t = await srcOverlay.read(p);
		if (t !== null && t !== await hpRunning(p))
			diff.push([p, t]);
	}
	if (!diff.length)
		return { why: "No edit differs from the code the machine runs: edit a C file of " + hpFrom() + " first." };
	if (diff.length > 1)
		return { why: diff.length + " files differ from the code the machine runs (" + diff.map((d) => d[0]).join(", ") +
			"): a patch takes one C file; Rebuild and boot takes them all." };
	const [path, text] = diff[0];
	const kit = await hpKit();
	const o = kit.objects.find((x) => kit.files[x.deps[0]][0] === path && x.argv[0] === "clang");
	if (!o) {
		const n = kit.objects.filter((x) => x.deps.some((d) => kit.files[d][0] === path)).length;
		return { why: path + (n ? " is included by " + n + " object" + (n === 1 ? "" : "s") + ": a header edit changes every unit that includes it, Rebuild and boot instead." :
			" is not compiled on its own by this kernel's build.") };
	}
	if (gen !== HP.gen)
		return null;
	hpShow({ ok: false, why: "Checking whether your edit of " + path + " can be patched in: compiling it…" },
		"Checking whether your edit of " + path + " can be patched in: compiling " + o.out.replace(/\.o$/, ".c") + " twice…", "checking");
	const before = await hpObject(kit, o, path, await hpRunning(path), "before");
	const after = await hpObject(kit, o, path, text, "after");
	if (gen !== HP.gen)
		return null;
	const ty = dbgTypes();
	const koff = hpKoff(), used = hpUsed();
	const lto = o.argv.filter((x) => /^-f(no-)?lto/.test(x)).pop() || "";
	const plan = hpPlan(before, after, { base: HP_PA + used + koff, room: HP_SIZE - used, src: [await hpRunning(path), text],
		lookup: hpLookup(symbols.symbols.filter((s) => !s.patch), ty.state === "ok" ? ty.T : null, path),
		// The running kernel's bytes, to check they are what "before" compiles to.
		read: (va, n) => emulator.read_memory(va - koff, n), lto: lto.startsWith("-flto"),
		jumped: new Set(hpPatches().flatMap((p) => p.funcs.map((f) => f.old)).filter((x) => x !== null)) });
	// The new code's line rows (its debug info follows it).
	let lines = null;
	if (plan.ok)
		try {
			lines = hpLines(after, plan.at, kit.dir + "/");
		} catch (e) {
			plan.notes.push("no line rows for the new code (" + e.message + ")");
		}
	return { path, text, plan, used, koff, lines };
}

async function hpCheck(gen) {
	const r = await hpMake(gen);
	if (!r || gen !== HP.gen)
		return;
	if (r.why || !r.plan.ok) {
		HP.plan = null;
		const why = r.why || "Cannot patch this edit: " + r.plan.why + ".";
		return hpShow({ ok: false, why }, why, r.why ? "" : "refused");
	}
	HP.plan = { gen, ...r };
	const p = r.plan;
	const text = "Ready: Patch running kernel replaces " + p.changed.join(", ") + (p.added.length ? " (adds " + p.added.join(", ") + ")" : "") +
		" in the paused machine with " + p.code.length + " bytes of new code at " + hex(HP_PA + r.used) + "." + (p.notes.length ? " Note: " + p.notes.join("; ") + "." : "");
	hpShow({ ok: true }, text, "ready");
}

// What the running machine says against the plan: null or why not.
async function hpMachineWhy(r) {
	const cpu = emulator.v86.cpu, p = r.plan;
	const eip = cpu.instruction_pointer[0] >>> 0;
	for (const j of p.jumps)
		if (eip > j.va && eip < j.va + 5)
			return "the CPU is stopped inside the first 5 bytes of " + j.name + ", which the jump replaces: Step past them first";
	const va = HP_PA + r.used + r.koff;
	if (cpu.cr[0] & 0x80000000) {
		const w = pageWalk(cpu, va);
		if (w.phys !== HP_PA + r.used)
			return "the reserved memory (physical " + hex(HP_PA) + ") is not mapped at " + hex(va) + " by the running kernel's page tables";
	}
	// A function the kernel inlined into others (LTO, a header) keeps those copies.
	for (const name of p.changed) {
		let f = dbgFnFacts(name);
		for (let i = 0; i < 100 && f.state === "loading"; i++) {
			await new Promise((ok) => setTimeout(ok, 100));
			f = dbgFnFacts(name);
		}
		if (f.state !== "ok")
			continue;
		for (const { e } of f.fns)
			for (const at of e[8] || []) {
				const s = api.symbols.symbolAt(symbols.symbols, at);
				if (!s || !p.changed.includes(s.name))
					return name + " is also inlined into " + (s ? s.name : "code at " + hex(at)) + " in the running kernel, which the patch does not replace";
			}
	}
	return null;
}

// Pressed: the background check's plan (made again if stale), written.
async function hpRun() {
	const t0 = kitNow();
	HP.busy = true;
	try {
		let r = HP.plan && HP.plan.gen === HP.gen ? HP.plan : null;
		if (!r) {
			r = await hpMake(HP.gen);
			if (!r || r.why || !r.plan.ok)
				throw new Error(r ? r.why || r.plan.why : "the machine changed meanwhile");
		}
		const why = await hpMachineWhy(r);
		if (why)
			throw new Error(why);
		const p = r.plan, n = hpPatches().length + 1;
		const hx = (b) => Array.from(b, (x) => (x < 16 ? "0" : "") + x.toString(16)).join("");
		const writes = [[HP_PA + r.used, hx(p.code)]].concat(p.jumps.map((j) => [j.va - r.koff, hx(j.bytes)]));
		const used = r.used + Math.ceil(p.code.length / 16) * 16;
		inputPatch(stepper, { n, path: r.path, text: r.text, used, writes, lines: r.lines,
			funcs: p.funcs.map((f) => ({ name: f.name, va: f.va, size: f.size, old: f.old })) });
		HP.busy = false;
		HP.last = { n, ms: kitNow() - t0, at: stepper.steps };
		hpSymbols();
		hpDebug();
		hpView().catch((e) => console.warn("hot patch view:", e));
		HP.gen++;
		HP.plan = null;
		const text = "Patch " + n + " is in: " + p.changed.map((c) => c + " (" + hex(p.funcs.find((f) => f.name === c).old) + ")").join(", ") +
			" now jump" + (p.changed.length === 1 ? "s" : "") + " to the new code at " + hex(p.funcs[0].va) + " (" + p.code.length + " bytes), made at instruction " +
			stepper.steps.toLocaleString("en") + " in " + ((kitNow() - t0) / 1000).toFixed(2) + " s. Continue runs it.";
		// The machine runs the edit now: nothing to patch until the next one.
		hpShow({ ok: false, why: "The machine runs your edit: Continue runs it; edit again to patch again." }, text, "done");
	} catch (e) {
		HP.busy = false;
		HP.gen++;
		HP.plan = null;
		hpShow({ ok: false, why: "Not patched: " + e.message + "." }, "Not patched: " + e.message + ".", "failed");
	}
}

function initHotPatch() {
	state.subscribe(hpSync, ["runState", "image", "emulator"]);
	// A kernel's symbols loaded after its boot get the copies' too.
	state.subscribe(hpSymbols, ["symbols"]);
	hpLoadIndex();
}

const HP_TIP = "Puts your edit into the running machine without a reboot: while the machine is paused and one C file differs from the code it runs, " +
	"the page compiles that file's unit twice (the text it runs and your edit, clang.wasm, the kernel's own build command without LTO) and compares them function by function. " +
	"The changed functions are copied, with the strings and tables they use, into 128 KiB of memory reserved for patches (physical 0x60000, which Linux never allocates: " +
	"it keeps the first megabyte for itself), their calls and variables point at the running kernel's, and each old function starts with a jump to its copy. Continue runs the new code. " +
	"Refused, with the reason, when the edit changes data (a variable added or initialised differently), a function's signature, a function the kernel inlined elsewhere, " +
	"or needs what the kernel set up at boot (static keys, exception tables): then Rebuild and boot. A patch is part of the run: a replay makes it at the same instruction, " +
	"stepping back undoes it, snapshots keep it.";

// The Source tab's box (#hp_box, under Rebuild and boot).
function HotPatchPanel() {
	useSyncExternalStore(HP.subscribe, () => HP.rev);
	const ps = hpPatches();
	return (
		<>
			<div className="scan"><button id="hp_go" type="button" className="rbgo" disabled={!HP.can.ok || HP.busy} title={HP.title}
				onClick={hpRun}><span aria-hidden="true">⚡</span> Patch running kernel</button>{" "}<Tip text={HP_TIP} /></div>
			<p id="hp_status" className="note" role="status" data-state={HP.state}>{HP.status}</p>
			<ul id="hp_list" className="hplist" hidden={!ps.length}>{ps.map((p) => (
				<li key={p.n} data-n={p.n}>{"Patch " + p.n + " at instruction " + p.at.toLocaleString("en") + ": " + p.path + ", " + p.funcs.map((f) => f.name).join(", ")}</li>
			))}</ul>
		</>
	);
}

// The editor bar's button (next to its Rebuild and boot).
function HotPatchButton() {
	useSyncExternalStore(HP.subscribe, () => HP.rev);
	return (
		<button id="hp_go_panel" type="button" className="rbgo" disabled={!HP.can.ok || HP.busy} title={HP.title || undefined} onClick={hpRun}>
			<span aria-hidden="true">⚡</span>{" "}<span className="rblabel">Patch running kernel</span>
		</button>
	);
}

provide({ hpSync });
export { HP, HP_PA, HP_SIZE, initHotPatch, HotPatchPanel, HotPatchButton, hpPatches };
