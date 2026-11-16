"use strict";

// Tabs, the URL hash, compact tab labels.

import { hex, PAGE, $ } from "./util.js";
import { tab, selectedPage, setTab, setVmHasKeys, emulator, navBuilt, setSelectedPage, provide } from "./page.js";
import { graphShow } from "./graph.js";
import { srcPanel, srcShowFile } from "./source.js";
import { covShow } from "./coverage.js";
import { MEV } from "./memevents.js";
import { MMAP } from "./memmap.js";
import { QUESTS } from "./quests.js";
import { renderSettings } from "./settings.js";
import { bmFromHash } from "./bookmarks.js";
import { drawGrid } from "./memview.js";
import { inspect } from "./listing.js";
import { renderContext } from "./debugui.js";

// ---- tabs -----------------------------------------------------------------

const TABS = ["machine", "memory", "page", "debugger", "calls", "source", "snapshots", "events", "quests", "settings", "about"];
// The names of renamed tabs in old links (#graph, ?tab=coverage).
const TAB_ALIASES = { graph: "calls", coverage: "source" };

// Hash: #memory, #page/0x100000, #debugger, ...; with the Source panel open
// its file and line after ~src= (#debugger~src=init/main.c:1000), so a
// reload opens that file again.
const navHashNow = () => "#" + tab + (tab === "page" && selectedPage >= 0 ? "/" + hex(selectedPage * PAGE) : "") + srcHash();

function srcHash() {
	if (!srcPanel || $("src_panel").hidden)
		return "";
	const l = srcPanel.pick || srcPanel.line;
	return "~src=" + encodeURI(srcPanel.path) + (l ? ":" + l : "");
}

// The hash the page last wrote or read. Another one is a navigation (a
// link, the address bar) whose hashchange has not run yet: writeHash reads
// it first instead of writing over it.
let hashOwn = null;

function writeHash() {
	if (hashOwn !== null && location.hash !== hashOwn)
		return readHash();
	const h = navHashNow();
	if (location.hash !== h)
		history.replaceState(history.state, "", h); // keeps the entry's place in the jump history
	hashOwn = location.hash;
}

function showTab(name) {
	name = TAB_ALIASES[name] || name;
	setTab(TABS.includes(name) ? name : "machine");
	for (const t of TABS) {
		$("panel_" + t).hidden = t !== tab;
		$("tab_" + t).setAttribute("aria-selected", t === tab);
	}
	vmKeys();
	writeHash();
	if (tab === "page")
		inspect();
	if (tab === "memory")
		drawGrid();
	if (tab === "debugger")
		renderContext();
	if (tab === "settings")
		renderSettings();
	MMAP.place(tab); // memmap.js: the live memory map goes with the Quests tab
	MEV.shown(tab === "events"); // memevents.js
	QUESTS.shown(tab === "quests"); // quests.js
	graphShow(tab === "calls");
	covShow(tab === "source");
}

// The VM's screen is in the Machine tab and is hidden (it keeps changing)
// on the others: keys go to the VM only while it is shown, and Alt+Left/Right
// go to the jump history until the screen is clicked again.
function vmKeys() {
	if (tab !== "machine")
		setVmHasKeys(false);
	if (emulator)
		emulator.keyboard_set_status(tab === "machine");
}

// Compact tabs (Settings tab): the tab buttons show only their emoji, the
// name is their tooltip and stays their accessible name.
const COMPACT_KEY = "mkb-compact-tabs";
// Default on a phone: at 400 px and less the names take three rows.
const compactDefault = () => window.matchMedia("(max-width: 400px)").matches;

function setCompactTabs(on) {
	document.body.classList.toggle("compact-tabs", on);
	$("compact_tabs").checked = on;
	for (const t of TABS) {
		const b = $("tab_" + t);
		if (on)
			b.dataset.tip = b.getAttribute("aria-label");
		else
			delete b.dataset.tip;
	}
}

function initCompactTabs() {
	let saved = null;
	try {
		saved = localStorage.getItem(COMPACT_KEY);
	} catch (e) {
		// no storage: the default
	}
	setCompactTabs(saved === null ? compactDefault() : saved === "1");
	$("compact_tabs").addEventListener("change", () => {
		setCompactTabs($("compact_tabs").checked);
		try {
			localStorage.setItem(COMPACT_KEY, $("compact_tabs").checked ? "1" : "0");
		} catch (e) {
			// no storage: until reload
		}
	});
}

// A hash change (typed, or a link) opens that tab and page. Going back
// through the jump history changes the hash too: navRestore has drawn that
// view already, with its scroll positions, and the hash says what is shown.
function readHash() {
	bmFromHash(); // a link's #bm=: the bookmark is opened when its kernel runs
	hashOwn = location.hash;
	if (navBuilt && location.hash === navHashNow())
		return;
	let h = location.hash.slice(1), src = null;
	const at = h.indexOf("~src=");
	if (at >= 0) {
		try {
			src = /^(.+?)(?::(\d+))?$/.exec(decodeURI(h.slice(at + 5)));
		} catch (e) {
			src = null; // a malformed hash: no file
		}
		h = h.slice(0, at);
	}
	let [name, addr] = h.split("/");
	if (!name && !navBuilt) {
		// ?tab=calls when the URL has no hash; ?quests=1, ?memevents=1 open their tabs
		const q = new URLSearchParams(location.search);
		name = q.get("tab") || (q.get("quests") === "1" ? "quests" : q.get("memevents") === "1" ? "events" : null);
	}
	if (addr) {
		const a = parseInt(addr, 16);
		if (Number.isFinite(a))
			setSelectedPage(Math.floor(a / PAGE));
	}
	showTab(name || "machine");
	// The file of the hash, without a history entry (a reload, a link).
	if (src && (!srcPanel || $("src_panel").hidden || srcPanel.path !== src[1] || (srcPanel.pick || srcPanel.line || 0) !== Number(src[2] || 0)))
		srcShowFile(src[1], Number(src[2] || 0));
}

// Other modules change them through these (an imported binding is read-only).
const setHashOwn = (v) => { hashOwn = v; };

// The page API's entries of this module (page.js).
provide({ showTab, writeHash, setCompactTabs, compactDefault });

export { navHashNow, hashOwn, showTab, writeHash, vmKeys, initCompactTabs, readHash, TABS, compactDefault,
	setCompactTabs, setHashOwn };
