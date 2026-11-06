"use strict";

// Variables from the kernel's DWARF (dbginfo.js dbgTypes, dwarf.js
// DwarfTypes data): the Debugger's Variables panel (watch expressions
// pretty-printed from guest RAM, pointers open the Page tab at their
// target) and the Page listing's hex rows (the byte under the pointer or
// the tap named as variable + member path, e.g. init_task.comm[3]).
// Values are read from guest RAM when drawn: kernel virtual addresses are
// physical + the kernel's offset (0xC0000000 for i386 Linux).

const WATCH_KEY = "mkb-watch:";
// Shown as one-tap suggestions when the kernel has them.
const WATCH_SUGGEST = ["init_task", "jiffies_64", "linux_banner", "system_state", "current_task", "init_uts_ns"];
// Elements of an array shown at once.
const WATCH_MAX_ELEMENTS = 64;
// Bytes of a char * target shown as a string.
const WATCH_MAX_STRING = 96;

let watchList = [];
const watchOpen = new Set(); // "expression/path" of expanded rows
let watchKernel = null;

const varsReady = () => typeof emulator !== "undefined" && emulator && emuReady;

// Virtual - physical of the kernel's data (0 for hello).
const varsOffset = () => virtOffset(V86_KERNEL_ADDR);

// Guest bytes at physical pa, or null when there is no RAM there yet.
function varsRead(pa, n) {
	if (!varsReady() || pa < 0 || n < 0 || pa + n > emuRam)
		return null;
	return emulator.read_memory(pa, n);
}

// Whether the kernel's own code has surely not run yet: paused with EIP
// outside its symbols (BIOS, setup, decompressor) and its C entry never
// stepped. Its variables then hold whatever RAM holds there.
function varsNotStarted() {
	const eip = typeof currentPhys === "function" ? currentPhys() : -1;
	if (!stepper || !symbols || eip < 0)
		return false;
	const entry = symbols.symbols.find((s) => s.name === "start_kernel" || s.name === "hello_main");
	return !!entry && !stepper.executed(entry.paddr) && stepper.symbolOf(eip) < 0;
}
const VARS_NOT_STARTED = "The kernel's own code has not run yet (EIP is in the BIOS, setup or the decompressor): " +
	"until it is in place its variables hold whatever RAM holds there.";

// name -> [variable, ...] of T, made once.
function varsByName(T) {
	if (!T.byName) {
		T.byName = new Map();
		for (const v of T.vars) {
			const l = T.byName.get(v[0]);
			if (l)
				l.push(v);
			else
				T.byName.set(v[0], [v]);
		}
	}
	return T.byName;
}

const varsSection = (T, v) => T.sections[v[4]] || "?";

// How a variable's address relates to what the kernel uses, for notes.
function varsNote(T, v) {
	const sec = varsSection(T, v);
	const out = [];
	if (v[5] & 4)
		out.push("static local" + (v[6] ? " of " + v[6] + "()" : ""));
	if (/^\.init\./.test(sec))
		out.push("in " + sec + ": freed once the kernel has booted (free_initmem), then the memory is reused");
	if (v[5] & 2)
		out.push("per-CPU variable");
	return out;
}

// Member name of struct/union s, also inside anonymous members: [offset, member] or null.
function varsMember(T, sid, name) {
	const t = T.types[sid];
	if (!t || (t[0] !== DWT.struct && t[0] !== DWT.union) || !t[3])
		return null;
	for (const m of t[3]) {
		if (m[0] === name)
			return [m[1], m];
		if (!m[0]) {
			const r = varsMember(T, dwStrip(T, m[2]), name);
			if (r)
				return [m[1] + r[0], r[1]];
		}
	}
	return null;
}

// Physical address of a per-CPU variable's CPU 0 copy: the template's
// address + __per_cpu_offset[0] once setup_per_cpu_areas() has run.
function varsPerCpu(T, pa) {
	const off = (varsByName(T).get("__per_cpu_offset") || [])[0];
	const b = off ? varsRead(off[1], 4) : null;
	const d = b ? (b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24) >>> 0 : 0;
	return d ? { pa: (pa + d) >>> 0, how: "CPU 0's copy (template + __per_cpu_offset[0] = " + hex(d) + ")" } :
		{ pa, how: "the template in .data..percpu: the per-CPU copies are not set up yet (__per_cpu_offset[0] is 0)" };
}

