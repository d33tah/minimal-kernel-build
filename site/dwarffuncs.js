"use strict";

// DWARF function facts (dwarf.js): the reader's part that collects them
// and DwarfFuncs, which looks them up.

import { AT, DW_TAG_SUBPROGRAM, DW_TAG_PARAM, DW_TAG_UNSPEC_PARAMS, DW_TAG_INLINED, DW_CODE_PARENTS, dwAddrForm,
	DW_TYPE_TAGS, DW_TAG_LEXICAL_BLOCK, DW_TAG_MEMBER, DW_TAG_ENUMERATOR, DW_TAG_SUBRANGE, DW_TAG_VARIABLE,
	DwarfReaderBase } from "./dwarfread.js";

// ---- function facts -------------------------------------------------------------
//
// One entry per source function (name and where it is declared): what
// DW_TAG_subprogram says about it and where its code is, out of line and
// inlined. Built unit by unit next to the types (DwarfTypes.addUnit), so
// its type ids are the table's. gen-dwarf.js ships it in name-hashed
// shards (funcs-<k>.json.gz, dwFuncShard); for an import it comes with
// the types from the Worker.
//   { format, files: [path], fns: [[name, file index (-1: unknown), line,
//     flags, inline, return type id (-1: void), [[param name, type id],
//     ...], [[lo, length, lo2, length2, ...], ...] its out-of-line code,
//     one list of ranges per DW_TAG_subprogram with code (a copy per unit
//     for a static inline function, GCC's .isra/.constprop clones; a
//     second range is GCC's .cold part), [entry, ...] its inlined copies
//     (DW_TAG_inlined_subroutine with code: the lowest address of each,
//     sorted)], ...] }
//   addresses physical; flags 1 external (DW_AT_external), 2 varargs,
//   4 noreturn, 8 prototyped; inline: DW_AT_inline (0 not inlined, 1
//   inlined, 2 declared inline but not inlined, 3 declared inline and
//   inlined) or -1 when the DIE has none.
class DwarfFuncs {
	constructor(dw) {
		this.dw = dw;
		this.byKey = new Map();
		this.list = [];
	}

	// d: decodeData with locals; id: the type table's id function for d.
	add(d, id, toPhys) {
		const local = (off) => d.subs.get(off) || (this.dw ? this.dw.subprogramAt(off) : null);
		// A function's facts through DW_AT_abstract_origin / specification:
		// each field from the first DIE of the chain that has it; the
		// parameters (names, types) of the DIE that declares them.
		const resolve = (rec) => {
			const chain = [rec];
			for (let r = rec, n = 0; n < 8; n++) {
				const next = r.origin >= 0 ? r.origin : r.spec;
				const q = next >= 0 ? local(next) : null;
				if (!q)
					break;
				chain.push(q);
				r = q;
			}
			const first = (f) => { for (const r of chain) { const v = f(r); if (v !== null && v !== undefined) return v; } return null; };
			const name = first((r) => r.name);
			if (!name)
				return null;
			// File and line each from the first DIE that has it (GCC leaves
			// DW_AT_decl_file out of a definition in its declaration's file).
			const file = first((r) => r.file), line = first((r) => r.line || null) || 0;
			const pr = chain.slice().reverse().find((r) => r.params.length) || chain[chain.length - 1];
			const params = pr.params.map((p, k) => [p[0] || first((r) => r.params[k] && r.params[k][0]) || "", p[1] !== null ? p[1] :
				first((r) => r.params[k] && r.params[k][1])]);
			const ret = first((r) => r.ret);
			return { name, file, line, ext: chain.some((r) => r.ext), varargs: chain.some((r) => r.varargs),
				noret: chain.some((r) => r.noret), proto: chain.some((r) => r.proto), inl: first((r) => r.inl), ret, params };
		};
		const entry = (f) => {
			const key = f.name + "\0" + (f.file || "") + "\0" + f.line;
			let e = this.byKey.get(key);
			if (!e) {
				e = [f.name, f.file, f.line, (f.ext ? 1 : 0) | (f.varargs ? 2 : 0) | (f.noret ? 4 : 0) | (f.proto ? 8 : 0),
					f.inl === null ? -1 : f.inl, f.ret === null ? -1 : id(f.ret), f.params.map(([n, t]) => [n, t === null ? -1 : id(t)]), [], []];
				this.byKey.set(key, e);
				this.list.push(e);
			} else {
				// Another unit's DIE of the same function adds what this one lacked.
				e[3] |= (f.ext ? 1 : 0) | (f.varargs ? 2 : 0) | (f.noret ? 4 : 0) | (f.proto ? 8 : 0);
				if (e[4] < 0 && f.inl !== null)
					e[4] = f.inl;
			}
			return e;
		};
		for (const rec of d.subs.values()) {
			const rs = rec.rs.map(([lo, hi]) => [toPhys(lo), hi - lo]).filter(([pa]) => pa !== null);
			if (!rs.length || rec.decl)
				continue;
			const f = resolve(rec);
			if (!f)
				continue;
			const e = entry(f);
			if (!e[7].some((r) => r[0] === rs[0][0]))
				e[7].push(rs.flat());
		}
		for (const [origin, lo] of d.copies) {
			const pa = toPhys(lo);
			const rec = pa === null ? null : local(origin);
			const f = rec ? resolve(rec) : null;
			if (f)
				entry(f)[8].push(pa);
		}
	}

