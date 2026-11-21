"use strict";

// Snapshots compared (snapshots.js): two snapshots or one and now, the
// registers, the pages and the hex of a page.

import { realNow } from "./clock.js";
import { kib, hex, $, h4, h8, PAGE, esc, asciiOf } from "./util.js";
import { emulator, emuRam, setSelectedPage, api } from "./page.js";
import { snapById, snapBlob, snapMeta, snapWorker, snapCount } from "./snaprecord.js";
import { controller } from "./ui.jsx";
let snapDiff = null;      // { a, b, pages, ... } of the diff shown
let pageSnapView = null;  // the Page tab shows a snapshot's page: { page, bytes, base, name, against, cur }
// Other modules set them through these (an imported binding is read-only).
const setSnapDiff = (d) => { snapDiff = d; };
const setPageSnapView = (v) => { pageSnapView = v; };

// One side of a diff for the worker. "now" is a save_state() of the
// running machine: one consistent copy of the CPU and RAM, and v86 packs
// only the non-zero pages, so it costs what Save snapshot costs (copying
// all of RAM on the main thread took 295 ms for 64 MiB at a 4x throttle).
async function snapSide(id, fresh) {
	if (id !== "now") {
		const s = snapById(id);
		return snapBlob(s);
	}
	return fresh ? { key: "now", raw: await emulator.save_state() } : { key: "now" };
}

function snapNowInfo() {
	const meta = snapMeta(emulator);
	return { name: "now", meta, ram: emuRam };
}

// What the diff box shows besides snapDiff (snapshotspanel.jsx draws it):
// the status line, the state of the last diff ("running", "done",
// "error"), how many pages it compared (-1: all), whether one runs, and the
// pickers' choices (null: the default, the newest two).
// n counts the diffs made (the box draws a new one afresh: its groups open).
const DIFF = controller({ status: "", state: "", compared: -1, busy: false, pickA: null, pickB: null, n: 0 });

// Diff a and b (snapshot ids or "now"), the pickers' values.
async function snapRunDiff(a, b) {
	if (!a || !b || !emulator)
		return;
	DIFF.state = "running";
	DIFF.status = "Decompressing and comparing...";
	DIFF.busy = true;
	DIFF.changed();
	const t0 = realNow();
	try {
		const now = a === "now" || b === "now" ? snapNowInfo() : null;
		const sa = await snapSide(a, true), sb = await snapSide(b, a !== "now");
		const tr = [sa, sb].filter((x) => x.raw).map((x) => x.raw);
		const r = await snapWorker({ op: "diff", a: sa, b: sb }, tr);
		const info = (id, cpu) => {
			const s = id === "now" ? now : snapById(id);
			const regs = { ...(s.meta ? s.meta.regs : {}), ...cpu.regs };
			return { id, name: s.name, meta: s.meta, regs, ram: s.ram };
		};
		snapDiff = { a: info(a, r.a), b: info(b, r.b), pages: r.pages, ms: Math.round(realNow() - t0), workerMs: r.ms, hex: null };
		DIFF.n++;
		DIFF.compared = r.compared;
		DIFF.status = "Compared in " + snapDiff.ms + " ms (the comparison itself " + r.ms + " ms, in a Worker" +
			(r.compared >= 0 ? "; one side is a delta of the other, so only its " + r.compared.toLocaleString("en") + " page" +
				(r.compared === 1 ? " was" : "s were") + " compared" : "") + ").";
		DIFF.state = "done";
	} catch (e) {
		DIFF.status = "Diff failed: " + ((e && e.message) || e);
		DIFF.state = "error";
	}
	DIFF.busy = false;
	DIFF.changed();
}

const SNAP_REGS = ["EIP", "EFLAGS", "EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP", "ESP", "CS", "DS", "SS", "ES", "FS", "GS", "CR0", "CR2", "CR3", "CR4"];

function snapRegText(n, v) {
	if (v === undefined)
		return "-";
	if (/^[CDSEFG]S$/.test(n))
		return "0x" + h4(v);
	let t = "0x" + h8(v);
	if (n === "EFLAGS")
		t += " " + api.machine.FLAG_BITS.filter(([b]) => v & (1 << b)).map(([, f]) => f).join(" ");
	return t;
}

// The diff shown changed (or is gone): the box draws it again.
function renderSnapDiff() {
	DIFF.changed();
}