// A watch expression: name, Function::name, then .member, ->member, [index];
// a leading * dereferences the result. { pa, type, bits?, notes } or { error }.
function varsEval(T, expr) {
	const m = /^\s*(\*?)\s*(?:([A-Za-z_]\w*)::)?([A-Za-z_]\w*)\s*(.*)$/.exec(expr);
	if (!m)
		return { error: "not a variable name: " + expr };
	let list = varsByName(T).get(m[3]) || [];
	if (m[2])
		list = list.filter((v) => v[6] === m[2]);
	if (!list.length)
		return { error: "no variable " + (m[2] ? m[2] + "::" : "") + m[3] + " in this kernel's DWARF" };
	const v = list.find((x) => x[5] & 1) || list[0];
	const notes = varsNote(T, v);
	if (list.length > 1)
		notes.push(list.length + " variables have this name: this is the one in " + varsSection(T, v) + " at " +
			hex(v[1] + varsOffset()) + "; Function::name picks a static local");
	let pa = v[1], type = v[2], bits = null;
	if (v[5] & 2) {
		const c = varsPerCpu(T, pa);
		pa = c.pa;
		notes.push(c.how);
	}
	const deref = () => {
		const s = dwStrip(T, type);
		if (s < 0 || T.types[s][0] !== DWT.ptr)
			throw new Error("not a pointer");
		const b = varsRead(pa, 4);
		if (!b)
			throw new Error("the pointer is not in RAM");
		const va = (b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24) >>> 0;
		const to = va - varsOffset();
		if (!va || to < 0 || to >= emuRam)
			throw new Error("the pointer (" + hex(va) + ") is not a kernel address in RAM");
		pa = to;
		type = T.types[s][1];
	};
	const re = /\s*(?:\.(\w+)|->(\w+)|\[(\d+)\])/y;
	let rest = m[4], p = 0;
	try {
		while (p < rest.length) {
			re.lastIndex = p;
			const t = re.exec(rest);
			if (!t)
				throw new Error("cannot read “" + rest.slice(p) + "” (use .member, ->member, [index])");
			p = re.lastIndex;
			if (bits)
				throw new Error("a bit field has no members");
			if (t[1] || t[2]) {
				if (t[2])
					deref();
				const r = varsMember(T, dwStrip(T, type), t[1] || t[2]);
				if (!r)
					throw new Error(dwTypeName(T, type) + " has no member " + (t[1] || t[2]));
				pa += r[0];
				type = r[1][2];
				if (r[1].length > 3)
					bits = [r[1][3], r[1][4]];
			} else {
				const i = Number(t[3]);
				const s = dwStrip(T, type);
				if (s >= 0 && T.types[s][0] === DWT.array) {
					const n = T.types[s][2][0];
					if (n >= 0 && i >= n)
						notes.push("index " + i + " is past the end (" + n + " elements)");
					const inner = T.types[s][2].slice(1);
					const es = inner.reduce((x, k) => x * k, dwTypeSize(T, T.types[s][1]));
					pa += i * es;
					type = inner.length ? varsSubArray(T, T.types[s][1], inner) : T.types[s][1];
				} else {
					deref();
					pa += i * dwTypeSize(T, type);
				}
			}
		}
		if (m[1])
			deref();
	} catch (e) {
		return { error: e.message };
	}
	return { pa, type, bits, notes, v };
}

// The type id of "elem [dims]" (a row of a multi-dimensional array),
// added to T once.
function varsSubArray(T, elem, dims) {
	const k = elem + ":" + dims.join(",");
	T.sub = T.sub || new Map();
	if (!T.sub.has(k)) {
		T.sub.set(k, T.types.length);
		T.types.push([DWT.array, elem, dims]);
	}
	return T.sub.get(k);
}

const le = (b, n) => { let x = 0n; for (let k = n - 1; k >= 0; k--) x = x << 8n | BigInt(b[k]); return x; };

