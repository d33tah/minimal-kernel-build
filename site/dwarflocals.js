"use strict";

// DWARF parameters and local variables (dwarf.js): scopes, locations,
// type names and sizes, variable declarations and where an address is.

import { DWT } from "./dwarfread.js";
import { dwarfResolvePath } from "./dwarftables.js";

// ---- parameters and local variables -------------------------------------------
//
// The scopes of a unit's code as the page reads them (gen-dwarf.js ships
// them in locals-<hex>.json shards per 64 KiB, the Worker sends a unit's
// for an import): one group per concrete function, its scopes in DIE
// order, the function first:
//   [kind, parent, ranges, name, frame base, vars]
//     kind    0 function, 1 lexical block, 2 inlined call
//     parent  index of the enclosing scope in the group (-1: the function)
//     ranges  [lo, length, lo2, length2, ...] physical
//     name    the function's (kinds 0 and 2), null for a block
//     frame base  location (below) of DW_AT_frame_base, functions only
//     vars    [[name, type id (DwarfTypes), flags, location], ...] in DIE
//             order; flags 1: a parameter, 2: only in the abstract origin
//             (the compiler said nothing about it here: optimized out)
// A location is null (none: optimized out), "hex" (one DWARF expression),
// [[lo, length, "hex"], ...] (a location list, physical; length 0: the
// default location), { c: number } or { cb: "hex" } (DW_AT_const_value),
// or { why } (unreadable).
function dwarfScopeGroups(scopes, id, toPhys) {
	const hx = (b) => Array.from(b, (x) => (x < 16 ? "0" : "") + x.toString(16)).join("");
	const loc = (l, cv) => {
		if (cv !== null && cv !== undefined)
			return typeof cv === "number" ? { c: cv } : { cb: hx(cv) };
		if (!l)
			return null;
		if (l.why)
			return { why: l.why };
		if (l.expr)
			return hx(l.expr);
		const out = [];
		for (const [lo, hi, e] of l.list) {
			if (lo === 0 && hi === 2 ** 32) {
				out.push([0, 0, hx(e)]);
				continue;
			}
			const pa = toPhys(lo);
			if (pa !== null)
				out.push([pa, hi - lo, hx(e)]);
		}
		return out;
	};
	const groups = [];
	let g = null, map = null;
	scopes.forEach((s, k) => {
		const rs = [];
		for (const [lo, hi] of s.ranges) {
			const pa = toPhys(lo);
			if (pa !== null)
				rs.push(pa, hi - lo);
		}
		if (s.kind === 0) {
			g = rs.length ? [] : null;
			map = new Map();
			if (g)
				groups.push(g);
		}
		if (!g || !rs.length || (s.kind !== 0 && !map.has(s.parent)))
			return;
		map.set(k, g.length);
		g.push([s.kind, s.kind === 0 ? -1 : map.get(s.parent), rs, s.name, s.kind === 0 ? loc(s.fb, null) : null,
			s.vars.filter((v) => v.name).map((v) => [v.name, id(v.type), (v.param ? 1 : 0) | (v.inherited ? 2 : 0), loc(v.loc, v.cv)])]);
	});
	return groups;
}

// The scopes of group list G containing physical address pa, innermost
// first: [[group, scope index], ...]; [] when no function there.
function dwarfScopesAt(G, pa) {
	const inside = (r) => {
		for (let k = 0; k < r.length; k += 2)
			if (pa >= r[k] && pa < r[k] + r[k + 1])
				return true;
		return false;
	};
	for (const g of G) {
		if (!inside(g[0][2]))
			continue;
		const out = [];
		for (let k = g.length - 1; k >= 0; k--)
			if (inside(g[k][2]) && (!out.length || dwScopeIn(g, out[out.length - 1][1], k)))
				out.push([g, k]);
		return out;
	}
	return [];
}