	finish() {
		const files = [], fileIx = new Map();
		for (const e of this.list) {
			const p = e[1];
			if (p === null) {
				e[1] = -1;
				continue;
			}
			if (!fileIx.has(p)) {
				fileIx.set(p, files.length);
				files.push(p);
			}
			e[1] = fileIx.get(p);
			e[8].sort((x, y) => x - y);
		}
		return { format: 1, files, fns: this.list };
	}
}

// The shard of a function's facts by name (FNV-1a), for gen-dwarf.js and
// the page alike.
function dwFuncShard(name, n) {
	let h = 0x811c9dc5;
	for (let k = 0; k < name.length; k++)
		h = Math.imul(h ^ name.charCodeAt(k), 0x01000193) >>> 0;
	return h % n;
}

// The name DWARF gives the function of an ELF symbol: GCC's clones and
// split parts (start_kernel.cold, foo.isra.0, foo.constprop.0, foo.part.0)
// are the function before the first dot.
const dwFuncBase = (sym) => sym.replace(/\..*$/, "");

class DwarfReaderFuncs extends DwarfReaderBase {
	// ---- function facts (DwarfFuncs) ----------------------------------------
	// What a DW_TAG_subprogram DIE says about its function, as it says it
	// (a concrete DIE with an abstract origin says almost nothing itself):
	// { name, file (display path), line, ext, inl (DW_AT_inline or null),
	// decl, noret, proto, ret (type DIE offset, null: none given), origin,
	// spec (DIE offsets, -1: none), params: [[name, type offset or null]],
	// varargs, rs: [[lo, hi], ...] virtual (set by the caller) }.
	subRecord(c, a, ref, files) {
		const n = a[AT.name] || a[AT.linkage_name] || a[AT.mips_linkage_name];
		const fi = a[AT.decl_file] ? a[AT.decl_file][1] : -1;
		return { name: n ? this.str(c, ...n) : null, file: fi >= 0 && files[fi] ? files[fi] : null,
			line: a[AT.decl_line] ? a[AT.decl_line][1] : 0, ext: !!a[AT.external], inl: a[AT.inline] ? a[AT.inline][1] : null,
			decl: !!a[AT.declaration], noret: !!a[AT.noreturn], proto: !!a[AT.prototyped], ret: a[AT.type] ? ref(a[AT.type]) : null,
			origin: a[AT.abstract_origin] ? ref(a[AT.abstract_origin]) : -1, spec: a[AT.specification] ? ref(a[AT.specification]) : -1,
			params: [], varargs: false, rs: [] };
	}

