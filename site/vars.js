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

// Values that are not in RAM (a local in a register, a computed or
// constant value, pieces of these) live in cells at made-up "physical"
// addresses from VARS_CELLS on, so the one render path (varsRow and what
// it calls) shows them like memory. Remade at every Debugger render.
const VARS_CELLS = 2 ** 40, VARS_CELL_SIZE = 2 ** 20;
let varsCells = [];

// A cell for bytes (known[k] 0: byte k unknown); how says where they
// are, why why some are not known. Returns its address.
function varsCell(bytes, known, how, why) {
	varsCells.push({ bytes, known, how, why });
	return VARS_CELLS + (varsCells.length - 1) * VARS_CELL_SIZE;
}

function varsCellAt(pa) {
	return pa >= VARS_CELLS ? varsCells[Math.floor((pa - VARS_CELLS) / VARS_CELL_SIZE)] || null : null;
}

// Guest bytes at physical pa, or null when there is no RAM there yet (or
// a cell's bytes are not all known).
function varsRead(pa, n) {
	const c = varsCellAt(pa);
	if (c) {
		const o = pa - VARS_CELLS - varsCells.indexOf(c) * VARS_CELL_SIZE;
		if (o < 0 || o + n > c.bytes.length)
			return null;
		for (let k = 0; k < n; k++)
			if (!c.known[o + k])
				return null;
		return c.bytes.subarray(o, o + n);
	}
	if (!varsReady() || pa < 0 || n < 0 || pa + n > emuRam)
		return null;
	return emulator.read_memory(pa, n);
}

// The text for a value varsRead could not read at pa.
const varsMissing = (pa) => varsCellAt(pa) ? "<optimized out>" : "(not in RAM)";

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
		return { text: varsMissing(pa) };
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
				return varsMissing(pa);
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
	const at = pa + varsOffset(), cell = varsCellAt(pa);
	ty.title = (cell ? cell.how + (cell.why ? "; " + cell.why : "") : "at " + hex(at) + (bits ? " bits " + bits[1] + "–" + (bits[1] + bits[0] - 1) : "") +
		", physical " + hex(pa)) + ", " + dwTypeSize(T, type) + " bytes";
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

// ---- parameters and locals of the call frames -------------------------------
//
// At a pause, every frame the call stack unwound (app.js unwindHere: EIP
// for the innermost, the call instruction for the others) gets the
// parameters and locals the DWARF has there (dbginfo.js dbgLocals: the
// scopes around that address, inlined calls as frames of their own, like
// gdb's backtrace), each read through its location (unwind.js dwLocation:
// a register of that frame, memory at an offset of the frame base, the
// CFA from the call frame information, a computed value, pieces) and
// shown with the Variables panel's rows. What the DWARF or the frame does
// not have says so: optimized out, a register a callee may have changed,
// a value only described at the function's entry.

// "frame index:function" -> whether the user left that frame open (the
// innermost one is open unless closed).
const localsOpen = new Map();

// Bytes at virtual va through the page tables, or null.
function localsReadBytes(va, n) {
	const cpu = stepper && stepper.cpu;
	if (!cpu || n > 4096)
		return null;
	const out = new Uint8Array(n);
	for (let k = 0; k < n;) {
		const w = pageWalk(cpu, (va + k) >>> 0);
		if (w.phys === null)
			return null;
		const run = Math.min(n - k, 4096 - ((va + k) & 0xfff));
		if (w.phys + run > cpu.mem8.length)
			return null;
		out.set(cpu.mem8.subarray(w.phys, w.phys + run), k);
		k += run;
	}
	return out;
}

// The physical address of n bytes at virtual va when they are one run of
// RAM (what varsRow reads), else -1.
function localsPhys(va, n) {
	const cpu = stepper && stepper.cpu;
	const a = cpu ? pageWalk(cpu, va >>> 0).phys : null, b = cpu ? pageWalk(cpu, (va + Math.max(n, 1) - 1) >>> 0).phys : null;
	return a !== null && b !== null && b - a === Math.max(n, 1) - 1 && a + n <= emuRam ? a : -1;
}

