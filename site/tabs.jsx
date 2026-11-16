"use strict";

// Tabs, the URL hash, compact tab labels. The tab bar is a React component
// (R3) on page.js's `tab` and `compactTabs`.

import { useLayoutEffect } from "react";
import { hex, PAGE, $ } from "./util.js";
import { tab, selectedPage, setTab, setVmHasKeys, emulator, navBuilt, setSelectedPage, setCompactTabs, markTabShown, provide } from "./page.js";
import { graphShow } from "./graph.js";
import { srcPanel, srcShowFile } from "./source.js";
import { bmFromHash } from "./bookmarks.js";
import { useStore } from "./ui.jsx";

// ---- tabs -----------------------------------------------------------------

const TABS = ["machine", "memory", "page", "debugger", "calls", "source", "snapshots", "events", "quests", "settings", "about"];
// The names of renamed tabs in old links (#graph, ?tab=coverage).
const TAB_ALIASES = { graph: "calls", coverage: "source" };
// Each tab's button: its emoji and its name.
const TAB_LOOK = {
	machine: ["🖥️", "Machine"], memory: ["🗺️", "Memory"], page: ["🔢", "Page"], debugger: ["🐞", "Debugger"],
	calls: ["📊", "Calls"], source: ["📝", "Source"], snapshots: ["💾", "Snapshots"], events: ["📋", "Events"],
	quests: ["🎯", "Quests"], settings: ["⚙️", "Settings"], about: ["ℹ️", "About"],
};

// The tab bar. Compact (Settings): the buttons show only their emoji, the
// name is their tooltip and stays their accessible name.
function TabBar() {
	const shown = useStore("tab");
	const compact = useStore("compactTabs");
	useLayoutEffect(() => {
		document.body.classList.toggle("compact-tabs", compact);
	}, [compact]);
	return (
		<nav className="tabbar" role="tablist" aria-label="Views">
			{TABS.map((t) => {
				const [emoji, word] = TAB_LOOK[t];
				return (
					<button key={t} role="tab" type="button" id={"tab_" + t} data-tab={t} aria-label={word}
						aria-selected={t === shown} data-tip={compact ? word : undefined} onClick={() => showTab(t)}>
						<span className="temo" aria-hidden="true">{emoji}</span> <span className="tword">{word}</span>
					</button>
				);
			})}
		</nav>
	);
}

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

// The tab bar, Settings, Quests and Events follow `tab` (React, the store); the
// `tabShows` (markTabShown: the panel is shown then; a view that throws does
// not stop the others); the ones not yet moved there are told after.
// yet moved there are told.
function showTab(name) {
	name = TAB_ALIASES[name] || name;
	setTab(TABS.includes(name) ? name : "machine");
	for (const t of TABS)
		$("panel_" + t).hidden = t !== tab;
	vmKeys();
	writeHash();
	markTabShown();
	showViews();
}

function showViews() {
	graphShow(tab === "calls");
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

// Compact tabs (Settings tab), remembered in this browser.
const COMPACT_KEY = "mkb-compact-tabs";
// Default on a phone: at 400 px and less the names take three rows.
const compactDefault = () => window.matchMedia("(max-width: 400px)").matches;

function initCompactTabs() {
	let saved = null;
	try {
		saved = localStorage.getItem(COMPACT_KEY);
	} catch (e) {
		// no storage: the default
	}
	setCompactTabs(saved === null ? compactDefault() : saved === "1");
}

// The Settings tab's box: on or off, and remembered.
function chooseCompactTabs(on) {
	setCompactTabs(on);
	try {
		localStorage.setItem(COMPACT_KEY, on ? "1" : "0");
	} catch (e) {
		// no storage: until reload
	}
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
provide({ showTab, writeHash });

export { navHashNow, hashOwn, showTab, writeHash, vmKeys, initCompactTabs, readHash, TABS, compactDefault,
	chooseCompactTabs, setHashOwn, TabBar };
