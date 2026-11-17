"use strict";

// The Settings tab: everything the page remembers in this browser, each
// with what it does, a way to change or forget it, and "Reset all
// settings". Preferences (compact tabs, speed, clock, count) have their own
// controls, mirrored from the run bar in the Machine tab; what is remembered
// for one kernel (imported symbols, command line, watch expressions, the
// measured boot length) is listed from localStorage, so a key a later
// version adds is shown too (as "kept by this page"). Imported DWARF lives
// in IndexedDB (dbginfo.js).
//
// A React component (R3) on page.js's store: it draws itself again when the
// tab is shown, the speed or compact tabs change, or an entry is forgotten.

import { useReducer, useState, useEffect } from "react";
import { num, $ } from "./util.js";
import { image, setCompactTabs } from "./page.js";
import { dbgIdbOp, dbgForget } from "./dbginfo.js";
import { MAP_KEY, CMDLINE_KEY, cmdlineFromUrl, forgetImported, defaultCmdline } from "./symimport.js";
import { WATCH_KEY, watchKernel, setWatchKernel, watchLoad, renderWatches } from "./vars.js";
import { compactDefault, chooseCompactTabs } from "./tabs.jsx";
import { useStore } from "./ui.jsx";
import { REF_KEY } from "./debugui.js";

const SET_PREFIX = "mkb-";
const SET_IDB_ROW = "idb:" + "mkb-dwarf";
// The keys of the preferences with a control of their own (data-key).
const SET_PREFS = ["mkb-compact-tabs", "mkb-speed", "mkb-mips", "mkb-count"];

function setStoredKeys() {
	try {
		return Object.keys(localStorage).filter((k) => k.startsWith(SET_PREFIX)).sort();
	} catch (e) {
		return [];
	}
}

// setGet and setRemove only see keys setStoredKeys listed: localStorage
// works then (where it throws, nothing is listed).
function setGet(k) {
	return localStorage.getItem(k);
}

function setRemove(k) {
	localStorage.removeItem(k);
}

// The current bzImage's key part (SHA-256), if hashed already.
const setCurrentSha = () => (image && image.sha) || "";

function setWhose(sha) {
	return sha && sha === setCurrentSha() ? "this kernel (" + image.name + ")" :
		"another bzImage (SHA-256 " + sha.slice(0, 12) + "…)";
}

// One remembered key: [what it is, what forgetting it does].
function setDescribe(k) {
	const v = setGet(k) || "";
	if (k.startsWith(MAP_KEY)) {
		let m = {};
		try {
			m = JSON.parse(v);
		} catch (e) {
			m = {};
		}
		const n = Array.isArray(m.symbols) ? m.symbols.length : 0;
		return "Imported symbols for " + setWhose(k.slice(MAP_KEY.length)) + ": " + (m.name || "?") + " (" + (m.source || "?") +
			", " + num(n) + " symbols), used instead of the kernel's own after every reload.";
	}
	if (k.startsWith(CMDLINE_KEY))
		return "Command line for " + setWhose(k.slice(CMDLINE_KEY.length)) + ": \"" + v + "\", used at every Boot of that bzImage.";
	if (k.startsWith(WATCH_KEY)) {
		let w = [];
		try {
			w = JSON.parse(v);
		} catch (e) {
			w = [];
		}
		return "Watch expressions for " + k.slice(WATCH_KEY.length) + " in the Debugger tab: " + (w.join(", ") || "none") + ".";
	}
	if (k.startsWith(REF_KEY)) {
		const rest = k.slice(REF_KEY.length);
		const i = rest.lastIndexOf("-");
		const clock = rest.slice(i + 1);
		return "Instructions from the kernel entry to Still alive measured for " + rest.slice(0, i) + " with the " +
			(clock === "real" ? "real-time" : clock + "/µs") + " clock: " + num(Number(v)) + ". The ETA while counting uses it.";
	}
	return k + ": \"" + (v.length > 40 ? v.slice(0, 40) + "…" : v) + "\". Kept by this page (no description yet).";
}

// Forget one key, with its effect on what is shown now.
async function setForget(k) {
	if (k === SET_IDB_ROW) {
		await dbgIdbOp("readwrite", (s) => { s.clear(); return null; });
		return;
	}
	const cur = setCurrentSha();
	if (k.startsWith(MAP_KEY)) {
		if (cur && k === MAP_KEY + cur) {
			await forgetImported(); // the kernel's own symbols, reboots
			return;
		}
		setRemove(k);
		await dbgForget(k);
		return;
	}
	setRemove(k);
	if (k.startsWith(CMDLINE_KEY) && cur && k === CMDLINE_KEY + cur && cmdlineFromUrl === null) {
		delete $("cmdline").dataset.typed;
		$("cmdline").value = defaultCmdline(); // for the next Boot
	}
	if (k.startsWith(WATCH_KEY) && k === WATCH_KEY + watchKernel) {
		setWatchKernel(undefined); // reload the (now empty) list
		watchLoad();
		renderWatches();
	}
}

async function setIdbCount() {
	if (typeof indexedDB === "undefined")
		return 0;
	return (await dbgIdbOp("readonly", (s) => s.count())) || 0;
}

// A change here is the same as a change in the Machine tab: that control's own
// handler applies and remembers it.
function setMirror(to, prop, v) {
	$(to)[prop] = v;
	$(to).dispatchEvent(new Event("change"));
}