// The DWARF expression context of an unwound frame f.
function localsCtx(f, fb) {
	const ctx = {
		reg: (r) => f.regs && f.regs[r] !== undefined && f.regs[r] !== null ? f.regs[r] >>> 0 : undefined,
		readBytes: localsReadBytes,
		read: (a, n) => {
			const b = localsReadBytes(a, n);
			return b ? b.reduceRight((v, x) => v * 256 + x, 0) : undefined;
		},
		cfa: f.cfa === undefined || f.cfa === null ? undefined : f.cfa >>> 0,
	};
	dwSetFrameBase(ctx, fb ? dwLocAt(fb, f.lookup) : null);
	return ctx;
}

// One variable of a frame: { pa, how, why, state } for varsRow (pa < 0:
// nothing to show), state "ok", "partial" (some bytes known) or "out".
function localsValue(T, v, f, ctx) {
	const size = dwTypeSize(T, v[1]) || 4;
	const l = dwLocAt(v[3], f.lookup);
	if (l.why)
		return { pa: -1, state: "out", why: v[2] & 2 ? "optimized out (nothing about it is left in this copy of the function)" : l.why };
	if (l.c !== undefined || l.cb) {
		const b = new Uint8Array(size);
		if (l.cb)
			b.set(l.cb.subarray(0, size));
		else
			for (let k = 0, x = BigInt(l.c); k < size; k++, x >>= 8n)
				b[k] = Number(BigInt.asUintN(8, x));
		return { pa: varsCell(b, new Uint8Array(size).fill(1), "a constant (DW_AT_const_value)", ""), state: "ok", how: "a constant" };
	}
	const r = dwLocation(l.expr, ctx, size);
	if (r.why)
		return { pa: -1, state: "out", why: r.why + (ctx.fbWhy && /frame base/.test(r.why) ? " (" + ctx.fbWhy + ")" : "") };
	if (r.mem !== undefined) {
		const pa = localsPhys(r.mem, size);
		if (pa >= 0)
			return { pa, state: "ok", how: r.how };
		const b = localsReadBytes(r.mem, size);
		if (!b)
			return { pa: -1, state: "out", why: r.how + ": not mapped" };
		return { pa: varsCell(b, new Uint8Array(size).fill(1), r.how, ""), state: "ok", how: r.how };
	}
	const known = r.known.subarray(0, size).every(Boolean);
	return { pa: varsCell(r.bytes, r.known, r.how, r.whys.join("; ")), state: known ? "ok" : "partial", how: r.how,
		why: r.whys.join("; ") };
}

// The unsigned value of a scalar (integer, pointer, enum) at pa as a
// decimal string, null for anything else or bytes not known: what the
// tests compare with gdb.
function localsRaw(T, type, pa) {
	const s = dwStrip(T, type);
	const t = s >= 0 ? T.types[s] : null;
	if (!t || ![DWT.base, DWT.ptr, DWT.enum].includes(t[0]) || !(t[2] > 0 && t[2] <= 8))
		return null;
	const b = varsRead(pa, t[2]);
	return b ? le(b, t[2]).toString() : null;
}

// The variables of one local frame (an inlined call or the function).
function localsGroup(T, lf, k, f, ctx, key, where) {
	const box = document.createElement("div");
	box.className = "lscope";
	box.dataset.func = lf.func || "";
	box.dataset.inlined = lf.inlined ? "1" : "";
	const head = document.createElement("div");
	head.className = "lhead";
	head.append((lf.func || "?") + "()" + (lf.inlined ? " inlined" : ""));
	if (where)
		head.append(" ", srcLocEl(where));
	box.append(head);
	const list = [...lf.params.map((v) => [v, "param"]), ...lf.locals.map((v) => [v, "local"])];
	for (const [v, kind] of list) {
		const d = document.createElement("div");
		d.className = "lvar";
		d.dataset.name = v[0];
		d.dataset.kind = kind;
		const x = localsValue(T, v, f, ctx);
		d.dataset.state = x.state;
		if (x.pa < 0) {
			const r = document.createElement("span");
			r.className = "wrow";
			const n = document.createElement("span");
			n.className = "wname";
			n.textContent = v[0];
			const ty = document.createElement("span");
			ty.className = "wtype";
			ty.textContent = dwTypeName(T, v[1]);
			const val = document.createElement("span");
			val.className = "wval lout";
			val.textContent = "<optimized out>";
			r.append(n, " = ", val, " ", ty);
			const leaf = document.createElement("div");
			leaf.className = "wleaf";
			leaf.append(r);
			d.append(leaf);
			d.dataset.value = "";
		} else {
			d.append(varsRow(T, v[0], v[1], x.pa, null, key + "/" + k + "/" + v[0], 0));
			const val = d.querySelector(".wval");
			d.dataset.value = val ? val.textContent : "";
			const raw = localsRaw(T, v[1], x.pa);
			if (raw !== null)
				d.dataset.raw = raw;
		}
		const loc = document.createElement("span");
		loc.className = "lloc";
		loc.textContent = (kind === "param" ? "parameter, " : "") + (x.state === "out" ? x.why : x.how + (x.why ? "; " + x.why : ""));
		(d.querySelector(".wrow") || d).append(" ", loc);
		box.append(d);
	}
	if (!list.length) {
		const n = document.createElement("div");
		n.className = "note";
		n.textContent = "no parameters or locals here";
		box.append(n);
	}
	return box;
}