// What a pointer value points at, for its label: a variable (+offset) or a function.
function varsPointee(T, va) {
	const pa = va - varsOffset();
	const w = va && pa >= 0 ? dwWhere(T, pa) : null;
	// At a variable's start: its name (its first member is there too).
	if (w)
		return pa === w.var[1] ? w.var[0] : w.path + (w.offset ? "+" + w.offset : "");
	const s = symbols && pa >= 0 ? symbolAt(symbols.symbols, pa) : null;
	return s ? s.name + (pa > s.paddr ? "+0x" + (pa - s.paddr).toString(16) : "") : "";
}

// A C string from guest RAM at virtual va, or null.
function varsString(va, max) {
	const pa = va - varsOffset();
	const b = va ? varsRead(pa, Math.min(max, Math.max(0, emuRam - pa))) : null;
	if (!b)
		return null;
	let e = b.indexOf(0);
	const cut = e < 0;
	if (cut)
		e = b.length;
	return JSON.stringify(String.fromCharCode(...b.subarray(0, e))) + (cut ? "…" : "");
}

// Value of a scalar, enum or pointer at pa: { text, ptr (virtual) } or null
// (aggregates, or no RAM).
function varsScalar(T, type, pa, bits) {
	const s = dwStrip(T, type);
	const t = s >= 0 ? T.types[s] : null;
	if (!t || t[0] === DWT.func || t[0] === DWT.void)
		return { text: "" };
	if (t[0] === DWT.struct || t[0] === DWT.union || t[0] === DWT.array)
		return null;
	const size = t[2] > 0 ? t[2] : 4;
	const b = varsRead(pa, bits ? Math.ceil((bits[1] + bits[0]) / 8) : size);
	if (!b)
		return { text: "(not in RAM)" };
	let raw = bits ? (le(b, b.length) >> BigInt(bits[1])) & ((1n << BigInt(bits[0])) - 1n) : le(b, size);
	const width = bits ? bits[0] : size * 8;
	const signed = t[0] === DWT.base && (t[3] === 5 || t[3] === 6) || (t[0] === DWT.enum && t[4] >= 0 &&
		[5, 6].includes((T.types[dwStrip(T, t[4])] || [])[3]));
	const sval = signed && raw >> BigInt(width - 1) ? raw - (1n << BigInt(width)) : raw;
	if (t[0] === DWT.ptr) {
		const va = Number(raw);
		if (!va)
			return { text: "NULL" };
		const to = t[1] >= 0 ? T.types[dwStrip(T, t[1])] : null;
		const str = to && to[0] === DWT.base && to[2] === 1 ? varsString(va, WATCH_MAX_STRING) : null;
		const what = varsPointee(T, va);
		return { text: hex(va) + (what ? " <" + what + ">" : "") + (str ? " " + str : ""), ptr: va };
	}
	if (t[0] === DWT.enum) {
		const mod = 1n << BigInt(size * 8);
		const e = (t[3] || []).find(([, x]) => ((BigInt(x) % mod) + mod) % mod === raw);
		return { text: e ? e[0] + " (" + sval + ")" : String(sval) + " (no enumerator of " + dwTypeName(T, s) + ")" };
	}
	if (t[3] === 2)
		return { text: raw ? "true" : "false" };
	if (t[3] === 4 && !bits) {
		const dv = new DataView(Uint8Array.from(b).buffer);
		return { text: String(size === 4 ? dv.getFloat32(0, true) : size === 8 ? dv.getFloat64(0, true) : "?") };
	}
	if ((t[3] === 6 || t[3] === 8) && size === 1) {
		const c = Number(raw);
		return { text: sval + (c >= 0x20 && c < 0x7f ? " '" + String.fromCharCode(c) + "'" : "") };
	}
	return { text: String(sval) + (raw > 9n ? " (0x" + raw.toString(16) + ")" : "") };
}

// Children of an aggregate: [[label, pa, type, bits]] and a note.
function varsChildren(T, type, pa) {
	const s = dwStrip(T, type);
	const t = s >= 0 ? T.types[s] : null;
	if (!t)
		return { list: [] };
	if (t[0] === DWT.struct || t[0] === DWT.union) {
		if (!t[3])
			return { list: [], note: "only declared: this kernel's DWARF has no members for " + dwTypeName(T, s) };
		return { list: t[3].map((m) => [m[0] ? "." + m[0] : "(anonymous " + dwTypeName(T, m[2]).split(" ")[0] + ")", pa + m[1], m[2],
			m.length > 3 ? [m[3], m[4]] : null]) };
	}
	if (t[0] === DWT.array) {
		const n = t[2][0];
		const inner = t[2].slice(1);
		const etype = inner.length ? varsSubArray(T, t[1], inner) : t[1];
		const es = dwTypeSize(T, etype);
		const shown = n < 0 ? 0 : Math.min(n, WATCH_MAX_ELEMENTS);
		const list = [];
		for (let i = 0; i < shown; i++)
			list.push(["[" + i + "]", pa + i * es, etype, null]);
		return { list, note: n < 0 ? "flexible array: its length is not in the type" :
			n > shown ? "first " + shown + " of " + n + " elements: watch name[i] for others" : "" };
	}
	return { list: [] };
}

