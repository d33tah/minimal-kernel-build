"use strict";

// Snapshots compared (snapshots.js): two snapshots or one and now, the
// registers, the pages and the hex of a page.

import { realNow } from "./clock.js";
import { kib, hex, $, h4, h8, PAGE, esc, asciiOf } from "./util.js";
import { emulator, emuRam, setSelectedPage, api } from "./page.js";
import { snapById, snapBlob, snapMeta, snapWorker, snapCount, snapButton } from "./snaprecord.js";
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

async function snapRunDiff() {
	const a = $("snap_a").value, b = $("snap_b").value;
	if (!a || !b || !emulator)
		return;
	const out = $("snap_diff_out");
	out.dataset.state = "running";
	$("snap_diff_status").textContent = "Decompressing and comparing...";
	$("snap_diff").disabled = true;
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
		renderSnapDiff();
		out.dataset.compared = r.compared >= 0 ? r.compared : "all";
		$("snap_diff_status").textContent = "Compared in " + snapDiff.ms + " ms (the comparison itself " + r.ms + " ms, in a Worker" +
			(r.compared >= 0 ? "; one side is a delta of the other, so only its " + r.compared.toLocaleString("en") + " page" +
				(r.compared === 1 ? " was" : "s were") + " compared" : "") + ").";
		out.dataset.state = "done";
	} catch (e) {
		$("snap_diff_status").textContent = "Diff failed: " + ((e && e.message) || e);
		out.dataset.state = "error";
	}
	$("snap_diff").disabled = false;
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

function renderSnapDiff() {
	const out = $("snap_diff_out");
	const d = snapDiff;
	out.hidden = !d;
	if (!d)
		return;
	const A = d.a, B = d.b;
	const nm = (x) => (x.id === "now" ? "now" : "“" + x.name + "”");
	out.dataset.icountA = A.meta.icount;
	out.dataset.icountB = B.meta.icount;
	const nA = snapCount(A.meta), nB = snapCount(B.meta);
	out.dataset.countA = nA;
	out.dataset.countB = nB;
	const sum = $("snap_diff_sum");
	sum.textContent = "From " + nm(A) + " to " + nm(B) + ": instruction " + nA.toLocaleString("en") + " → " +
		nB.toLocaleString("en") + " (" + (nB >= nA ? "+" + (nB - nA).toLocaleString("en") :
		"earlier by " + (nA - nB).toLocaleString("en")) + "), EIP " +
		(A.meta.sym || hex(A.meta.eip)) + " → " + (B.meta.sym || hex(B.meta.eip)) + "; " + d.pages.length + " of " +
		Math.ceil(Math.max(A.ram, B.ram) / PAGE).toLocaleString("en") + " pages of RAM changed, " +
		d.pages.reduce((n, p) => n + p[1], 0).toLocaleString("en") + " bytes." +
		(A.ram !== B.ram ? " The RAM sizes differ (" + kib(A.ram) + ", " + kib(B.ram) + "): pages on one side only count as changed." : "");
	// registers
	const tb = $("snap_regs").tBodies[0];
	tb.textContent = "";
	const same = [];
	for (const n of SNAP_REGS) {
		const o = A.regs[n], v = B.regs[n];
		if (o === v) {
			same.push(n);
			continue;
		}
		const tr = tb.insertRow();
		Object.assign(tr.dataset, { reg: n, old: o === undefined ? "" : hex(o), new: v === undefined ? "" : hex(v) });
		tr.insertCell().textContent = n;
		tr.insertCell().textContent = snapRegText(n, o);
		tr.insertCell().textContent = "→";
		const c = tr.insertCell();
		c.textContent = snapRegText(n, v);
		c.className = "snapnew";
	}
	$("snap_regs").hidden = !tb.rows.length;
	$("snap_regs_same").textContent = tb.rows.length ? "Unchanged: " + same.join(" ") + "." : "No register changed.";
	// screen
	const sc = $("snap_screen");
	sc.textContent = "";
	const la = A.meta.screen || [], lb = B.meta.screen || [];
	let nScr = 0;
	for (let i = 0; i < Math.max(la.length, lb.length); i++) {
		const o = la[i] || "", v = lb[i] || "";
		if (o === v)
			continue;
		nScr++;
		const row = document.createElement("div");
		row.dataset.line = i;
		row.className = "snapscr";
		const n = document.createElement("span");
		n.className = "snapln";
		n.textContent = "line " + (i + 1);
		const po = document.createElement("pre");
		po.textContent = "- " + o;
		const pv = document.createElement("pre");
		pv.textContent = "+ " + v;
		pv.className = "snapnew";
		row.append(n, po, pv);
		sc.append(row);
	}
	if (!nScr)
		sc.textContent = "No line of the VGA text screen changed.";
	renderSnapPages();
	renderSnapHex();
}