// Back to the defaults (their handlers remember them), then forget all;
// returns how many entries were forgotten.
async function resetSettings() {
	setCompactTabs(compactDefault());
	setMirror("dbg_speed", "value", "1000");
	setMirror("clock_mips", "value", "10");
	setMirror("count", "checked", true);
	const keys = setStoredKeys();
	for (const k of keys)
		await setForget(k);
	await setForget(SET_IDB_ROW);
	for (const k of setStoredKeys())
		setRemove(k);
	return keys.length;
}

// The options of a select of the Machine tab (speed, clock), as [label, value].
const setOptionsOf = (id) => [...$(id).options].map((o) => [o.textContent, o.value]);

// One remembered entry with its Forget button.
function SetRow({ k, text, onForget }) {
	return (
		<div className="setrow">
			<code className="setkey" data-key={k} title={k}>{k.length > 30 ? k.slice(0, 28) + "…" : k}</code>
			{" "}<span className="note">{text}</span>{" "}
			<button type="button" className="forget" aria-label={"Forget " + k} onClick={() => onForget(k)}>Forget</button>
		</div>
	);
}

// What is remembered, as it is when drawn: a drawing is mounted each time
// the tab is shown or an entry is forgotten. The localStorage rows come at
// once; the IndexedDB row joins when its count arrives (that can take a
// while), unless a newer drawing started.
function SetStored({ onForget }) {
	const [keys] = useState(() => setStoredKeys().filter((k) => !SET_PREFS.includes(k)));
	const [idb, setIdb] = useState(0);
	useEffect(() => {
		let mine = true;
		setIdbCount().then((n) => {
			if (mine)
				setIdb(n);
		});
		return () => { mine = false; };
	}, []);
	const rows = keys.map((k) => <SetRow key={k} k={k} text={setDescribe(k)} onForget={onForget} />);
	if (idb)
		rows.push(<SetRow key={SET_IDB_ROW} k={SET_IDB_ROW} onForget={onForget}
			text={"Decoded DWARF of imported vmlinux files: " + num(idb) + (idb === 1 ? " entry" : " entries") +
				" in IndexedDB, so file:line works after a reload without the file."} />);
	return rows.length ? rows : <p className="note">Nothing remembered for any kernel yet.</p>;
}

function SettingsPanel() {
	const shown = useStore("tab") === "settings";
	const compact = useStore("compactTabs");
	const speed = useStore("prefSpeed");
	// A new drawing of the stored entries (Forget, Reset); the clock and
	// count controls read the Machine tab's when drawn.
	const [drawing, redraw] = useReducer((n) => n + 1, 0);
	const [status, setStatus] = useState("");
	const [speeds] = useState(() => setOptionsOf("dbg_speed"));
	const [clocks] = useState(() => setOptionsOf("clock_mips"));
	const forget = async (k) => {
		await setForget(k);
		setStatus("Forgot " + k + ".");
		redraw();
	};
	const reset = async () => {
		const n = await resetSettings();
		setStatus("All settings reset: " + num(n) + (n === 1 ? " entry" : " entries") + " forgotten.");
		redraw();
	};
	const mirror = (to, prop, v) => {
		setMirror(to, prop, v);
		redraw();
	};
	return (
		<>
			<p className="note">What this page remembers in this browser (localStorage, and IndexedDB for imported DWARF). Nothing is sent anywhere. A change takes effect at once.</p>
			<h3>Tabs</h3>
			<div className="setrow">
				<label><input type="checkbox" id="compact_tabs" data-key="mkb-compact-tabs" checked={compact}
					onChange={(e) => chooseCompactTabs(e.target.checked)} /> Compact tabs: emoji only</label>
				{" "}<span className="note">The tab buttons show only their emoji; hover, focus or a long press shows the name. On by default on screens up to 400 px wide, where the names take three rows.</span>
			</div>
			<h3>Running</h3>
			<div className="setrow">
				<label>Speed after the kernel entry <select id="set_speed" data-key="mkb-speed" value={speed}
					onChange={(e) => mirror("dbg_speed", "value", e.target.value)}>
					{speeds.map(([label, v]) => <option key={v} value={v}>{label}</option>)}
				</select></label>
				{" "}<span className="note">The speed in the Machine tab: BIOS and setup are stepped as fast as possible, then the kernel runs at this speed. Changing it there changes it here. ?speed= in the URL overrides it for one visit.</span>
			</div>
			<div className="setrow">
				<label>Clock <select id="set_mips" data-key="mkb-mips" value={$("clock_mips").value}
					onChange={(e) => mirror("clock_mips", "value", e.target.value)}>
					{clocks.map(([label, v]) => <option key={v} value={v}>{label}</option>)}
				</select></label>
				{" "}<span className="note">Guest time while the page steps the CPU: a virtual clock of this many instructions per microsecond, or real time (the clock in the Machine tab). ?mips= in the URL overrides it.</span>
			</div>
			<div className="setrow">
				<label><input type="checkbox" id="set_count" data-key="mkb-count" checked={$("count").checked}
					onChange={(e) => mirror("count", "checked", e.target.checked)} /> Count the boot at full speed</label>
				{" "}<span className="note">The count box in the Machine tab: at full speed, first step the whole boot in the background, counting, then run the JIT. ?count=0 in the URL turns it off for one visit.</span>
			</div>
			<h3>Remembered for each kernel</h3>
			<div id="settings_stored">{shown && <SetStored key={drawing} onForget={forget} />}</div>
			<h3>Reset</h3>
			<div className="setrow">
				<button id="settings_reset" type="button" onClick={reset}>Reset all settings</button>
				{" "}<span className="note">Forgets everything on this tab and goes back to the defaults. The offline copy of the site (service worker cache) stays.</span>
				{" "}<span id="settings_status" className="note" role="status">{status}</span>
			</div>
		</>
	);
}

export { SettingsPanel };
