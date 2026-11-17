"use strict";

// DWARF line tables and inline chains: EIP -> file:line and the functions
// it was inlined into (innermost first, like `llvm-symbolizer --inlines`).
//
// One set of modules for three users: site/gen-dwarf.js (node, CI)
// decodes a whole -g vmlinux into address shards for the shipped kernels;
// dwarfworker.js (the page's Worker) decodes one compile unit at a time of
// an imported vmlinux; the page (dbginfo.js) looks up both through
// dwarfTable() and dwarfFrames(), so there is one lookup and one render
// path.
//
// Supports DWARF 2-5, 32-bit DWARF, little-endian, the forms of GCC
// (strp, line_strp, sec_offset, ref4, implicit_const, ...) and of clang
// (strx1-4, addrx, rnglistx, loclistx, ref_addr with str_offsets, addr
// and rnglists bases). Relocation sections (.rel.debug_*) are ignored: a
// vmlinux is ET_EXEC, its DWARF values are final.
//
// DwarfReader is a chain of classes, one per concern: DwarfReaderBase
// (dwarfread.js: units, abbreviations, DIEs, line programs, with the tag
// and form constants), DwarfReaderFuncs (dwarffuncs.js: function facts,
// with DwarfFuncs) and DwarfReader here (scopes, parameters and locals).
// The ELF sections, line tables and shards are in dwarftables.js, the
// lookups of locals, types and variables in dwarflocals.js, DwarfTypes in
// dwarftypes.js.

import { dwOpEnd } from "./unwind.js";
import { AT, DW_TAG_SUBPROGRAM, DW_TAG_INLINED, DW_TAG_LEXICAL_BLOCK, dwAddrForm, DW_TAG_PARAM, DW_TAG_VARIABLE,
	u32le } from "./dwarfread.js";
import { DwarfReaderFuncs } from "./dwarffuncs.js";

class DwarfReader extends DwarfReaderFuncs {

	// ---- scopes, parameters and local variables (decodeData with locals) ----
	// A scope is a concrete function (kind 0: a DW_TAG_subprogram with code),
	// a lexical block with code (1) or an inlined call (2), with its address
	// ranges, its parent scope and its variables. The rules are gdb's
	// (dwarf2/read.c read_lexical_block_scope, inherit_abstract_dies): a
	// lexical block without code holds its variables for the scope around
	// it; a function, inlined call or block with an abstract origin also
	// gets the origin's children that none of its own children refers to
	// (their parameters and variables are optimized out here), where a
	// clang block without DW_AT_abstract_origin stands for the origin's
	// child at the same place when both have children of the same tags.

	// The scope a DW_TAG_subprogram / inlined_subroutine / lexical_block
	// opens for its children: a new one, the parent's, or -1 (no code).
	scopeDie(c, ab, tag, dieOff, parent, L, ref) {
		const a = this.readDie(c, ab);
		const origin = a[AT.abstract_origin] ? ref(a[AT.abstract_origin]) : -1;
		if (L.lastKid)
			L.lastKid[2] = origin;
		const sub = tag === DW_TAG_SUBPROGRAM ? this.subRecord(c, a, ref, L.files) : null;
		if (sub)
			L.subs.set(dieOff, sub);
		if (tag !== DW_TAG_SUBPROGRAM && parent.scope < 0)
			return -1;
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
		if (sub)
			sub.rs = rs;
		else if (tag === DW_TAG_INLINED && rs.length && origin >= 0)
			L.copies.push([origin, rs.reduce((m, r) => Math.min(m, r[0]), Infinity)]);
		if (!rs.length)
			return tag === DW_TAG_LEXICAL_BLOCK && !a[AT.low_pc] && !a[AT.ranges] ? parent.scope : -1;
		let name = null;
		if (tag !== DW_TAG_LEXICAL_BLOCK) {
			const n = a[AT.name] || a[AT.linkage_name] || a[AT.mips_linkage_name];
			if (n)
				name = this.str(c, ...n);
			else if (origin >= 0)
				name = this.nameAt(origin);
			else if (a[AT.specification])
				name = this.nameAt(ref(a[AT.specification]));
		}
		L.scopes.push({ kind: tag === DW_TAG_SUBPROGRAM ? 0 : tag === DW_TAG_LEXICAL_BLOCK ? 1 : 2,
			parent: tag === DW_TAG_SUBPROGRAM ? -1 : parent.scope, ranges: rs, name, origin, die: dieOff, vars: [],
			fb: tag === DW_TAG_SUBPROGRAM ? this.locAttr(c, a[AT.frame_base]) : null });
		return L.scopes.length - 1;
	}