// Changed pages grouped by region / ELF section, with their symbols.
function renderSnapPages() {
	const box = $("snap_pages");
	box.textContent = "";
	const d = snapDiff;
	if (!d.pages.length) {
		box.textContent = "No page of RAM changed.";
		return;
	}
	const groups = new Map();
	for (const [p, n] of d.pages) {
		const g = p * PAGE < emuRam ? api.memory.pageLabel(p) || "RAM" : "beyond this machine's RAM";
		if (!groups.has(g))
			groups.set(g, []);
		groups.get(g).push([p, n]);
	}
	for (const [g, list] of groups) {
		const det = document.createElement("details");
		det.open = true;
		const sumEl = document.createElement("summary");
		sumEl.textContent = g + ": " + list.length + " page" + (list.length === 1 ? "" : "s") + ", " +
			list.reduce((n, x) => n + x[1], 0).toLocaleString("en") + " bytes changed";
		det.append(sumEl);
		for (const [p, n] of list) {
			const row = document.createElement("div");
			row.className = "snappg";
			Object.assign(row.dataset, { page: p, bytes: n });
			const syms = api.memory.symbolsInPage(p);
			const addr = document.createElement("code");
			addr.textContent = "0x" + h8(p * PAGE);
			row.append(addr, " " + n.toLocaleString("en") + " byte" + (n === 1 ? "" : "s") +
				(syms.length ? " · " + syms.slice(0, 3).map((s) => s.name).join(", ") + (syms.length > 3 ? " +" + (syms.length - 3) : "") : "") + " ");
			row.append(snapButton("hexdump", "snaphex", "Both sides of this page, byte by byte", () => snapShowHex(p)), " ",
				snapButton("Page tab: " + (d.b.id === "now" ? "now" : "#" + snapById(d.b.id).seq), "snapopen",
					"Open this page in the Page tab as " + (d.b.id === "now" ? "it is now" : "“" + d.b.name + "” had it") +
					"; yellow bytes differ from " + (d.a.id === "now" ? "now" : "“" + d.a.name + "”"), () => snapOpenPage(p, "b")), " ",
				snapButton("Page tab: " + (d.a.id === "now" ? "now" : "#" + snapById(d.a.id).seq), "snapopena",
					"Open this page in the Page tab as " + (d.a.id === "now" ? "it is now" : "“" + d.a.name + "” had it"), () => snapOpenPage(p, "a")));
			det.append(row);
		}
		box.append(det);
	}
}

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
		renderSnapHex();
	} catch (e) {
		$("snap_diff_status").textContent = "Reading the page failed: " + ((e && e.message) || e);
	}
}

function renderSnapHex() {
	const box = $("snap_hex");
	const d = snapDiff, h = d && d.hex;
	box.hidden = !h;
	box.textContent = "";
	if (!h) {
		delete box.dataset.page;
		return;
	}
	const per = window.matchMedia("(max-width: 640px)").matches ? 8 : 16;
	const head = document.createElement("p");
	head.className = "note";
	let nChg = 0;
	for (let i = 0; i < PAGE; i++)
		nChg += h.a[i] !== h.b[i];
	head.append("Page 0x" + h8(h.p * PAGE) + " (" + api.memory.pageLabel(h.p) + "): " + nChg + " bytes differ, marked yellow" +
		(h.all ? "." : "; rows without a change are left out. "));
	if (!h.all)
		head.append(snapButton("all rows", "", "Show all " + PAGE / per + " rows", () => {
			h.all = true;
			renderSnapHex();
		}));
	const grid = document.createElement("div");
	grid.className = "snaphexgrid";
	for (const [side, mine, other, x] of [["a", h.a, h.b, d.a], ["b", h.b, h.a, d.b]]) {
		const col = document.createElement("div");
		col.className = "snapcol snapcol-" + side;
		const t = document.createElement("b");
		t.textContent = x.id === "now" ? "now" : "#" + snapById(x.id).seq + " " + x.name;
		const pre = document.createElement("pre");
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
		pre.innerHTML = html || "(no row differs)";
		col.append(t, pre);
		grid.append(col);
	}
	box.append(head, grid);
	box.dataset.page = h.p;
}

// The Page tab at a snapshot's page (side "a" or "b"); Back returns here.
async function snapOpenPage(p, which) {
	const d = snapDiff;
	let r;
	try {
		r = await snapPages(p);
	} catch (e) {
		$("snap_diff_status").textContent = "Reading the page failed: " + ((e && e.message) || e);
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
	pageSnapBanner };