// What the diff's summary, registers and screen show, from snapDiff.
function snapDiffView() {
	const d = snapDiff;
	const A = d.a, B = d.b;
	const nm = (x) => (x.id === "now" ? "now" : "“" + x.name + "”");
	const nA = snapCount(A.meta), nB = snapCount(B.meta);
	const sum = "From " + nm(A) + " to " + nm(B) + ": instruction " + nA.toLocaleString("en") + " → " +
		nB.toLocaleString("en") + " (" + (nB >= nA ? "+" + (nB - nA).toLocaleString("en") :
		"earlier by " + (nA - nB).toLocaleString("en")) + "), EIP " +
		(A.meta.sym || hex(A.meta.eip)) + " → " + (B.meta.sym || hex(B.meta.eip)) + "; " + d.pages.length + " of " +
		Math.ceil(Math.max(A.ram, B.ram) / PAGE).toLocaleString("en") + " pages of RAM changed, " +
		d.pages.reduce((n, p) => n + p[1], 0).toLocaleString("en") + " bytes." +
		(A.ram !== B.ram ? " The RAM sizes differ (" + kib(A.ram) + ", " + kib(B.ram) + "): pages on one side only count as changed." : "");
	const regs = [], same = [];
	for (const n of SNAP_REGS) {
		const o = A.regs[n], v = B.regs[n];
		if (o === v)
			same.push(n);
		else
			regs.push({ n, old: o === undefined ? "" : hex(o), new: v === undefined ? "" : hex(v), oldText: snapRegText(n, o), newText: snapRegText(n, v) });
	}
	const la = A.meta.screen || [], lb = B.meta.screen || [];
	const screen = [];
	for (let i = 0; i < Math.max(la.length, lb.length); i++) {
		const o = la[i] || "", v = lb[i] || "";
		if (o !== v)
			screen.push({ i, o, v });
	}
	return { nA, nB, sum, regs, same, screen };
}

// Changed pages grouped by region / ELF section, with their symbols.
function snapPageGroups() {
	const groups = new Map();
	for (const [p, n] of snapDiff.pages) {
		const g = p * PAGE < emuRam ? api.memory.pageLabel(p) || "RAM" : "beyond this machine's RAM";
		if (!groups.has(g))
			groups.set(g, []);
		groups.get(g).push([p, n, api.memory.symbolsInPage(p)]);
	}
	return groups;
}

// A side's name in the page rows' buttons: "now" or "#seq".
const snapSideTag = (x) => (x.id === "now" ? "now" : "#" + snapById(x.id).seq);

async function snapPages(p) {
	const d = snapDiff;
	return snapWorker({ op: "pages", a: await snapSide(d.a.id, false), b: await snapSide(d.b.id, false), p });
}

async function snapShowHex(p) {
	const d = snapDiff;
	try {
		const r = await snapPages(p);
		if (d !== snapDiff)
			return;
		d.hex = { p, a: r.a, b: r.b, all: false };
	} catch (e) {
		DIFF.status = "Reading the page failed: " + ((e && e.message) || e);
	}
	DIFF.changed();
}

function snapHexAll() {
	snapDiff.hex.all = true;
	DIFF.changed();
}

// The hexdump's two columns: per side its title and the rows' HTML (bytes
// that differ in <mark>), and the count of bytes that differ.
function snapHexView(per) {
	const d = snapDiff, h = d.hex;
	let nChg = 0;
	for (let i = 0; i < PAGE; i++)
		nChg += h.a[i] !== h.b[i];
	const cols = [];
	for (const [side, mine, other, x] of [["a", h.a, h.b, d.a], ["b", h.b, h.a, d.b]]) {
		let html = "";
		for (let o = 0; o < PAGE; o += per) {
			let chg = false;
			for (let i = o; i < o + per; i++)
				chg = chg || mine[i] !== other[i];
			if (!chg && !h.all)
				continue;
			html += h8(h.p * PAGE + o) + " ";
			let asc = "";
			for (let i = o; i < o + per; i++) {
				const b = mine[i].toString(16).padStart(2, "0");
				html += " " + (mine[i] !== other[i] ? "<mark>" + b + "</mark>" : b);
				asc += esc(asciiOf(mine[i]));
			}
			html += "  " + asc + "\n";
		}
		cols.push({ side, title: x.id === "now" ? "now" : "#" + snapById(x.id).seq + " " + x.name, html: html || "(no row differs)" });
	}
	return { nChg, cols, label: "Page 0x" + h8(h.p * PAGE) + " (" + api.memory.pageLabel(h.p) + "): " + nChg + " bytes differ, marked yellow" };
}

// The Page tab at a snapshot's page (side "a" or "b"); Back returns here.
async function snapOpenPage(p, which) {
	const d = snapDiff;
	let r;
	try {
		r = await snapPages(p);
	} catch (e) {
		DIFF.status = "Reading the page failed: " + ((e && e.message) || e);
		DIFF.changed();
		return;
	}
	if (d !== snapDiff)
		return;
	const me = which === "a" ? d.a : d.b, other = which === "a" ? d.b : d.a;
	const label = (x) => (x.id === "now" ? "now (when diffed)" : "“" + x.name + "”");
	const view = { page: p, bytes: which === "a" ? r.a : r.b, base: which === "a" ? r.b : r.a,
		id: me.id, name: label(me), againstId: other.id, against: label(other), cur: me.meta.phys };
	api.show.navJump(() => {
		pageSnapView = view;
		setSelectedPage(p);
		api.memory.drawGrid();
		api.show.setLive();
		api.show.showTab("page");
		api.show.inspect();
	});
}

function pageSnapBanner(on) {
	const el = $("page_snap");
	el.hidden = !on;
	if (on)
		$("page_snap_text").textContent = "Showing snapshot " + pageSnapView.name + "; yellow bytes differ from " +
			pageSnapView.against + ". ";
}

export { snapDiff, pageSnapView, setSnapDiff, setPageSnapView, snapRunDiff, renderSnapDiff, snapPages,
	pageSnapBanner, DIFF, snapDiffView, snapPageGroups, snapSideTag, snapShowHex, snapHexAll, snapHexView, snapOpenPage };
