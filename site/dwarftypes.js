"use strict";

// DWARF types and variables (dwarf.js): DwarfTypes.

import { DWT } from "./dwarfread.js";
import { DwarfFuncs } from "./dwarffuncs.js";
import { dwarfScopeGroups, dwTypeSize } from "./dwarflocals.js";

// ---- types and variables ----------------------------------------------------
//
// GCC repeats every type in every compile unit; DwarfTypes keeps one copy
// of each (named struct/union/enum: by kind, name, size and where it is
// declared; everything else by its structure) and only the types the
// variables reach. The result, { types, vars, sections } as JSON
// (gen-dwarf.js writes it for the shipped kernels, dwarfworker.js builds
// it for an import), is what the page's Variables panel and memory hover
// read:
//   types[id]  [DWT.base, name, size, DW_ATE encoding]
//              [DWT.ptr, target, size]            target -1: void
//              [DWT.const|volatile|restrict|atomic, target]
//              [DWT.typedef, name, target]
//              [DWT.struct|union, name, size, members, decl file:line, definition]
//                 members [name, byte offset, type] (+ bit size, bit position
//                 for a bit field); name "" for an anonymous member; members
//                 null for a declaration, then "definition" is the id of the
//                 full type of the same name when the kernel has one
//              [DWT.enum, name, size, [[name, value], ...], underlying type, decl file:line, definition]
//              [DWT.array, element, [count, ...]]  count -1: unknown (flexible)
//              [DWT.func, return type, [parameter types], varargs]
//              [DWT.void, name]
//   vars       [name, physical address, type, size, section index, flags, function]
//              sorted by address; flags 1: external, 2: per-CPU (the
//              template copy in .data..percpu), 4: a static local; function:
//              the one a static local is in (none when DWARF does not name it:
//              clang puts the statics of an only-inlined function in an
//              empty DW_TAG_subprogram)

// GCC's names of C's integer types, as C programmers (and gdb) write them.
const DW_BASE_NAMES = { "long int": "long", "long unsigned int": "unsigned long", "short int": "short",
	"short unsigned int": "unsigned short", "long long int": "long long", "long long unsigned int": "unsigned long long" };

class DwarfTypes {
	// dw: the DwarfReader, for types another unit holds (DW_FORM_ref_addr).
	constructor(dw) {
		this.dw = dw;
		this.types = [];
		this.keys = new Map();
		this.vars = [];
		this.files = []; // where variables are defined (vars[k][7])
		this.fileIx = new Map();
		this.seen = new Map(); // name@address -> variable
		this.other = new Map(); // unit index -> its decodeData types
		this.funcs = new DwarfFuncs(dw);
	}

	// The type id of a DIE offset of unit d (DwarfReader.decodeData): an
	// existing entry of this table, or a new one appended.
	idFor(d) {
		const memoKey = new Map(), memoId = new Map();
		const T = { get: (off) => d.types.get(off) || this.foreign(off) };
		const key = (off) => {
			if (off < 0)
				return "v";
			let k = memoKey.get(off);
			if (k !== undefined)
				return k;
			const t = T.get(off);
			if (!t)
				return "?";
			// A cycle can only pass through a named struct/union/enum (keyed by
			// name, no recursion); the placeholder keeps anything else finite.
			memoKey.set(off, "@" + off);
			switch (t.kind) {
			case "base": k = "b|" + t.name + "|" + t.size + "|" + t.enc; break;
			case "void": k = "0|" + t.name; break;
			case "ptr": case "const": case "volatile": case "restrict": case "atomic": k = t.kind + "|" + key(t.type); break;
			case "typedef": k = "t|" + t.name + "|" + key(t.type); break;
			case "array": k = "a|" + key(t.type) + "|" + t.dims.join(","); break;
			case "func": k = "f|" + key(t.type) + "|" + t.params.map(key).join(",") + "|" + (t.varargs ? 1 : 0); break;
			default:
				if (t.name)
					k = t.kind + "|" + t.name + "|" + (t.decl ? "decl" : t.size + "|" + (t.at || ""));
				else
					k = t.kind + "|" + t.size + "|{" + (t.kind === "enum" ? t.list.map((e) => e.join("=")).join(",") :
						t.list.map((m) => m[0] + "@" + m.slice(1).map((x, j) => j === 1 ? key(x) : x).join(":")).join(",")) + "}";
			}
			memoKey.set(off, k);
			return k;
		};
		const id = (off) => {
			if (off < 0)
				return -1;
			let v = memoId.get(off);
			if (v !== undefined)
				return v;
			const t = T.get(off);
			const k = t ? key(off) : "?";
			v = this.keys.get(k);
			if (v === undefined) {
				v = this.types.length;
				this.keys.set(k, v);
				memoId.set(off, v);
				this.types.push(null);
				this.types[v] = !t ? [DWT.void, "?"] : this.entry(t, id);
			} else
				memoId.set(off, v);
			return v;
		};
		return id;
	}

	// The scopes of unit d (decodeData with locals) with this table's type
	// ids (dwarfScopeGroups), for an import's Worker after the table is
	// done: types that are new to the table (none when the table was made
	// with locals) are appended, added says how many.
	scopesOf(d, toPhys) {
		const n = this.types.length;
		const groups = dwarfScopeGroups(d.scopes || [], this.idFor(d), toPhys);
		return { groups, added: this.types.slice(n) };
	}