// The Debugger's "Parameters and locals" section for the unwound frames
// uw (unwindHere), or the reason there are none.
function renderLocals(uw) {
	const box = $("locals"), st = $("locals_status");
	if (!box)
		return;
	varsCells = [];
	box.textContent = "";
	if (!uw) {
		st.textContent = runState === "jit" ? "Pause to see the parameters and locals of the call frames." : "";
		return;
	}
	if (!dbgHas()) {
		st.textContent = "No parameters or locals: " + DBG.why + ".";
		return;
	}
	const frames = uw.frames.filter((f) => !f.user);
	st.textContent = frames.length ? "From the DWARF at each frame's EIP (the call instruction for callers), read from its registers and stack" +
		(uw.state !== "ok" ? "; frames below the last unwound one have no registers, so their variables cannot be read" : "") + "." :
		"No call frames unwound here" + (uw.why ? " (" + uw.why + ")" : "") + ".";
	frames.forEach((f, n) => {
		const det = document.createElement("details");
		det.className = "lframe";
		det.dataset.n = n;
		det.dataset.pc = f.lookup;
		const name = stepper ? stepper.symName[stepper.symbolOf(insnStart(f.lookup))] || "" : "";
		const k = String(n);
		const sum = document.createElement("summary");
		const m = FRAME_METHODS[f.method] || ["?"];
		sum.append("#" + n + " " + (name || hex(f.lookup)) + " ");
		const tag = document.createElement("span");
		tag.className = "fmeth";
		tag.textContent = m[0];
		if (m[1])
			tag.title = "How this frame was found: " + m[1];
		sum.append(tag);
		det.append(sum);
		const fill = () => {
			if (det.dataset.filled)
				return;
			const r = dbgLocals(f.lookup);
			det.dataset.state = r.state;
			if (r.state !== "ok") {
				const d = document.createElement("div");
				d.className = "note";
				d.textContent = r.why;
				det.append(d);
				return; // filled again when the data arrives (dbgOnChange re-renders)
			}
			det.dataset.filled = "1";
			if (!r.frames.length) {
				const d = document.createElement("div");
				d.className = "note";
				d.textContent = "no DWARF function here (assembly, or code without debug info)";
				det.append(d);
				return;
			}
			const lines = dbgFrames(f.lookup);
			const ctx = localsCtx(f, r.frames[r.frames.length - 1].fb);
			r.frames.forEach((lf, i) => {
				const fr = lines.state === "ok" ? lines.frames[i] : null;
				const where = fr && fr.func === lf.func && fr.file && fr.line ? srcName(fr) : "";
				det.append(localsGroup(r.T, lf, i, f, ctx, "L" + n + (name || f.lookup), where));
			});
		};
		const ok = k + ":" + name;
		det.open = localsOpen.has(ok) ? localsOpen.get(ok) : n === 0;
		if (det.open)
			fill();
		det.addEventListener("toggle", () => {
			localsOpen.set(ok, det.open);
			if (det.open)
				fill();
		});
		box.append(det);
	});
}

// ---- memory operands in the listings (asmnames.js) -------------------------------