// A variable's location (dwarfScopeGroups) in force at physical address
// pa: { expr: Uint8Array }, { c } / { cb } (a constant), or { why } (none
// here: optimized out, or unreadable).
function dwLocAt(loc, pa) {
	const bytes = (h) => Uint8Array.from(h.match(/../g) || [], (x) => parseInt(x, 16));
	if (loc === null)
		return { why: "optimized out (the compiler kept no location for it)" };
	if (typeof loc === "string")
		return { expr: bytes(loc) };
	if (!Array.isArray(loc))
		return loc.cb !== undefined ? { cb: bytes(loc.cb) } : loc;
	let dflt = null;
	for (const [lo, len, e] of loc) {
		if (!len)
			dflt = e;
		else if (pa >= lo && pa < lo + len)
			return { expr: bytes(e) };
	}
	return dflt !== null ? { expr: bytes(dflt) } : { why: "optimized out here (its location list has no entry for this address)" };
}

// The frames at pa with their variables, innermost first, from the
// scopes dwarfScopesAt found: [{ func, inlined, scopes: [group, index],
// params: [var], locals: [var] }], one per inlined call and one for the
// function; a frame's locals are those of its lexical blocks around pa,
// innermost block first (gdb's order), its parameters in DIE order.
function dwarfLocalFrames(chain) {
	const out = [];
	let cur = null;
	for (const [g, k] of chain) {
		const s = g[k];
		if (!cur) {
			cur = { func: null, inlined: false, fb: g[0][4], params: [], locals: [] };
			out.push(cur);
		}
		for (const v of s[5])
			(v[2] & 1 ? cur.params : cur.locals).push(v);
		if (s[0] !== 1) {
			cur.func = s[3];
			cur.inlined = s[0] === 2;
			cur = null;
		}
	}
	return out.filter((f) => f.func !== null || f.params.length || f.locals.length);
}

// Whether scope a of group g lies inside scope b.
function dwScopeIn(g, a, b) {
	for (let s = a, n = 0; s >= 0 && n < 64; s = g[s][1], n++)
		if (s === b)
			return true;
	return false;
}

// The type behind typedefs and qualifiers (and a declaration's definition).
function dwStrip(T, id) {
	for (let n = 0; id >= 0 && n < 64; n++) {
		const t = T.types[id];
		if (t[0] === DWT.typedef || (t[0] >= DWT.const && t[0] <= DWT.atomic))
			id = t[t[0] === DWT.typedef ? 2 : 1];
		else if ((t[0] === DWT.struct || t[0] === DWT.union) && !t[3] && t[5] !== undefined)
			id = t[5];
		else if (t[0] === DWT.enum && !t[3] && t[6] !== undefined)
			id = t[6];
		else
			return id;
	}
	return id;
}

// Bytes of a type; 0 when unknown (void, a declaration only, a flexible array).
function dwTypeSize(T, id) {
	id = dwStrip(T, id);
	if (id < 0)
		return 0;
	const t = T.types[id];
	switch (t[0]) {
	case DWT.base: case DWT.ptr: return t[2];
	case DWT.struct: case DWT.union: case DWT.enum: return t[2] > 0 ? t[2] : 0;
	case DWT.array: return t[2].some((n) => n < 0) ? 0 : t[2].reduce((x, n) => x * n, dwTypeSize(T, t[1]));
	default: return 0;
	}
}