// The one-line value of an aggregate: a char array as a string, else {…}.
function varsSummary(T, type, pa) {
	const s = dwStrip(T, type);
	const t = T.types[s];
	if (t[0] === DWT.array) {
		const et = T.types[dwStrip(T, t[1])];
		if (et && et[0] === DWT.base && et[2] === 1 && t[2].length === 1 && t[2][0] > 0) {
			const b = varsRead(pa, t[2][0]);
			if (!b)
				return "(not in RAM)";
			const e = b.indexOf(0);
			return JSON.stringify(String.fromCharCode(...b.subarray(0, e < 0 ? b.length : e)));
		}
		return "[" + (t[2][0] >= 0 ? t[2][0] : "?") + "]";
	}
	return "{…}";
}

// One row (and, open, its children) of the Variables tree.
function varsRow(T, label, type, pa, bits, key, depth) {
	const head = document.createElement("span");
	head.className = "wrow";
	const name = document.createElement("span");
	name.className = "wname";
	name.textContent = label;
	const val = document.createElement("span");
	val.className = "wval";
	const ty = document.createElement("span");
	ty.className = "wtype";
	ty.textContent = dwTypeName(T, type) + (bits ? " : " + bits[0] : "");
	const at = pa + varsOffset();
	ty.title = "at " + hex(at) + (bits ? " bits " + bits[1] + "–" + (bits[1] + bits[0] - 1) : "") + ", physical " + hex(pa) +
		", " + dwTypeSize(T, type) + " bytes";
	head.append(name, " = ", val, " ", ty);
	const sc = varsScalar(T, type, pa, bits);
	if (sc) {
		val.textContent = sc.text;
		if (sc.ptr !== undefined) {
			const to = sc.ptr - varsOffset();
			if (to >= 0 && to < emuRam) {
				val.textContent = "";
				const b = symButton(sc.text, to, "Open the Page tab at " + hex(sc.ptr) + " (physical " + hex(to) + ")");
				b.classList.add("wptr");
				val.append(b);
			}
		}
		const d = document.createElement("div");
		d.className = "wleaf";
		d.dataset.path = key;
		d.append(head);
		return d;
	}
	val.textContent = varsSummary(T, type, pa);
	const det = document.createElement("details");
	det.dataset.path = key;
	const sum = document.createElement("summary");
	sum.append(head);
	det.append(sum);
	const fill = () => {
		if (det.dataset.filled)
			return;
		det.dataset.filled = "1";
		const ch = varsChildren(T, type, pa);
		const box = document.createElement("div");
		box.className = "wkids";
		if (depth > 24)
			box.textContent = "(nested too deep)";
		else
			for (const [l, cpa, ct, cb] of ch.list)
				box.append(varsRow(T, l, ct, cpa, cb, key + l, depth + 1));
		if (ch.note) {
			const n = document.createElement("div");
			n.className = "note";
			n.textContent = ch.note;
			box.append(n);
		}
		det.append(box);
	};
	if (watchOpen.has(key)) {
		det.open = true;
		fill();
	}
	det.addEventListener("toggle", () => {
		if (det.open) {
			watchOpen.add(key);
			fill();
		} else
			watchOpen.delete(key);
	});
	return det;
}

function watchSave() {
	try {
		localStorage.setItem(WATCH_KEY + watchKernel, JSON.stringify(watchList));
	} catch (e) {
		// no storage: the list lasts until the page is closed
	}
}

// The watch list of the current kernel (by name), remembered in this browser.
function watchLoad() {
	const k = image ? image.name : null;
	if (k === watchKernel)
		return;
	watchKernel = k;
	watchList = [];
	try {
		watchList = JSON.parse(localStorage.getItem(WATCH_KEY + k) || "[]");
	} catch (e) {
		watchList = [];
	}
}