	// The display paths of unit i's line program (what DW_AT_decl_file indexes).
	unitFiles(i) {
		if (!this.filesCache)
			this.filesCache = new Map();
		let f = this.filesCache.get(i);
		if (!f) {
			const r = this.root(i);
			f = r.stmt >= 0 && this.sec.readLine ? this.lineProgram(r.stmt, r.c.comp, true).files : [];
			this.filesCache.set(i, f);
		}
		return f;
	}

	// subRecord of the DW_TAG_subprogram DIE at absolute offset off in any
	// unit, with its parameters (an abstract origin or declaration that
	// clang puts in another unit, DW_FORM_ref_addr); null if it is none.
	subprogramAt(off) {
		if (!this.subCache)
			this.subCache = new Map();
		if (this.subCache.has(off))
			return this.subCache.get(off);
		let rec = null;
		const i = this.unitOf(off);
		if (i >= 0) {
			const c = this.root(i).c;
			c.b = this.unitBytes(i);
			c.p = off - c.base;
			const abbrevs = this.abbrevs(c.u.abbrevOff);
			const ab = abbrevs[this.uleb(c)];
			if (ab && ab.tag === DW_TAG_SUBPROGRAM) {
				const ref = (x) => x[0] === 0x10 || x[0] === 0x1f20 ? x[1] : c.base + x[1];
				rec = this.subRecord(c, this.readDie(c, ab), ref, this.unitFiles(i));
				for (let depth = 0; ab.children && c.p < c.b.length;) {
					const code = this.uleb(c);
					if (!code) {
						if (--depth < 0)
							break;
						continue;
					}
					const k = abbrevs[code];
					if (!k)
						break;
					if (depth === 0 && k.tag === DW_TAG_PARAM) {
						const a = this.readDie(c, k);
						rec.params.push([a[AT.name] ? this.str(c, ...a[AT.name]) : null, a[AT.type] ? ref(a[AT.type]) : null]);
					} else {
						this.skipDie(c, k);
						if (depth === 0 && k.tag === DW_TAG_UNSPEC_PARAMS)
							rec.varargs = true;
					}
					if (k.children)
						depth++;
				}
			}
		}
		if (this.subCache.size > 50000)
			this.subCache.clear();
		this.subCache.set(off, rec);
		return rec;
	}

	// One compile unit: its line rows and the address ranges of its
	// functions (depth 0) and inlined calls (depth 1 + inlined ancestors),
	// with the call site of each inlined call.
	decodeUnit(i) {
		const r = this.root(i);
		const c = r.c;
		c.b = this.unitBytes(i);
		const out = { files: [], rows: [], ranges: [], cover: r.ranges, name: r.name, comp: c.comp };
		if (r.stmt >= 0 && this.sec.readLine)
			Object.assign(out, this.lineProgram(r.stmt, c.comp));
		if (!r.children)
			return out;
		const abbrevs = this.abbrevs(c.u.abbrevOff);
		const end = c.u.end - c.base;
		const inl = [0]; // inlined-call depth at each DIE nesting level
		const ref = (x) => x[0] === 0x10 || x[0] === 0x1f20 ? x[1] : c.base + x[1];
		while (c.p < end && inl.length) {
			const dieOff = c.base + c.p;
			const code = this.uleb(c);
			if (!code) {
				inl.pop();
				continue;
			}
			const ab = abbrevs[code];
			if (!ab)
				throw new Error("bad abbreviation code at .debug_info+" + dieOff.toString(16));
			const tag = ab.tag;
			let depth = inl[inl.length - 1];
			if (tag === DW_TAG_SUBPROGRAM || tag === DW_TAG_INLINED) {
				const a = this.readDie(c, ab);
				if (tag === DW_TAG_SUBPROGRAM)
					depth = 0;
				else
					depth++;
				let rs = [];
				if (a[AT.ranges])
					rs = this.ranges(c, ...a[AT.ranges]);
				else if (a[AT.low_pc] && a[AT.high_pc]) {
					const lo = this.addr(c, ...a[AT.low_pc]);
					const [f, v] = a[AT.high_pc];
					const hi = dwAddrForm(f) ? this.addr(c, f, v) : lo + v;
					if (hi > lo)
						rs = [[lo, hi]];
				}
				if (rs.length) {
					let name = null;
					const n = a[AT.name] || a[AT.linkage_name] || a[AT.mips_linkage_name];
					if (n)
						name = this.str(c, ...n);
					else if (a[AT.abstract_origin])
						name = this.nameAt(ref(a[AT.abstract_origin]));
					else if (a[AT.specification])
						name = this.nameAt(ref(a[AT.specification]));
					const cf = tag === DW_TAG_INLINED && a[AT.call_file] ? a[AT.call_file][1] : -1;
					const cl = tag === DW_TAG_INLINED && a[AT.call_line] ? a[AT.call_line][1] : 0;
					for (const [lo, hi] of rs)
						out.ranges.push(lo, hi, depth, name, cf, cl);
				}
			} else if (ab.children && !DW_CODE_PARENTS.has(tag)) {
				if (ab.sib >= 0) {
					const a = this.readDie(c, ab);
					c.p = ref(a[AT.sibling]) - c.base;
					continue;
				}
				this.skipDie(c, ab);
			} else
				this.skipDie(c, ab);
			if (ab.children)
				inl.push(depth);
		}
		return out;
	}

