"use strict";

// Variables from the kernel's DWARF (vars.js): reading guest memory
// through the page tables, evaluating a watch expression, and the rows
// that show a value with its members.

import { V86_KERNEL_ADDR, hex } from "./util.js";
import { emulator, emuReady, emuRam, stepper, symbols, api } from "./page.js";
import { DWT } from "./dwarfread.js";
import { dwStrip, dwVarsPick, dwTypeName, dwTypeSize, dwVarDecls, dwWhere } from "./dwarflocals.js";
import { srcLinkEl } from "./srclinks.js";
import { virtOffset } from "./livedis.js";
// Elements of an array shown at once.
const WATCH_MAX_ELEMENTS = 64;
// Bytes of a char * target shown as a string.
const WATCH_MAX_STRING = 96;
const watchOpen = new Set(); // "expression/path" of expanded rows

const varsReady = () => emulator && emuReady;

// Virtual - physical of the kernel's data (0 for hello).
const varsOffset = () => virtOffset(V86_KERNEL_ADDR);

// Values that are not in RAM (a local in a register, a computed or
// constant value, pieces of these) live in cells at made-up "physical"
// addresses from VARS_CELLS on, so the one render path (varsRow and what
// it calls) shows them like memory. Remade at every Debugger render.
const VARS_CELLS = 2 ** 40, VARS_CELL_SIZE = 2 ** 20;
let varsCells = [];
const setVarsCells = (v) => { varsCells = v; }; // vars.js renderLocals remakes them

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
	const eip = api.machine.currentPhys();
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

// A watch expression: name, Function::name or file.c::name (also gdb's
// 'file.c'::name; dwarflocals.js dwVarsPick), then .member, ->member, [index]; a
// leading * dereferences the result. { pa, type, bits?, notes, v (the
// variable), all (every variable of that name) } or { error }.
function varsEval(T, expr) {
	const m = /^\s*(\*?)\s*(?:(?:'([^']+)'|([\w.\/+-]+))::)?([A-Za-z_]\w*)\s*(.*)$/.exec(expr);
	if (!m)
		return { error: "not a variable name: " + expr };
	const pick = dwVarsPick(T, varsByName(T).get(m[4]) || [], m[2] || m[3] || "", m[4]);
	if (pick.why)
		return { error: pick.why };
	const v = pick.v;
	const notes = varsNote(T, v);
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
	let rest = m[5], p = 0;
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
	return { pa, type, bits, notes, v, all: pick.all };
}

// "defined at" of variable v: a link (srcLinkEl) to its
// DW_AT_decl_file:decl_line (data-file, data-line, data-var: its address;
// more than one when several units define it at its address, dwVarDecls), or why not
// known. The one render path for the Variables panel's rows and notes and
// the Page listing's hex-hover text.
function varDeclEl(T, v) {
	const ds = dwVarDecls(T, v);
	if (!ds.length)
		return document.createTextNode(T.files ? "an unknown place (its DIE has no DW_AT_decl_file)" :
			"an unknown place (these variables were read by an older version of this page: import the vmlinux again)");
	const el = document.createElement("span");
	ds.forEach((d, k) => {
		const b = srcLinkEl(d.file, d.line, d.file + ":" + d.line);
		b.classList.add("varloc");
		b.dataset.var = v[1];
		b.title = v[0] + " is defined at " + d.file + ":" + d.line + " (DW_AT_decl_file, DW_AT_decl_line): open it in the Source panel";
		el.append(k === 1 ? ", also at " : k ? ", " : "", b);
	});
	if (ds.length > 1)
		el.append(" (definitions at one address: e.g. a __weak one and the one the linker kept; DWARF does not say which)");
	return el;
}

// The most other definitions of a watched name its note lists.
const VARS_OTHERS_SHOWN = 20;

// The notes under a watch of variable v: where it is defined and, when
// other variables have its name, theirs with their addresses.
function varsDeclNotes(T, v, all) {
	const out = [];
	const d = document.createElement("div");
	d.className = "note wdecl";
	d.append("defined at ", varDeclEl(T, v));
	out.push(d);
	if (all.length > 1) {
		const o = document.createElement("div");
		o.className = "note wothers";
		o.append(all.length + " variables have this name: this is the one in " + varsSection(T, v) + " at " + hex(v[1] + varsOffset()) + ". The others: ");
		const rest = all.filter((x) => x !== v);
		rest.slice(0, VARS_OTHERS_SHOWN).forEach((x, k) => {
			o.append(k ? ", " : "", varDeclEl(T, x), " (" + hex(x[1] + varsOffset()) + (x[5] & 4 && x[6] ? ", static local of " + x[6] + "()" : "") + ")");
		});
		if (rest.length > VARS_OTHERS_SHOWN)
			o.append(", and " + (rest.length - VARS_OTHERS_SHOWN) + " more");
		o.append(". file.c::" + v[0] + " picks one by the file it is defined in (path or unique suffix), Function::" + v[0] + " a static local.");
		out.push(o);
	}
	return out;
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
	const s = symbols && pa >= 0 ? api.symbols.symbolAt(symbols.symbols, pa) : null;
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
// (Only for what varsScalar leaves: a struct, union or array.)
function varsChildren(T, type, pa) {
	const s = dwStrip(T, type);
	const t = T.types[s];
	if (t[0] === DWT.struct || t[0] === DWT.union) {
		if (!t[3])
			return { list: [], note: "only declared: this kernel's DWARF has no members for " + dwTypeName(T, s) };
		return { list: t[3].map((m) => [m[0] ? "." + m[0] : "(anonymous " + dwTypeName(T, m[2]).split(" ")[0] + ")", pa + m[1], m[2],
			m.length > 3 ? [m[3], m[4]] : null]) };
	}
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
				const b = api.symbols.symButton(sc.text, to, "Open the Page tab at " + hex(sc.ptr) + " (physical " + hex(to) + ")");
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

export { varsReady, varsOffset, setVarsCells, varsCell, varsRead, varsNotStarted, VARS_NOT_STARTED, varsByName,
	varsSection, varsNote, varsMember, varsEval, varDeclEl, varsDeclNotes, le, varsScalar, varsRow };