function watchAdd(expr) {
	expr = String(expr || "").trim();
	if (!expr)
		return;
	watchLoad();
	if (!watchList.includes(expr)) {
		watchList.unshift(expr);
		watchSave();
	}
	renderWatches();
}

// The Debugger's Variables section.
function renderWatches() {
	const box = $("watches");
	if (!box || tab !== "debugger")
		return;
	watchLoad();
	const st = $("watch_status");
	const r = dbgTypes();
	box.dataset.state = r.state;
	$("watch_quick").textContent = "";
	if (r.state !== "ok") {
		st.textContent = r.why;
		box.textContent = "";
		return;
	}
	const T = r.T;
	const names = varsByName(T);
	st.textContent = T.vars.length + " variables and " + (T.types.length - (T.sub ? T.sub.size : 0)) + " types from the DWARF. " +
		(varsReady() ? "Values read from guest RAM " + (runState === "jit" ? "while it runs (Refresh, or pause)" : "now") + "." :
			"Boot the kernel to read values.") + (varsNotStarted() ? " " + VARS_NOT_STARTED : "");
	box.dataset.started = varsNotStarted() ? "no" : "";
	for (const n of WATCH_SUGGEST)
		if (names.has(n) && !watchList.includes(n)) {
			const b = document.createElement("button");
			b.type = "button";
			b.textContent = "+ " + n;
			b.title = "Watch " + n;
			b.addEventListener("click", () => watchAdd(n));
			$("watch_quick").append(b);
		}
	box.textContent = "";
	watchList.forEach((expr, i) => {
		const w = document.createElement("div");
		w.className = "watch";
		w.dataset.expr = expr;
		const x = document.createElement("button");
		x.type = "button";
		x.className = "wdel";
		x.textContent = "×";
		x.title = "Stop watching " + expr;
		x.setAttribute("aria-label", "stop watching " + expr);
		x.addEventListener("click", () => {
			watchList.splice(i, 1);
			watchSave();
			renderWatches();
		});
		const e = varsEval(T, expr);
		if (e.error) {
			const s = document.createElement("span");
			s.className = "werr";
			s.textContent = expr + ": " + e.error;
			w.dataset.state = "error";
			w.append(x, s);
		} else {
			w.dataset.state = "ok";
			w.dataset.addr = e.pa;
			w.append(x, varsRow(T, expr.trim(), e.type, e.pa, e.bits, expr + "/", 0));
			for (const n of e.notes) {
				const d = document.createElement("div");
				d.className = "note";
				d.textContent = n;
				w.append(d);
			}
		}
		box.append(w);
	});
	if (!watchList.length)
		box.textContent = "No watches: type a variable (init_task.comm, jiffies_64, ...) or tap a byte in the Page tab's hex rows.";
}