// The names asmOperandNames needs, made once per listing render: the
// types (starting their load), symbols, locals and CFI shards (fetched
// as asked for), and at a pause the registers and CFA at EIP. null when
// there is nothing to name with.
function opEnv() {
	const ty = dbgHas() ? dbgTypes() : null;
	const T = ty && ty.state === "ok" ? ty.T : null;
	if (!symbols && !T)
		return null;
	const row = (pa) => {
		const r = dbgHas() ? dbgCfiRow(pa) : null;
		return r && r !== "loading" && !r.why ? r : null;
	};
	const scopes = (pa) => {
		const r = T ? dbgLocalGroups(pa) : null;
		return r && r.state === "ok" ? dwarfScopesAt(r.G, pa) : null;
	};
	const env = { off: varsOffset(), T, sym: kernelSymLookup(), row, scopes, cur: null, sameFrame: null,
		loading: ty && ty.state === "loading" ? ty.why : "" };
	const eip = currentPhys();
	if (eip >= 0 && stepper) {
		const r = stepper.regs();
		const v = [r.eax, r.ecx, r.edx, r.ebx, r.esp, r.ebp, r.esi, r.edi];
		const reg = (n) => (v[n] === undefined ? undefined : v[n] >>> 0);
		const rw = row(eip);
		const cfa = rw && rw.cfa && rw.cfa.reg !== undefined && reg(rw.cfa.reg) !== undefined ? (reg(rw.cfa.reg) + rw.cfa.off) >>> 0 : undefined;
		env.cur = { pa: eip, reg, cfa };
		// The same function (one invocation: EIP's frame) as EIP.
		const fn = (pa) => { const c = scopes(pa); return c && c.length ? c[c.length - 1][0][0][2][0] : -1; };
		const mine = fn(eip);
		env.sameFrame = (pa) => mine >= 0 && fn(pa) === mine;
	}
	return env;
}

// The names of the memory operands of the instruction `text` at physical
// pa as HTML for a listing line (after the instruction), "" if none;
// counts them in stat.named.
function opNamesHtml(pa, text, env, stat) {
	if (!env)
		return "";
	const names = asmOperandNames(text, pa, env);
	if (!names.length)
		return "";
	if (stat)
		stat.named = (stat.named || 0) + names.length;
	return " " + names.map((n) => '<span class="opn" data-op="' + escHtml(n.op) + '" data-name="' + escHtml(n.name) + '" data-kind="' + n.kind + '"' +
		(n.va !== undefined ? ' data-va="' + n.va + '"' : "") + (n.rel !== undefined ? ' data-rel="' + n.rel + '"' : "") +
		' title="' + escHtml(n.op + " = " + n.detail) + '">&lt;' + escHtml(n.name) + "&gt;</span>").join(" ");
}

// An instruction's text as HTML for a listing line (the Page tab's and the
// Debugger's, whether the text is disasm.json's or decoded live): a
// direct branch's `<name+0xoff>` (llvm-objdump's, or decodeLive's) is a
// .brt span that opens the target when clicked and whose tooltip, made
// when hovered (brtTitle), is the target's source and function facts;
// then the names of the memory operands (opNamesHtml).
function insnHtml(pa, text, env, stat) {
	const t = asmBranchTarget(text);
	const m = t >= 0 ? /^(.*\S)\s+<([^<>]+)>$/.exec(text) : null;
	const head = m ? escHtml(m[1]) + ' <span class="brt" data-name="' + escHtml(m[2]) + '" data-va="' + t + '" data-pa="' +
		((t - varsOffset()) >>> 0) + '">&lt;' + escHtml(m[2]) + "&gt;</span>" : escHtml(text);
	return head + opNamesHtml(pa, text, env, stat);
}

// The tooltip of a branch target's name (.brt): its address and what the
// debug info says of the code there (dbgTitle: the inline chain and each
// function's facts), or why there is none.
function brtTitle(b) {
	const pa = Number(b.dataset.pa);
	return b.dataset.name + " at " + hex(Number(b.dataset.va)) + " (click: open it)\n" + dbgTitle(pa, b.dataset.name.replace(/\+0x[0-9a-f]+$/, ""));
}

// ---- tasks: names and pids from task_struct ------------------------------------
//
// The stepper tells tasks apart by the kernel stack they switch away on
// (stepper.js: ESP >> 13, 8 KiB stacks); the kernel's task_struct.stack
// names that stack, so the two meet there. A task's name (comm) and pid
// are read from guest RAM with the offsets of task_struct's members from
// the DWARF: the running task through the per-CPU current_task; any other
// through the task_struct current_task named when it last switched away
// (stepper.onTaskLeave = varsTaskNow), read again while the task lives (a
// name changed by exec shows), else found in the task list (init_task.tasks)
// by its stack when the kernel has that list. A task that exited keeps
// what was read as it last switched away. graph.js taskNames uses this for
// the timeline, frames and call tree; the Debugger's "task" row shows the
// running one.