// C spelling of a type, as gdb's whatis prints it: "struct task_struct",
// "char [16]", "void (*)(struct work_struct *)", "const char *".
function dwTypeName(T, id, inner = "") {
	const sp = (s) => s ? " " + s : "";
	if (id < 0)
		return "void" + sp(inner);
	const t = T.types[id];
	switch (t[0]) {
	case DWT.base: case DWT.typedef: case DWT.void: return t[1] + sp(inner);
	case DWT.struct: case DWT.union: case DWT.enum:
		return (t[0] === DWT.struct ? "struct " : t[0] === DWT.union ? "union " : "enum ") + (t[1] || "{...}") + sp(inner);
	case DWT.ptr: {
		const to = t[1] >= 0 ? T.types[t[1]][0] : -1;
		return dwTypeName(T, t[1], to === DWT.array || to === DWT.func ? "(*" + inner + ")" : "*" + inner);
	}
	case DWT.const: case DWT.volatile: case DWT.restrict: case DWT.atomic: {
		// A run of qualifiers, in gdb's order (const volatile).
		const quals = (x) => {
			const qs = new Set();
			for (let n = 0; x >= 0 && T.types[x][0] >= DWT.const && T.types[x][0] <= DWT.atomic && n < 8; n++) {
				qs.add(T.types[x][0]);
				x = T.types[x][1];
			}
			return [qs, x];
		};
		const [qs, below] = quals(id);
		const to = below >= 0 ? T.types[below] : null;
		// A qualified array is an array of qualified elements (GCC says both).
		if (to && to[0] === DWT.array)
			for (const q of quals(to[1])[0])
				qs.delete(q);
		const q = [...qs].sort((a, b) => a - b).map((k) => ["const", "volatile", "restrict", "_Atomic"][k - DWT.const]).join(" ");
		if (!q)
			return dwTypeName(T, below, inner);
		if (to && to[0] === DWT.ptr)
			return dwTypeName(T, below, q + sp(inner));
		return q + " " + dwTypeName(T, below, inner);
	}
	case DWT.array: return dwTypeName(T, t[1], inner + t[2].map((n) => "[" + (n >= 0 ? n : "") + "]").join(""));
	case DWT.func: {
		const ps = t[2].map((p) => dwTypeName(T, p));
		if (t[3])
			ps.push("...");
		return dwTypeName(T, t[1], inner + "(" + (ps.length ? ps.join(", ") : "void") + ")");
	}
	default: return "?" + sp(inner);
	}
}

// A function (DwarfFuncs entry e) in C: "int printk(const char *fmt,
// ...)", or without names its type as gdb's ptype prints it, "int (const
// char *, ...)". A prototyped function without parameters takes (void).
function dwFuncProto(T, e, named = true) {
	const ps = e[6].map(([n, t]) => dwTypeName(T, t, named ? n : ""));
	if (e[3] & 2)
		ps.push("...");
	return dwTypeName(T, e[5], (named ? e[0] : "") + "(" + (ps.length ? ps.join(", ") : e[3] & 8 ? "void" : "") + ")");
}

// Index of the last variable starting at or below pa, -1 if none.
function dwVarIndex(T, pa) {
	const v = T.vars;
	let a = 0, b = v.length;
	while (a < b) { const m = (a + b) >> 1; if (v[m][1] <= pa) a = m + 1; else b = m; }
	return a - 1;
}

// Where variable v (types.json) is defined: { file, line } (DW_AT_decl_file
// and decl_line, through the declaration or abstract origin when the
// definition has none) or null.
function dwVarDecl(T, v) {
	return T.files && v.length > 7 && v[7] >= 0 && T.files[v[7]] ? { file: T.files[v[7]], line: v[8] } : null;
}

// Every place a definition of v at its address says it is defined:
// dwVarDecl's first, then those of other units (a __weak definition and
// the one the linker kept); [] when unknown.
function dwVarDecls(T, v) {
	const d = dwVarDecl(T, v);
	return d ? [d, ...(v[9] || []).map(([f, line]) => ({ file: T.files[f], line }))] : [];
}

// "file:line" of dwVarDecls (", also at file:line ..."), or "" when unknown.
function dwVarDeclText(T, v) {
	const ds = dwVarDecls(T, v);
	return ds.map((d, k) => (k === 1 ? ", also at " : k ? ", " : "") + d.file + ":" + d.line).join("") +
		(ds.length > 1 ? " (definitions at one address: e.g. a __weak one and the one the linker kept)" : "");
}