	// One compile unit's data: the variables with a fixed address
	// (DW_OP_addr / DW_OP_addrx, optionally + DW_OP_plus_uconst; static
	// locals included) and the raw type DIEs, for DwarfTypes.addUnit.
	// Types are { kind, name, size, type (absolute DIE offset or -1 for
	// void), ... } keyed by absolute DIE offset. With locals, also the
	// scopes of the code (dwarfLocals reads them): see collectScopes().
	decodeData(i, locals = false) {
		const r = this.root(i);
		const c = r.c;
		c.b = this.unitBytes(i);
		const files = r.stmt >= 0 && this.sec.readLine ? this.lineProgram(r.stmt, c.comp, true).files : [];
		const out = { types: new Map(), vars: [], files };
		if (!r.children)
			return out;
		const abbrevs = this.abbrevs(c.u.abbrevOff);
		const end = c.u.end - c.base;
		const ref = (x) => x[0] === 0x10 || x[0] === 0x1f20 ? x[1] : c.base + x[1];
		const tref = (a) => a[AT.type] ? ref(a[AT.type]) : -1;
		const name = (a) => a[AT.name] ? this.str(c, ...a[AT.name]) : null;
		const cval = (x) => {
			if (!x)
				return null;
			if (x.length === 3) // a block: little-endian bytes
				return x[2] <= 6 ? [...c.b.subarray(x[1], x[1] + x[2])].reduceRight((v, y) => v * 256 + y, 0) : null;
			return x[1];
		};
		const declared = new Map(); // declaration DIEs of variables, for DW_AT_specification
		const L = locals ? { scopes: [], info: new Map(), kids: new Map(), lastKid: null, subs: new Map(), copies: [], files } : null;
		const stack = [{ tag: r.tag, raw: null, fn: -1, scope: -1, die: -1 }];
		while (c.p < end && stack.length) {
			const dieOff = c.base + c.p;
			const code = this.uleb(c);
			if (!code) {
				stack.pop();
				continue;
			}
			const ab = abbrevs[code];
			if (!ab)
				throw new Error("bad abbreviation code at .debug_info+" + dieOff.toString(16));
			const tag = ab.tag;
			const parent = stack[stack.length - 1];
			const kind = DW_TYPE_TAGS[tag];
			let raw = null, scope = parent.scope;
			if (L) {
				L.lastKid = null;
				if (parent.die >= 0) {
					L.lastKid = [dieOff, tag, -1];
					const k = L.kids.get(parent.die);
					if (k)
						k.push(L.lastKid);
					else
						L.kids.set(parent.die, [L.lastKid]);
				}
			}
			if (L && (tag === DW_TAG_SUBPROGRAM || tag === DW_TAG_INLINED || tag === DW_TAG_LEXICAL_BLOCK)) {
				scope = this.scopeDie(c, ab, tag, dieOff, parent, L, ref);
			} else if (kind) {
				const a = this.readDie(c, ab);
				raw = { kind, name: name(a), size: a[AT.byte_size] ? a[AT.byte_size][1] : -1, type: tref(a) };
				if (a[AT.declaration])
					raw.decl = true;
				if (kind === "base")
					raw.enc = a[AT.encoding] ? a[AT.encoding][1] : 0;
				else if (kind === "struct" || kind === "union" || kind === "enum") {
					raw.list = [];
					if (a[AT.decl_file])
						raw.at = (files[a[AT.decl_file][1]] || "?") + ":" + (a[AT.decl_line] ? a[AT.decl_line][1] : 0);
				} else if (kind === "array")
					raw.dims = [];
				else if (kind === "func")
					raw.params = [];
				out.types.set(dieOff, raw);
			} else if (tag === DW_TAG_MEMBER && parent.raw && parent.raw.list && parent.raw.kind !== "enum") {
				const a = this.readDie(c, ab);
				const loc = a[AT.data_member_location];
				let off = 0;
				if (loc && loc.length === 3) {
					// DWARF 2 style: DW_OP_plus_uconst n
					const q = { b: c.b, p: loc[1] };
					if (c.b[q.p++] === 0x23)
						off = this.uleb(q);
				} else if (loc)
					off = loc[1];
				const m = [name(a), off, tref(a)];
				if (a[AT.bit_size]) {
					const bits = a[AT.bit_size][1];
					let pos;
					if (a[AT.data_bit_offset])
						pos = a[AT.data_bit_offset][1];
					else {
						// DWARF 2/3: bit_offset counts from the most significant bit of a
						// byte_size storage unit at data_member_location.
						const unit = a[AT.byte_size] ? a[AT.byte_size][1] : 0;
						pos = off * 8 + (a[AT.bit_offset] ? unit * 8 - a[AT.bit_offset][1] - bits : 0);
					}
					m[1] = Math.floor(pos / 8);
					m.push(bits, pos % 8);
				}
				parent.raw.list.push(m);
			} else if (tag === DW_TAG_ENUMERATOR && parent.raw && parent.raw.kind === "enum") {
				const a = this.readDie(c, ab);
				// data1/2/4 values stay unsigned: the page compares values modulo
				// the enum's size.
				parent.raw.list.push([name(a), cval(a[AT.const_value])]);
			} else if (tag === DW_TAG_SUBRANGE && parent.raw && parent.raw.kind === "array") {
				const a = this.readDie(c, ab);
				let n = -1;
				if (a[AT.count] && a[AT.count].length === 2 && a[AT.count][0] !== 0x13)
					n = a[AT.count][1];
				else if (a[AT.upper_bound] && a[AT.upper_bound].length === 2 && a[AT.upper_bound][0] !== 0x13) {
					const ub = a[AT.upper_bound];
					// upper_bound -1 (all ones in data forms) is a flexible array: no count.
					const v = ub[0] === 0x0d ? ub[1] : ub[0] === 0x0b && ub[1] === 0xff ? -1 : ub[0] === 0x05 && ub[1] === 0xffff ? -1 :
						ub[0] === 0x06 && ub[1] === 0xffffffff ? -1 : ub[1];
					const lb = a[AT.lower_bound] ? a[AT.lower_bound][1] : 0;
					n = v < 0 ? -1 : v - lb + 1;
				}
				parent.raw.dims.push(n);
			} else if ((tag === DW_TAG_PARAM || tag === DW_TAG_UNSPEC_PARAMS) && parent.raw && parent.raw.kind === "func") {
				if (tag === DW_TAG_PARAM) {
					const a = this.readDie(c, ab);
					parent.raw.params.push(tref(a));
				} else {
					this.skipDie(c, ab);
					parent.raw.varargs = true;
				}
			} else if (L && tag === DW_TAG_PARAM && (parent.tag === DW_TAG_SUBPROGRAM || parent.tag === DW_TAG_INLINED)) {
				const a = this.readDie(c, ab);
				this.localDie(c, a, true, dieOff, parent, L, ref);
				const sub = parent.tag === DW_TAG_SUBPROGRAM ? L.subs.get(parent.die) : null;
				if (sub)
					sub.params.push([name(a), a[AT.type] ? ref(a[AT.type]) : null]);
			} else if (L && tag === DW_TAG_UNSPEC_PARAMS && parent.tag === DW_TAG_SUBPROGRAM) {
				this.skipDie(c, ab);
				const sub = L.subs.get(parent.die);
				if (sub)
					sub.varargs = true;
			} else if (tag === DW_TAG_VARIABLE) {
				const a = this.readDie(c, ab);
				if (L && parent.die >= 0)
					this.localDie(c, a, false, dieOff, parent, L, ref);
				const file = a[AT.decl_file] ? files[a[AT.decl_file][1]] || null : null;
				const line = a[AT.decl_line] ? a[AT.decl_line][1] : 0;
				if (a[AT.declaration])
					declared.set(dieOff, { name: name(a), type: tref(a), external: !!a[AT.external], file, line });
				const loc = a[AT.location];
				const va = loc && loc.length === 3 ? this.fixedAddr(c, loc[1], loc[2]) : null;
				if (va !== null)
					out.vars.push({ name: name(a), type: tref(a), va, spec: a[AT.specification] ? ref(a[AT.specification]) : -1,
						origin: a[AT.abstract_origin] ? ref(a[AT.abstract_origin]) : -1, external: !!a[AT.external], fn: parent.fn, file, line });
			} else
				this.skipDie(c, ab);
			if (ab.children)
				stack.push({ tag, raw, fn: tag === DW_TAG_SUBPROGRAM ? dieOff : parent.fn, scope,
					die: tag === DW_TAG_SUBPROGRAM || tag === DW_TAG_INLINED || tag === DW_TAG_LEXICAL_BLOCK ? dieOff : -1 });
		}
		if (L) {
			L.origins = new Map();
			for (const list of L.kids.values())
				for (const [d, , o] of list)
					L.origins.set(d, o);
			out.scopes = this.finishScopes(L);
			out.subs = L.subs;
			out.copies = L.copies;
		}
		// A definition through DW_AT_specification / abstract_origin takes the
		// declaration's name, type and (when it has none) place, as gdb
		// reads the attributes; static locals name their function.
		for (const v of out.vars) {
			const d = declared.get(v.spec) || declared.get(v.origin);
			if (d) {
				v.name = v.name || d.name;
				if (v.type < 0)
					v.type = d.type;
				v.external = v.external || d.external;
				v.file = v.file || d.file;
				v.line = v.line || d.line;
			}
			if ((!v.file || !v.line) && (v.spec >= 0 || v.origin >= 0)) {
				const [f, l] = this.declAt(v.origin >= 0 ? v.origin : v.spec);
				v.file = v.file || f;
				v.line = v.line || l;
			}
			// (A C++ static data member: its declaration is a DW_TAG_member.)
			const decl = v.origin >= 0 ? v.origin : v.spec;
			if (!v.name && decl >= 0)
				v.name = this.nameAt(decl);
			if (v.type < 0 && decl >= 0)
				v.type = this.typeAt(decl);
			v.scope = v.fn >= 0 ? this.nameAt(v.fn) : null;
		}
		return out;
	}
}

export { DwarfFuncs, dwFuncShard, dwFuncBase, DwarfReaderFuncs };