	// A DW_TAG_formal_parameter or DW_TAG_variable inside code: remembered
	// (abstract origins and inheritance need it) and added to its scope.
	localDie(c, a, param, dieOff, parent, L, ref) {
		const origin = a[AT.abstract_origin] ? ref(a[AT.abstract_origin]) : -1;
		if (L.lastKid)
			L.lastKid[2] = origin;
		if (a[AT.declaration])
			return;
		const v = { die: dieOff, name: a[AT.name] ? this.str(c, ...a[AT.name]) : null, type: a[AT.type] ? ref(a[AT.type]) : -1, origin, param,
			loc: this.locAttr(c, a[AT.location]), cv: a[AT.const_value] ? (a[AT.const_value].length === 3 ?
				c.b.slice(a[AT.const_value][1], a[AT.const_value][1] + a[AT.const_value][2]) : a[AT.const_value][1]) : null };
		L.info.set(dieOff, v);
		if (parent.scope >= 0)
			L.scopes[parent.scope].vars.push(v);
	}

	// A location attribute: { expr: bytes }, { list: [[lo, hi, bytes], ...] }
	// (virtual addresses), { why } or null (none: optimized out).
	locAttr(c, x) {
		if (!x)
			return null;
		if (x.length === 3)
			return { expr: this.ownExpr(c, c.b.slice(x[1], x[1] + x[2])) };
		const l = this.locList(c, x[0], x[1]);
		return l ? { list: l.map(([lo, hi, e]) => [lo, hi, this.ownExpr(c, e)]) } : { why: "its location list is in a section this file does not have" };
	}

	// An expression that needs nothing but itself: DW_OP_addrx and
	// DW_OP_GNU_addr_index (an index into .debug_addr, clang) become
	// DW_OP_addr with the address.
	ownExpr(c, e) {
		const opEnd = dwOpEnd;
		if (!e.includes(0xa1) && !e.includes(0xfb))
			return e;
		const out = [];
		for (let p = 0; p < e.length;) {
			const q = opEnd(e, p);
			if (q < 0)
				return e;
			if (e[p] === 0xa1 || e[p] === 0xfb) {
				const cur = { b: e, p: p + 1 };
				const va = this.addr(c, 0x1b, this.uleb(cur));
				if (va === null)
					return e;
				out.push(0x03, va & 255, va >>> 8 & 255, va >>> 16 & 255, va >>> 24 & 255);
			} else
				out.push(...e.subarray(p, q));
			p = q;
		}
		return Uint8Array.from(out);
	}

