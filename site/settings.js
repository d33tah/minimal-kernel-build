"use strict";

// The Settings tab: everything the page remembers in this browser, each
// with what it does, a way to change or forget it, and "Reset all
// settings". Preferences (compact tabs, speed, clock, count) have their own
// controls, mirrored from the run bar above the tabs; what is remembered
// for one kernel (imported symbols, command line, watch expressions, the
// measured boot length) is listed from localStorage, so a key a later
// version adds is shown too (as "kept by this page"). Imported DWARF lives
// in IndexedDB (dbginfo.js).
//
// Loaded before app.js; its functions run after app.js has set up.

const SET_PREFIX = "mkb-";
const SET_IDB_ROW = "idb:" + "mkb-dwarf";

function setStoredKeys() {
	try {
		return Object.keys(localStorage).filter((k) => k.startsWith(SET_PREFIX)).sort();
	} catch (e) {
		return [];
	}
}

function setGet(k) {
	try {
		return localStorage.getItem(k);
	} catch (e) {
		return null;
	}
}

function setRemove(k) {
	try {
		localStorage.removeItem(k);
	} catch (e) {
		// no storage: nothing to forget
	}
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
		watchKernel = undefined; // reload the (now empty) list
		watchLoad();
		if (typeof renderWatches === "function")
			renderWatches();
	}
}

async function setIdbCount() {
	if (typeof indexedDB === "undefined")
		return 0;
	return (await dbgIdbOp("readonly", (s) => s.count())) || 0;
}

// The controls show the current values; the list shows what is remembered.
async function renderSettings() {
	$("set_speed").value = prefSpeed;
	$("set_mips").value = $("clock_mips").value;
	$("set_count").checked = $("count").checked;
	const prefs = new Set([...document.querySelectorAll("#panel_settings [data-key]")]
		.filter((e) => !e.closest("#settings_stored")).map((e) => e.dataset.key));
	const rows = setStoredKeys().filter((k) => !prefs.has(k)).map((k) => [k, setDescribe(k)]);
	const n = await setIdbCount();
	if (n)
		rows.push([SET_IDB_ROW, "Decoded DWARF of imported vmlinux files: " + num(n) + (n === 1 ? " entry" : " entries") +
			" in IndexedDB, so file:line works after a reload without the file."]);
	const box = $("settings_stored");
	box.textContent = "";
	if (!rows.length) {
		const p = document.createElement("p");
		p.className = "note";
		p.textContent = "Nothing remembered for any kernel yet.";
		box.append(p);
	}
	for (const [k, text] of rows) {
		const row = document.createElement("div");
		row.className = "setrow";
		const key = document.createElement("code");
		key.className = "setkey";
		key.dataset.key = k;
		key.textContent = k.length > 30 ? k.slice(0, 28) + "…" : k;
		key.title = k;
		const what = document.createElement("span");
		what.className = "note";
		what.textContent = text;
		const b = document.createElement("button");
		b.type = "button";
		b.className = "forget";
		b.textContent = "Forget";
		b.setAttribute("aria-label", "Forget " + k);
		b.addEventListener("click", async () => {
			await setForget(k);
			$("settings_status").textContent = "Forgot " + k + ".";
			renderSettings();
		});
		row.append(key, " ", what, " ", b);
		box.append(row);
	}
}

// A change here is the same as a change above the tabs: that control's own
// handler applies and remembers it.
function setMirror(from, to, prop) {
	$(from).addEventListener("change", () => {
		$(to)[prop] = $(from)[prop];
		$(to).dispatchEvent(new Event("change"));
	});
}

async function resetSettings() {
	// The defaults first (their handlers remember them), then forget all.
	setCompactTabs(compactDefault());
	$("dbg_speed").value = "1000";
	$("dbg_speed").dispatchEvent(new Event("change"));
	$("clock_mips").value = "10";
	$("clock_mips").dispatchEvent(new Event("change"));
	$("count").checked = true;
	$("count").dispatchEvent(new Event("change"));
	const keys = setStoredKeys();
	for (const k of keys)
		await setForget(k);
	await setForget(SET_IDB_ROW);
	for (const k of setStoredKeys())
		setRemove(k);
	$("settings_status").textContent = "All settings reset: " + num(keys.length) + (keys.length === 1 ? " entry" : " entries") + " forgotten.";
	renderSettings();
}

function initSettings() {
	for (const [from, to] of [["dbg_speed", "set_speed"], ["clock_mips", "set_mips"]])
		for (const o of $(from).options)
			$(to).add(new Option(o.textContent, o.value));
	setMirror("set_speed", "dbg_speed", "value");
	setMirror("set_mips", "clock_mips", "value");
	setMirror("set_count", "count", "checked");
	$("settings_reset").addEventListener("click", resetSettings);
}