// Names for the input's suggestions: those starting with what is typed.
function watchSuggest() {
	const r = dbgTypes();
	const dl = $("watch_names");
	dl.textContent = "";
	const q = $("watch_expr").value.trim();
	if (r.state !== "ok" || q.length < 2 || /[.\-[]/.test(q))
		return;
	let n = 0;
	for (const name of varsByName(r.T).keys())
		if (name.startsWith(q)) {
			const o = document.createElement("option");
			o.value = name;
			dl.append(o);
			if (++n >= 40)
				break;
		}
}

// ---- the Page listing: which variable a byte belongs to ---------------------

// Physical address of the byte under a pointer event in a hex row, or -1.
function listingByte(ev) {
	const line = ev.target.closest && ev.target.closest("#listing .hex");
	if (!line)
		return -1;
	const rect = line.getBoundingClientRect();
	const text = line.textContent;
	const col = Math.floor((ev.clientX - rect.left) / (rect.width / text.length));
	const per = Number(line.dataset.per), row = Number(line.dataset.row);
	const lo = Number(line.dataset.addr), end = Number(line.dataset.end);
	let i = -1;
	for (let k = 0; k < per; k++) {
		const c = 9 + k * 3 + (per === 16 && k >= 8 ? 1 : 0);
		if (col >= c && col < c + 2)
			i = k;
	}
	const asc = 9 + per * 3 - 1 + (per === 16 ? 1 : 0) + 2;
	if (i < 0 && col >= asc && col < asc + per)
		i = col - asc;
	const b = row + i;
	return i >= 0 && b >= lo && b < end ? b : -1;
}

// "init_task.comm[3]: char = 115 's' in init_task (struct task_struct, ...)".
function varInfoAt(pa) {
	const r = dbgTypes();
	if (r.state !== "ok")
		return { state: r.state, text: hex(pa) + ": " + r.why };
	const T = r.T;
	const w = dwWhere(T, pa);
	if (!w)
		return { state: "none", text: hex(pa) + " (" + hex(pa + varsOffset()) + "): no variable of the DWARF here (heap, stack, " +
			"page tables, code, or data without debug info)" };
	const v = w.var;
	const sc = w.note.startsWith("bit") || w.note === "padding" ? null : varsScalar(T, w.type, w.leafAddr, null);
	let text = hex(pa + varsOffset()) + " " + w.path + (w.offset ? " +" + w.offset : "") +
		(w.note ? " (" + w.note + ")" : "") + ": " + dwTypeName(T, w.type) + (sc && sc.text ? " = " + sc.text : "");
	if (w.unions.length)
		text += "; the same bytes are also " + w.unions.join(", ") + " (union)";
	text += " — in " + v[0] + ": " + dwTypeName(T, v[2]) + ", " + v[3] + " bytes, " + varsSection(T, v) +
		varsNote(T, v).map((n) => "; " + n).join("");
	if (v[5] & 2)
		text += " (this is the template; the Variables panel shows CPU 0's copy)";
	if (varsNotStarted())
		text += ". " + VARS_NOT_STARTED;
	return { state: "ok", text, path: w.path, note: w.note };
}

let varPinned = -1;

function showVarInfo(pa, pinned) {
	const el = $("var_info");
	el.textContent = "";
	el.dataset.state = "";
	if (pa < 0)
		return;
	const r = varInfoAt(pa);
	el.dataset.state = r.state;
	el.dataset.path = r.path || "";
	el.dataset.addr = pa;
	el.append(r.text);
	if (pinned && r.state === "ok") {
		const b = document.createElement("button");
		b.type = "button";
		b.id = "var_watch";
		b.textContent = "Watch " + r.path;
		b.addEventListener("click", () => {
			watchAdd(r.path);
			showTab("debugger");
		});
		el.append(" ", b);
	}
}

// The DWARF type of a data symbol for its label line in the listing ("" if
// unknown or not loaded); starts the load of the types.
function varTypeOf(sym) {
	if (/^\.text|^\.init\.text|^\.head\.text/.test(sym.section || "") || !dbgHas())
		return "";
	const r = dbgTypes();
	if (r.state !== "ok")
		return "";
	const v = (varsByName(r.T).get(sym.name) || []).find((x) => x[1] === sym.paddr);
	return v ? dwTypeName(r.T, v[2]) : "";
}

function setupVars() {
	const listing = $("listing");
	listing.addEventListener("mousemove", (ev) => {
		if (varPinned >= 0)
			return;
		showVarInfo(listingByte(ev), false);
	});
	listing.addEventListener("mouseleave", () => {
		if (varPinned < 0)
			showVarInfo(-1);
	});
	listing.addEventListener("click", (ev) => {
		const b = listingByte(ev);
		if (b < 0) {
			if (ev.target.closest && ev.target.closest("#listing .hex")) {
				varPinned = -1;
				showVarInfo(-1);
			}
			return;
		}
		varPinned = b === varPinned ? -1 : b;
		showVarInfo(b, varPinned >= 0);
	});
	$("watch_add").addEventListener("click", () => {
		watchAdd($("watch_expr").value);
		$("watch_expr").value = "";
	});
	$("watch_expr").addEventListener("keydown", (ev) => {
		if (ev.key === "Enter")
			$("watch_add").click();
	});
	$("watch_expr").addEventListener("input", watchSuggest);
	$("watch_refresh").addEventListener("click", renderWatches);
	dbgOnChange(() => {
		renderWatches();
		if (varPinned >= 0 || $("var_info").dataset.addr)
			showVarInfo(Number($("var_info").dataset.addr), varPinned >= 0);
	});
}