	// Names and types through abstract origins, then the inherited ones.
	finishScopes(L) {
		const resolve = (v, depth = 0) => {
			if ((v.name !== null && v.type >= 0 && (v.cv !== null || v.loc)) || v.origin < 0 || depth > 8)
				return v;
			const o = L.info.get(v.origin);
			if (o) {
				resolve(o, depth + 1);
				if (v.name === null) v.name = o.name;
				if (v.type < 0) v.type = o.type;
				// gdb reads an attribute a DIE lacks from its abstract origin
				// (a static local has its DW_OP_addr there).
				if (v.cv === null && !v.loc) {
					v.cv = o.cv;
					v.loc = o.loc;
				}
			} else {
				if (v.name === null) v.name = this.nameAt(v.origin);
				if (v.type < 0) v.type = this.typeAt(v.origin);
			}
			return v;
		};
		const kids = (die) => L.kids.get(die) || this.foreignKids(die, L);
		// An abstract DIE gdb processes: a parameter or variable becomes
		// one of s's (optimized out), a block (no code) passes its own on.
		const take = (s, k, tag, depth) => {
			if (tag === DW_TAG_LEXICAL_BLOCK) {
				if (depth < 16)
					for (const [kk, kt] of kids(k))
						take(s, kk, kt, depth + 1);
			} else if (tag === DW_TAG_PARAM || tag === DW_TAG_VARIABLE) {
				const o = L.info.get(k);
				if (o)
					s.vars.push(resolve({ die: k, name: o.name, type: o.type, origin: o.origin, param: o.param, loc: o.loc, cv: o.cv,
						inherited: !o.loc && o.cv === null }));
			}
		};
		const final = (o) => {
			for (let n = 0; o >= 0 && n < 8; n++) {
				const up = L.origins.get(o);
				if (up === undefined || up < 0 || up === o)
					break;
				o = up;
			}
			return o;
		};
		// The DIEs under a block, through nested blocks: their abstract
		// origins (concrete side) or their offsets (abstract side).
		const under = (die, abstract, out = new Set(), depth = 0) => {
			for (const [k, tag, origin] of abstract ? kids(die) : L.kids.get(die) || []) {
				const o = abstract ? k : final(origin);
				if (o >= 0)
					out.add(o);
				if (tag === DW_TAG_LEXICAL_BLOCK && depth < 16)
					under(k, abstract, out, depth + 1);
			}
			return out;
		};
		const scopeOf = new Map(L.scopes.map((x, k) => [x.die, k]));
		for (const s of L.scopes) {
			s.vars.forEach((v) => resolve(v));
			if (s.origin < 0)
				continue;
			const mine = L.kids.get(s.die) || [], theirs = kids(s.origin);
			const iso = mine.length === theirs.length && mine.every((x, k) => x[1] === theirs[k][1]);
			const parentOf = new Set(theirs.map((x) => x[0]));
			const used = new Set();
			mine.forEach(([die, tag, origin], k) => {
				if (tag === 0x48 || tag === 0x4109) // call sites
					return;
				let o = final(origin);
				if (o < 0 && iso && tag === DW_TAG_LEXICAL_BLOCK)
					o = theirs[k][0];
				if (o < 0 && tag === DW_TAG_LEXICAL_BLOCK) {
					// A clang block without DW_AT_abstract_origin in a copy whose
					// children differ from the origin's (e.g. an extra inlined
					// call): the origin's block holding what its DIEs refer to.
					const refs = under(die, false);
					const b = refs.size ? theirs.find(([t, tt]) => tt === DW_TAG_LEXICAL_BLOCK && !used.has(t) &&
						[...under(t, true)].some((x) => refs.has(x))) : null;
					if (b) {
						o = b[0];
						const own = scopeOf.get(die);
						if (own !== undefined && L.scopes[own].origin < 0)
							L.scopes[own].origin = o; // its missing DIEs are inherited there
						else if (own === undefined) // no code: its variables went to s
							for (const [kk, kt] of kids(o))
								if (!refs.has(kk))
									take(s, kk, kt, 1);
					}
				}
				if (o >= 0 && parentOf.has(o))
					used.add(o);
			});
			for (const [k, tag] of theirs)
				if (!used.has(k))
					take(s, k, tag, 0);
		}
		return L.scopes;
	}