	// d: DwarfReader.decodeData of one unit. toPhys: virtual -> physical or
	// null (outside the loaded image); secOf: virtual -> section index.
	// With d.scopes (decodeData with locals): returns its functions with
	// their scopes and variables, types as ids of this table
	// (dwarfScopeGroups), else [].
	addUnit(d, toPhys, secOf) {
		const id = this.idFor(d);
		for (const x of d.vars) {
			const pa = toPhys(x.va);
			if (pa === null || !x.name)
				continue;
			const k = x.name + "@" + pa;
			const had = this.seen.get(k);
			if (had && x.file) {
				// Another definition at the same address (a __weak one and the
				// one the linker kept: DWARF does not say which is which).
				const f = this.fileId(x.file), line = x.line || 0;
				if (had[7] < 0)
					[had[7], had[8]] = [f, line];
				else if ((had[7] !== f || had[8] !== line) && !(had[9] || []).some((d) => d[0] === f && d[1] === line))
					(had[9] = had[9] || []).push([f, line]);
			}
			if (had) {
				// Another unit's DIE for it: keep a complete type over an
				// incomplete one (an array without a size: extern const char x[]).
				const T = { types: this.types };
				if (!dwTypeSize(T, had[2]) && x.type >= 0) {
					const t = id(x.type);
					if (dwTypeSize(T, t))
						had[2] = t;
				}
				continue;
			}
			const sec = secOf(x.va);
			const v = [x.name, pa, id(x.type), 0, sec[0], (x.external ? 1 : 0) | (sec[1] ? 2 : 0) | (x.fn >= 0 ? 4 : 0), x.scope || "",
				x.file ? this.fileId(x.file) : -1, x.line || 0];
			this.seen.set(k, v);
			this.vars.push(v);
		}
		if (d.subs)
			this.funcs.add(d, id, toPhys);
		return d.scopes ? dwarfScopeGroups(d.scopes, id, toPhys) : [];
	}

	// Index of path in files (vars[k][7]), added when new.
	fileId(path) {
		let f = this.fileIx.get(path);
		if (f === undefined) {
			f = this.files.length;
			this.files.push(path);
			this.fileIx.set(path, f);
		}
		return f;
	}

	foreign(off) {
		const i = this.dw ? this.dw.unitOf(off) : -1;
		if (i < 0)
			return undefined;
		let m = this.other.get(i);
		if (!m) {
			if (this.other.size > 8)
				this.other.clear();
			m = this.dw.decodeData(i).types;
			this.other.set(i, m);
		}
		return m.get(off);
	}

	entry(t, id) {
		switch (t.kind) {
		case "base": return [DWT.base, DW_BASE_NAMES[t.name] || t.name, t.size, t.enc];
		case "void": return [DWT.void, t.name || "void"];
		case "ptr": return [DWT.ptr, id(t.type), t.size > 0 ? t.size : 4];
		case "typedef": return [DWT.typedef, t.name, id(t.type)];
		case "array": return [DWT.array, id(t.type), t.dims.length ? t.dims : [-1]];
		case "func": return [DWT.func, id(t.type), t.params.map(id), t.varargs ? 1 : 0];
		case "struct": case "union":
			return [DWT[t.kind], t.name || "", t.decl ? -1 : t.size, t.decl ? null : t.list.map((m) => {
				const e = [m[0] || "", m[1], id(m[2])];
				if (m.length > 3)
					e.push(m[3], m[4]);
				return e;
			}), t.at || ""];
		case "enum": return [DWT.enum, t.name || "", t.decl ? -1 : t.size, t.decl ? null : t.list, id(t.type), t.at || ""];
		default: return [DWT[t.kind], id(t.type)];
		}
	}

	// { format, types, vars, sections, files }: declarations point to the
	// definition of the same name, vars get their sizes and are sorted.
	// A variable: [name, physical address, type id, size, section index,
	// flags (1 external, 2 per-CPU, 4 static local), function of a static
	// local, index in files of DW_AT_decl_file, DW_AT_decl_line, and only
	// when other units define a variable of that name at that address
	// too: [[file index, line], ...] of those]; the trailing fields are
	// left out when unknown ("", -1).
	finish(sections) {
		const defs = new Map();
		this.types.forEach((t, k) => {
			if ((t[0] === DWT.struct || t[0] === DWT.union || t[0] === DWT.enum) && t[1] && t[3] && !defs.has(t[0] + t[1]))
				defs.set(t[0] + t[1], k);
		});
		for (const t of this.types)
			if ((t[0] === DWT.struct || t[0] === DWT.union || t[0] === DWT.enum) && !t[3] && defs.has(t[0] + t[1]))
				t[t[0] === DWT.enum ? 6 : 5] = defs.get(t[0] + t[1]);
		const out = { format: 2, types: this.types, vars: this.vars, sections, files: this.files, funcs: this.funcs.finish() };
		for (const v of this.vars) {
			v[3] = dwTypeSize(out, v[2]);
			if (v[7] < 0)
				v.length = v[6] ? 7 : 6;
		}
		this.vars.sort((x, y) => x[1] - y[1] || y[3] - x[3]);
		return out;
	}
}

export { DwarfTypes };