// Tasks read from the task list at most (a corrupt list ends there).
const TASK_LIST_MAX = 4096;

const varsU32 = (b) => (b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24) >>> 0;

// Offsets in struct task_struct (from current_task's type), or { why }.
function varsTaskLayout(T) {
	if (T.taskLayout)
		return T.taskLayout;
	const ct = (varsByName(T).get("current_task") || [])[0];
	const it = (varsByName(T).get("init_task") || [])[0];
	const p = ct ? dwStrip(T, ct[2]) : -1;
	const sid = p >= 0 && T.types[p][0] === DWT.ptr ? dwStrip(T, T.types[p][1]) : -1;
	const m = (name) => sid >= 0 ? varsMember(T, sid, name) : null;
	const comm = m("comm"), pid = m("pid"), stack = m("stack"), tasks = m("tasks");
	T.taskLayout = !ct ? { why: "this kernel's DWARF has no current_task variable" } :
		!comm || !pid ? { why: "struct task_struct has no comm or pid in this kernel's DWARF" } :
		{ comm: comm[0], pid: pid[0], stack: stack ? stack[0] : -1, tasks: tasks && it ? tasks[0] : -1, init: it ? it[1] : -1 };
	return T.taskLayout;
}

// The task_struct at virtual address va: { va, comm, pid, region } (region:
// its stack >> 13, -1 if task_struct has no stack member), or null.
function varsTaskRead(L, va) {
	const pa = va - varsOffset();
	if (!va || pa < 0 || pa >= emuRam)
		return null;
	const c = varsRead(pa + L.comm, 16), p = varsRead(pa + L.pid, 4), s = L.stack >= 0 ? varsRead(pa + L.stack, 4) : null;
	if (!c || !p)
		return null;
	let comm = "";
	for (const b of c) {
		if (!b)
			break;
		comm += b >= 32 && b < 127 ? String.fromCharCode(b) : "\\x" + b.toString(16).padStart(2, "0");
	}
	return { va, comm, pid: varsU32(p) | 0, region: s ? varsU32(s) >>> 13 : -1 };
}

// The running task through current_task: varsTaskRead's fields plus how,
// or { why }.
function varsCurrentTask(T) {
	const L = varsTaskLayout(T);
	if (L.why)
		return L;
	const e = varsEval(T, "current_task");
	const b = e.error ? null : varsRead(e.pa, 4);
	if (!b)
		return { why: e.error || "current_task is not in RAM" };
	const t = varsTaskRead(L, varsU32(b));
	return t ? Object.assign(t, { how: "current_task" + (e.notes.length ? " (" + e.notes.join("; ") + ")" : "") }) :
		{ why: "current_task (" + hex(varsU32(b)) + ") does not point at a task_struct in RAM" };
}

// stepper.onTaskLeave: the leaving task as current_task names it (it still
// does: __switch_to sets the next one after the stack switch the stepper
// sees), or null without types (starts loading them).
function varsTaskNow(task, step, dead) {
	if (!dbgHas())
		return null;
	const r = dbgTypes();
	if (r.state !== "ok" || varsTaskLayout(r.T).why)
		return null;
	const c = varsCurrentTask(r.T);
	return c.why ? null : Object.assign(c, { step, dead });
}

// stack region -> task_struct, from the task list (init_task and every
// task on init_task.tasks), or an empty map without that list.
function varsTaskList(L) {
	const out = new Map();
	if (L.tasks < 0 || L.stack < 0 || L.init < 0)
		return out;
	const off = varsOffset(), first = varsTaskRead(L, L.init + off);
	if (first)
		out.set(first.region, first);
	const head = L.init + off + L.tasks;
	let at = head;
	for (let n = 0; n < TASK_LIST_MAX; n++) {
		const b = varsRead(at - off, 4);
		if (!b || varsU32(b) === head)
			break;
		at = varsU32(b);
		const t = varsTaskRead(L, at - L.tasks);
		if (!t)
			break;
		out.set(t.region, t);
	}
	return out;
}