	// The children of a DIE in another unit (an abstract origin clang
	// refers to with DW_FORM_ref_addr) as [[offset, tag, origin], ...],
	// like L.kids for this unit; their parameters and variables go into
	// L.info, the blocks' children are read when asked for.
	foreignKids(off, L) {
		const out = [];
		L.kids.set(off, out);
		const i = this.unitOf(off);
		if (i < 0)
			return out;
		const c = this.root(i).c;
		c.b = this.unitBytes(i);
		c.p = off - c.base;
		const abbrevs = this.abbrevs(c.u.abbrevOff);
		const ab = abbrevs[this.uleb(c)];
		if (!ab || !ab.children)
			return out;
		this.skipDie(c, ab);
		const ref = (x) => x[0] === 0x10 || x[0] === 0x1f20 ? x[1] : c.base + x[1];
		for (let depth = 0; c.p < c.b.length;) {
			const die = c.base + c.p;
			const code = this.uleb(c);
			if (!code) {
				if (--depth < 0)
					break;
				continue;
			}
			const k = abbrevs[code];
			if (!k)
				break;
			if (depth === 0) {
				const a = this.readDie(c, k);
				const origin = a[AT.abstract_origin] ? ref(a[AT.abstract_origin]) : -1;
				out.push([die, k.tag, origin]);
				if ((k.tag === DW_TAG_PARAM || k.tag === DW_TAG_VARIABLE) && !a[AT.declaration])
					L.info.set(die, { die, name: a[AT.name] ? this.str(c, ...a[AT.name]) : null, type: a[AT.type] ? ref(a[AT.type]) : -1,
						origin, param: k.tag === DW_TAG_PARAM, loc: this.locAttr(c, a[AT.location]),
						cv: a[AT.const_value] ? (a[AT.const_value].length === 3 ?
							c.b.slice(a[AT.const_value][1], a[AT.const_value][1] + a[AT.const_value][2]) : a[AT.const_value][1]) : null });
			} else
				this.skipDie(c, k);
			if (k.children)
				depth++;
		}
		return out;
	}

	// DW_AT_type of the DIE at absolute offset off (a variable's abstract
	// origin), -1 if none.
	typeAt(off) {
		const i = this.unitOf(off);
		if (i < 0)
			return -1;
		const c = this.root(i).c;
		c.b = this.unitBytes(i);
		c.p = off - c.base;
		const ab = this.abbrevs(c.u.abbrevOff)[this.uleb(c)];
		if (!ab)
			return -1;
		const a = this.readDie(c, ab);
		return a[AT.type] ? (a[AT.type][0] === 0x10 ? a[AT.type][1] : c.base + a[AT.type][1]) : -1;
	}

	// [file, line] of DW_AT_decl_file / decl_line of the DIE at absolute
	// offset off in any unit, each through its abstract origin or
	// specification when the DIE has none (as gdb reads them; GCC leaves
	// out of a definition what its declaration already says); null / 0 if
	// unknown.
	declAt(off) {
		let file = null, line = 0;
		for (let depth = 0; depth < 8 && (!file || !line); depth++) {
			const i = this.unitOf(off);
			if (i < 0)
				break;
			const c = this.root(i).c;
			c.b = this.unitBytes(i);
			c.p = off - c.base;
			const ab = this.abbrevs(c.u.abbrevOff)[this.uleb(c)];
			if (!ab)
				break;
			const a = this.readDie(c, ab);
			if (!file && a[AT.decl_file])
				file = this.unitFiles(i)[a[AT.decl_file][1]] || null;
			if (!line && a[AT.decl_line])
				line = a[AT.decl_line][1];
			const next = a[AT.abstract_origin] || a[AT.specification];
			if (!next)
				break;
			off = next[0] === 0x10 ? next[1] : c.base + next[1];
		}
		return [file, line];
	}

	// The address a location expression names when it is a fixed one:
	// DW_OP_addr / DW_OP_addrx / DW_OP_GNU_addr_index, optionally followed by
	// DW_OP_plus_uconst; null for anything else (registers, stack values, ...).
	fixedAddr(c, p, len) {
		const b = c.b, end = p + len;
		const q = { b, p: p + 1 };
		let va;
		if (b[p] === 0x03) {
			va = c.u.addrSize === 4 ? u32le(b, q.p) : u32le(b, q.p) + u32le(b, q.p + 4) * 2 ** 32;
			q.p += c.u.addrSize;
		} else if (b[p] === 0xa1 || b[p] === 0xfb)
			va = this.addr(c, 0x1b, this.uleb(q));
		else
			return null;
		if (q.p < end && b[q.p] === 0x23) {
			q.p++;
			va += this.uleb(q);
		}
		return q.p === end && va !== null ? va : null;
	}
}

export { DwarfReader };