// The variable a watch expression's name picks out of all those with that
// name (list): qualifier q ("" none; a function: Function::name, its static
// locals; a source file, any q with a "." or "/": file.c::name, the file
// the variable is defined in, by path or unique suffix as dwarfResolvePath
// takes them); among several, the external one, else the first.
// { v, list (the variables q leaves), all: list } or { why, all }.
function dwVarsPick(T, all, q, name) {
	let list = all;
	if (q && /[./]/.test(q)) {
		if (!T.files)
			return { why: "this kernel's variables were read without their DW_AT_decl_file: they cannot be picked by file", all };
		const paths = [...new Set(all.flatMap((v) => dwVarDecls(T, v)).map((d) => d.file))];
		const r = dwarfResolvePath(paths, q, name + " is not defined in " + q.replace(/^\.?\//, "") + (paths.length ? "; it is defined in " +
			paths.slice(0, 5).join(", ") + (paths.length > 5 ? ", … (" + paths.length + " files)" : "") : "; no definition of it says where it is"));
		if (r.why)
			return { why: r.why, all };
		list = all.filter((v) => dwVarDecls(T, v).some((d) => d.file === r.path));
	} else if (q)
		list = all.filter((v) => v[6] === q);
	if (!list.length)
		return { why: "no variable " + (q ? q + "::" : "") + name + " in this kernel's DWARF", all };
	return { v: list.find((x) => x[5] & 1) || list[0], list, all };
}

// The variable holding byte pa and the path to it, innermost member or
// element last: { var, path: "init_task.comm[3]" (a C expression), note
// ("padding", "bits 3-5"), type, offset (of pa in the leaf), leafAddr,
// unions: the other members of a union there } or null.
function dwWhere(T, pa) {
	if (T.maxSize === undefined)
		T.maxSize = T.vars.reduce((x, v) => Math.max(x, v[3]), 1);
	let best = null;
	for (let k = dwVarIndex(T, pa); k >= 0 && T.vars[k][1] > pa - T.maxSize - 1; k--) {
		// The smallest variable holding pa; one of size 0 (an empty struct,
		// unknown size) only names its first byte when no other holds it.
		const v = T.vars[k];
		if (v[3] ? pa < v[1] + v[3] && (!best || !best[3] || v[3] < best[3]) : pa === v[1] && !best)
			best = v;
	}
	if (!best)
		return null;
	const r = dwPathIn(T, best[2], pa - best[1], best[0]);
	return { var: best, ...r, leafAddr: pa - r.offset };
}

// The member or element path to byte off of an object of type id named
// name: { path, note, type (of the leaf), offset (of the byte in the leaf),
// unions }, as dwWhere describes them.
function dwPathIn(T, id, off, name) {
	let path = name, note = "";
	const unions = [];
	for (let n = 0; n < 32; n++) {
		const s = dwStrip(T, id);
		if (s < 0)
			break;
		const t = T.types[s];
		if ((t[0] === DWT.struct || t[0] === DWT.union) && t[3]) {
			const hits = t[3].filter((m) => {
				const sz = m.length > 3 ? Math.ceil((m[4] + m[3]) / 8) : dwTypeSize(T, m[2]);
				return off >= m[1] && off < m[1] + Math.max(sz, 1);
			});
			if (!hits.length) {
				note = "padding";
				break;
			}
			const m = hits[0];
			if (t[0] === DWT.union && hits.length > 1)
				unions.push(...hits.slice(1).map((x) => x[0] || "{...}"));
			if (m[0])
				path += "." + m[0];
			off -= m[1];
			id = m[2];
			if (m.length > 3) {
				note = m[3] === 1 ? "bit " + m[4] : "bits " + m[4] + "\u2013" + (m[4] + m[3] - 1);
				break;
			}
		} else if (t[0] === DWT.array) {
			const es = dwTypeSize(T, t[1]);
			if (!es)
				break;
			const dims = t[2];
			let i = Math.floor(off / es);
			off -= i * es;
			const idx = [];
			for (let d = dims.length - 1; d >= 0; d--) {
				const n = dims[d] > 0 ? dims[d] : Infinity;
				idx.unshift(d ? i % n : i);
				i = Math.floor(i / n);
			}
			path += idx.map((x) => "[" + x + "]").join("");
			id = t[1];
		} else
			break;
	}
	return { path, note, type: id, offset: off, unions };
}

export { dwarfScopeGroups, dwarfScopesAt, dwLocAt, dwarfLocalFrames, dwStrip, dwTypeSize, dwTypeName, dwFuncProto,
	dwVarDecl, dwVarDecls, dwVarDeclText, dwVarsPick, dwWhere, dwPathIn };