// Per stepper task: { comm, pid, va, how } read from task_struct, or
// { why } when it cannot be (guess: graph.js names it by its thread function).
function varsTasks(st) {
	const r = dbgHas() ? dbgTypes() : { state: "none", why: "this kernel has no DWARF (no task_struct layout)" };
	const L = r.state === "ok" ? varsTaskLayout(r.T) : { why: r.why };
	const out = [];
	const ready = !L.why && varsReady() && !varsNotStarted();
	const cur = ready ? varsCurrentTask(r.T) : null;
	let list = null;
	for (let t = 0; t < st.taskCount; t++) {
		const region = st.taskRegion[t], seen = st.taskSeen[t];
		const alive = t === st.task || (region >= 0 && st.taskOf.get(region) === t);
		const same = (x) => x && (x.region < 0 || region < 0 || x.region === region);
		let info = null;
		if (ready && alive) {
			if (t === st.task && same(cur))
				info = Object.assign({}, cur, { how: "running: read through " + cur.how });
			else {
				// The boot task is init_task (pid 0).
				const va = seen ? seen.va : t === 0 && L.init >= 0 ? L.init + varsOffset() : 0;
				const again = va ? varsTaskRead(L, va) : null;
				if (same(again))
					info = Object.assign(again, { how: "its task_struct at " + hex(va) + (seen ? " (current_task when it last switched away)" : " (init_task)") });
				else if (region >= 0) {
					list = list || varsTaskList(L);
					const x = list.get(region);
					if (x)
						info = Object.assign({}, x, { how: "its task_struct at " + hex(x.va) + ", found in the task list (init_task.tasks) by its stack" });
				}
			}
		}
		if (!info && seen)
			info = Object.assign({}, seen, { how: "as current_task named it when it last switched away" + (seen.dead ? ", then exited" : "") });
		out.push(info || { why: L.why || (!varsReady() ? "the kernel is not running" : varsNotStarted() ? VARS_NOT_STARTED :
			"its task_struct is not known: it last switched away before the types were loaded" + (L.tasks < 0 ? ", and this kernel's task_struct has no task list" : "")) });
	}
	return out;
}

// The Debugger's "task" row: the running task (current_task).
function renderTaskRow(box) {
	const d = document.createElement("div");
	d.id = "cur_task";
	const b = document.createElement("b");
	b.textContent = "task";
	d.append(b, " ");
	const r = dbgHas() ? dbgTypes() : { state: "none", why: "this kernel has no DWARF, so no task_struct layout" };
	const L = r.state === "ok" ? varsTaskLayout(r.T) : { why: r.why };
	const c = L.why ? L : varsNotStarted() ? { why: VARS_NOT_STARTED } : varsCurrentTask(r.T);
	d.dataset.state = c.why ? (r.state === "loading" ? "loading" : "none") : "ok";
	if (c.why) {
		const s = document.createElement("span");
		s.className = "dim";
		s.textContent = "not known: " + c.why;
		d.append(s);
		d.title = "The running task (comm and pid of the task_struct current_task points at, with the member offsets from the DWARF) " +
			"is shown here when the kernel has tasks and its DWARF describes current_task and task_struct.";
	} else {
		const n = document.createElement("span");
		n.className = "mono";
		n.textContent = c.comm;
		const w = document.createElement("button");
		w.type = "button";
		w.textContent = "Watch";
		w.title = "Watch *current_task: the whole task_struct in Variables";
		w.addEventListener("click", () => watchAdd("*current_task"));
		d.append(n, " (pid " + c.pid + "), task_struct at " + hex(c.va) + " ", w);
		d.title = "The running task: comm and pid of the task_struct that " + c.how + " points at, read from guest RAM " +
			"with the member offsets from the DWARF.";
		Object.assign(d.dataset, { comm: c.comm, pid: c.pid, va: c.va });
	}
	box.append(d);
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
	// Branch target names: the tooltip when hovered (again once the DWARF
	// it needs is in), the target when clicked (before the line's own
	// click handler).
	let brtHover = null;
	document.addEventListener("mouseover", (ev) => {
		const b = ev.target.closest ? ev.target.closest(".brt") : null;
		brtHover = b;
		if (b)
			b.title = brtTitle(b);
	});
	dbgOnChange(() => {
		if (brtHover && brtHover.isConnected)
			brtHover.title = brtTitle(brtHover);
	});
	document.addEventListener("click", (ev) => {
		const b = ev.target.closest ? ev.target.closest(".brt") : null;
		if (b) {
			ev.stopPropagation();
			jumpTo(Number(b.dataset.pa));
		}
	}, true);
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
