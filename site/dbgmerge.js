"use strict";

// The debug info files of a kernel (dbginfo.js) read and decoded, and the
// DWARF of a kernel rebuilt in this page from a kit whose objects have no
// debug sections (Debian, plan E6): the rebuilt ELF has DWARF of the units
// compiled here only, so the files made from it (genimport.js) cover those,
// and CI's files of the shipped kernel (its -g build: the same code) cover
// every other unit, moved to where the rebuild placed it.
//
// Where code moved: each symbol of the shipped symbols.json is paired with
// the rebuilt kernel's symbol of the same name and size (the k-th of that
// name by address with the k-th, when both kernels have as many; the
// virtual - physical offset equal), and runs of pairs with the same
// address difference become intervals [lo, hi, d] of the new addresses
// (new = old + d). The compiled units' code ("owned": the outermost DWARF
// ranges of the files made here) is taken from those files only.
//
// The shards a lookup reads are made per 64 KiB of new addresses: the
// owned code's rows from the files made here, the rest from CI's shards of
// the old addresses, shifted. Line rows and ranges, call frame information,
// parameters and locals (their ranges, location lists and DW_OP_addr), the
// function facts' code ranges and inlined copies, the variables'
// addresses and files.json are moved the same way. The parameters, locals
// and facts of owned code keep the type table of the files made here
// (ownT); the others use CI's.

import { dwarfDecode, dwarfPack, dwarfRow } from "./dwarftables.js";
import { cfiDecode, cfiPack } from "./unwind.js";
import { dwOpEnd } from "./dwexpr.js";
import { DWT } from "./dwarfread.js";

// Bytes of a .gz file as fetched: gunzipped, unless the server already
// decoded it (Content-Encoding: gzip).
async function dbgGunzip(buf) {
	const b = new Uint8Array(buf);
	if (b.length > 2 && b[0] === 0x1f && b[1] === 0x8b)
		return new Uint8Array(await new Response(new Blob([b]).stream().pipeThrough(new DecompressionStream("gzip"))).arrayBuffer());
	return b;
}

const mgText = (b) => new TextDecoder().decode(b);

// A debug info file of the directory base (kernels.json "dwarf"): kind
// index, line (a line shard), cfi, locals, funcs (key: the shard), types,
// files (ix: the index naming them), decoded.
function mgRead(kfetch, base, ix, kind, key) {
	const name = { index: "index.json", types: ix && ix.types, files: ix && ix.files, line: key.toString(16) + ".bin",
		cfi: "cfi-" + key.toString(16) + ".bin", locals: "locals-" + key.toString(16) + ".json.gz",
		funcs: "funcs-" + key.toString(16) + ".json.gz" }[kind];
	return kfetch(base + name).then((r) => r.ok ? r.arrayBuffer() : Promise.reject(new Error("HTTP " + r.status))).then(async (buf) => {
		if (kind === "line")
			return dwarfDecode(new Uint8Array(buf));
		if (kind === "cfi")
			return cfiDecode(new Uint8Array(buf));
		return JSON.parse(mgText(kind === "locals" || kind === "funcs" ? await dbgGunzip(buf) : new Uint8Array(buf)));
	});
}

// Intervals [lo, hi, d] of the new addresses (sorted, not overlapping)
// from pairs [new, old, size].
function mgIntervals(pairs) {
	pairs.sort((x, y) => x[0] - y[0]);
	const iv = [];
	for (const [n, o, size] of pairs) {
		const d = n - o, last = iv[iv.length - 1];
		if (last && n < last[1]) {
			if (last[2] === d)
				last[1] = Math.max(last[1], n + size);
			continue; // an alias placed elsewhere in the old kernel: not trusted
		}
		if (last && last[2] === d)
			last[1] = n + size;
		else
			iv.push([n, n + size, d]);
	}
	return iv;
}

// The pairs of symbols (see the top): { phys, virt } intervals.
function mgMap(newSyms, oldSyms) {
	const group = (syms) => {
		const m = new Map();
		for (const s of syms)
			if (s.size > 0) {
				if (!m.has(s.name))
					m.set(s.name, []);
				m.get(s.name).push(s);
			}
		return m;
	};
	const N = group(newSyms), O = group(oldSyms), phys = [], virt = [];
	for (const [name, ns] of N) {
		const os = O.get(name);
		if (!os || os.length !== ns.length)
			continue;
		ns.sort((a, b) => a.paddr - b.paddr);
		os.sort((a, b) => a.paddr - b.paddr);
		ns.forEach((n, i) => {
			const o = os[i];
			if (n.size === o.size && n.vaddr - n.paddr === o.vaddr - o.paddr) {
				phys.push([n.paddr, o.paddr, n.size]);
				virt.push([n.vaddr, o.vaddr, n.size]);
			}
		});
	}
	return { phys: mgIntervals(phys), virt: mgIntervals(virt) };
}

// Index of the last interval of iv (sorted by start) starting at or below x.
function mgAt(iv, x) {
	let a = 0, b = iv.length;
	while (a < b) { const m = (a + b) >> 1; if (iv[m][0] <= x) a = m + 1; else b = m; }
	return a - 1;
}

// The parts of [lo, hi) the intervals iv cover: [[a, b, d], ...].
function mgClip(iv, lo, hi) {
	const out = [];
	for (let k = Math.max(0, mgAt(iv, lo)); k < iv.length && iv[k][0] < hi; k++) {
		const a = Math.max(lo, iv[k][0]), b = Math.min(hi, iv[k][1]);
		if (b > a)
			out.push([a, b, iv[k][2]]);
	}
	return out;
}

// [lo, hi) less the sorted, disjoint ranges R: [[a, b], ...].
function mgLess(R, lo, hi) {
	const out = [];
	let a = lo;
	for (let k = Math.max(0, mgAt(R, lo)); k < R.length && R[k][0] < hi; k++) {
		if (R[k][1] <= a)
			continue;
		if (R[k][0] > a)
			out.push([a, R[k][0]]);
		a = Math.max(a, R[k][1]);
	}
	if (a < hi)
		out.push([a, hi]);
	return out;
}

// Sorted ranges merged where they overlap or touch.
function mgUnion(R) {
	R.sort((x, y) => x[0] - y[0]);
	const out = [];
	for (const [a, b] of R)
		if (out.length && a <= out[out.length - 1][1])
			out[out.length - 1][1] = Math.max(b, out[out.length - 1][1]);
		else
			out.push([a, b]);
	return out;
}

// One line table from parts [table, lo, hi, d]: the rows and ranges of
// table in [lo, hi) moved by d, each part ended by a row without a line.
function mgLines(parts) {
	const strings = [], sidx = new Map();
	const sid = (t, k) => {
		if (k < 0)
			return -1;
		const s = t.strings[k];
		let v = sidx.get(s);
		if (v === undefined) {
			v = strings.length;
			strings.push(s);
			sidx.set(s, v);
		}
		return v;
	};
	const rows = [], sx = [], rg = new Map();
	let noStmt = false;
	for (const [t, a, b, d] of parts) {
		noStmt = noStmt || !t.lk || !!t.noStmt;
		for (let k = Math.max(0, dwarfRow(t, a)); k < t.la.length && t.la[k] < b; k++)
			rows.push([Math.max(a, t.la[k]) + d, 1, sid(t, t.lf[k]), t.ll[k], t.la[k] >= a && t.lk ? t.lk[k] : 0]);
		rows.push([b + d, 0, -1, 0, 0]);
		if (t.sx) {
			let x = 0, y = t.sx.length;
			while (x < y) { const q = (x + y) >> 1; if (t.sx[q] < a) x = q + 1; else y = q; }
			for (; x < t.sx.length && t.sx[x] < b; x++)
				sx.push(t.sx[x] + d);
		}
		for (let q = 0; q < t.ra.length; q++)
			if (t.ra[q] < b && t.rh[q] > a) {
				const r = [t.ra[q] + d, t.rh[q] + d, t.rd[q], sid(t, t.rn[q]), sid(t, t.rf[q]), t.rl[q]];
				rg.set(r.join(","), r);
			}
	}
	rows.sort((x, y) => x[0] - y[0] || x[1] - y[1]);
	const la = [], lf = [], ll = [], lk = [];
	for (const r of rows) {
		const n = la.length - 1;
		if (n >= 0 && la[n] === r[0]) {
			lf[n] = r[2]; ll[n] = r[3]; lk[n] = r[4];
			continue;
		}
		la.push(r[0]); lf.push(r[2]); ll.push(r[3]); lk.push(r[4]);
	}
	const t = dwarfPack(strings, la, lf, ll, [...rg.values()].sort((x, y) => x[0] - y[0] || x[2] - y[2]), lk, sx.sort((x, y) => x - y));
	if (noStmt)
		t.noStmt = true;
	return t;
}

// One CFI table from parts [table, lo, hi, d]: the FDEs of table that
// overlap [lo, hi), moved by d.
function mgCfi(parts) {
	const cies = [], cieIx = new Map(), fdes = [], seen = new Set(), bases = new Map();
	let size = 0, bias;
	for (const [t] of parts)
		if (!bases.has(t)) {
			bases.set(t, size);
			size += t.prog.length;
		}
	const src = new Uint8Array(size);
	for (const [t, o] of bases)
		src.set(t.prog, o);
	for (const [t, a, b, d] of parts) {
		bias = bias === undefined ? t.bias : bias === t.bias ? bias : null;
		for (let i = 0; i < t.lo.length; i++) {
			if (t.lo[i] >= b || t.lo[i] + t.len[i] <= a || seen.has(t.lo[i] + d))
				continue;
			seen.add(t.lo[i] + d);
			const ck = bases.get(t) + ":" + t.cie[i];
			if (!cieIx.has(ck)) {
				cieIx.set(ck, cies.length);
				cies.push(t.cies[t.cie[i]]);
			}
			fdes.push([t.lo[i] + d, t.len[i], cieIx.get(ck), bases.get(t) + t.off[i], t.plen[i]]);
		}
	}
	fdes.sort((x, y) => x[0] - y[0]);
	return cfiPack(cies, fdes, src, bias === undefined ? null : bias);
}

// Adds to the types.json data out (CI's, moved; its arrays are copied
// before they change) the variables of O (the files made here) that out
// lacks at their address: their types (and the types those reach) are
// appended to out.types with new ids, their sections and files named in
// out's lists.
function mgAddVars(out, O) {
	const have = new Set(out.vars.map((v) => v[0] + "@" + v[1]));
	const add = O.vars.filter((v) => !have.has(v[0] + "@" + v[1]));
	out.types = out.types.slice();
	out.sections = (out.sections || []).slice();
	out.files = (out.files || []).slice();
	const ids = new Map();
	const id = (k) => {
		if (k < 0 || k === null || k === undefined || !O.types[k])
			return k;
		let n = ids.get(k);
		if (n !== undefined)
			return n;
		n = out.types.length;
		ids.set(k, n);
		const t = O.types[k], c = t.slice();
		out.types.push(c);
		switch (t[0]) {
		case DWT.ptr: case DWT.const: case DWT.volatile: case DWT.restrict: case DWT.atomic: case DWT.array:
			c[1] = id(t[1]);
			break;
		case DWT.typedef:
			c[2] = id(t[2]);
			break;
		case DWT.func:
			c[1] = id(t[1]);
			c[2] = t[2].map(id);
			break;
		case DWT.struct: case DWT.union:
			// [5]: a declaration's definition (dwarftypes.js finish).
			c[3] = t[3] && t[3].map((m) => { const e = m.slice(); e[2] = id(m[2]); return e; });
			c[5] = id(t[5]);
			break;
		case DWT.enum:
			c[4] = id(t[4]);
			c[6] = id(t[6]);
			break;
		}
		c.length = t.length;
		return n;
	};
	// Index of name in list, added when new.
	const index = (list, name) => (list.includes(name) ? list.indexOf(name) : list.push(name) - 1);
	const file = (f) => (f >= 0 && O.files && O.files[f] !== undefined ? index(out.files, O.files[f]) : -1);
	const sec = (s) => (O.sections && O.sections[s] !== undefined ? index(out.sections, O.sections[s]) : s);
	// [2] type, [4] section, [7] file, [9] other definitions' [file, line].
	const field = [null, null, id, null, sec, null, null, file, null, (d) => d.map(([f, line]) => [file(f), line])];
	for (const v of add) {
		const x = v.map((f, i) => (field[i] ? field[i](f) : f));
		out.vars.push(x);
		for (const key of ["shrunk", "pieces"]) {
			const e = O[key] && O[key].find((s) => s[0] === v[1]);
			if (e && (v[5] & (key === "shrunk" ? 8 : 16)))
				out[key] = (out[key] || []).concat([e.slice()]);
		}
	}
	return add.length;
}

class DbgMerge {
	// kfetch(url) -> Response; own: the directory of the files made here,
	// ship: the shipped entry ({ dwarf, symbols }); syms: the rebuilt
	// kernel's symbols.
	constructor(kfetch, own, ship, syms) {
		this.kfetch = kfetch;
		this.own = own;
		this.ship = ship.dwarf.replace(/\/?$/, "/");
		this.shipSymbols = ship.symbols;
		this.syms = syms;
		this.cache = new Map(); // "kind:key" of CI's files -> promise
		this.ownCache = new Map();
	}

	shipGet(kind, key) {
		const k = kind + ":" + key;
		if (!this.cache.has(k))
			this.cache.set(k, mgRead(this.kfetch, this.ship, this.shipIx, kind, key));
		return this.cache.get(k);
	}

	ownGet(kind, key) {
		const k = kind + ":" + key;
		if (!this.ownCache.has(k))
			this.ownCache.set(k, mgRead(this.kfetch, this.own, this.ownIx, kind, key));
		return this.ownCache.get(k);
	}

	// The composed index.json (dbginfo.js reads it like a shipped one).
	async open() {
		const [ownIx, shipIx, S] = await Promise.all([mgRead(this.kfetch, this.own, null, "index", 0), mgRead(this.kfetch, this.ship, null, "index", 0),
			this.kfetch(this.shipSymbols).then((r) => r.ok ? r.json() : Promise.reject(new Error(this.shipSymbols + ": HTTP " + r.status)))]);
		this.ownIx = ownIx;
		this.shipIx = shipIx;
		this.size = 2 ** shipIx.shardBits;
		if (ownIx.shardBits !== shipIx.shardBits)
			throw new Error("shards of " + 2 ** ownIx.shardBits + " and " + this.size + " bytes");
		const map = mgMap(this.syms, S.symbols);
		this.phys = map.phys;
		this.virt = map.virt;
		// Old -> new, sorted by the old addresses.
		this.physOld = this.phys.map(([a, b, d]) => [a - d, b - d, d]).sort((x, y) => x[0] - y[0]);
		this.virtOld = this.virt.map(([a, b, d]) => [a - d, b - d, d]).sort((x, y) => x[0] - y[0]);
		// The owned code, and the facts and types of the files made here.
		const tables = await Promise.all(ownIx.shards.map((s) => this.ownGet("line", s)));
		const R = [];
		for (const t of tables)
			for (let q = 0; q < t.ra.length; q++)
				if (t.rd[q] === 0)
					R.push([t.ra[q], t.rh[q]]);
		this.owned = mgUnion(R);
		this.ownedIv = this.owned.map(([a, b]) => [a, b, 0]);
		this.ownTables = new Map(ownIx.shards.map((s, i) => [s, tables[i]]));
		this.ownT = ownIx.types ? await this.ownGet("types", 0) : null;
		this.ownFacts = new Map();
		const nf = ownIx.funcs ? ownIx.funcs.shards : 0;
		for (const F of await Promise.all(Array.from({ length: nf }, (_, k) => this.ownGet("funcs", k))))
			for (const e of F.fns) {
				if (!this.ownFacts.has(e[0]))
					this.ownFacts.set(e[0], []);
				this.ownFacts.get(e[0]).push({ e, file: e[1] >= 0 ? F.files[e[1]] : null });
			}
		const keys = (own, ship) => [...new Set([...(own || []), ...this.newKeys(ship || [])])].sort((a, b) => a - b);
		const sc = shipIx.cfi && !shipIx.cfi.why ? shipIx.cfi : null, oc = ownIx.cfi && !ownIx.cfi.why ? ownIx.cfi : null;
		this.ownCfi = new Set(oc ? oc.shards : []);
		this.ownLocals = new Set(ownIx.locals ? ownIx.locals.shards : []);
		this.index = { ...shipIx, source: ownIx.source + " (the units compiled here) and CI's " + shipIx.source + " (the others, moved to their new addresses)",
			version: [...new Set([...ownIx.version, ...shipIx.version])], shards: keys(ownIx.shards, shipIx.shards),
			cfi: sc || oc ? { source: (sc || oc).source, fdes: (sc ? sc.fdes : 0), shards: keys(oc && oc.shards, sc && sc.shards) } : shipIx.cfi,
			locals: shipIx.locals ? { ...shipIx.locals, shards: keys(ownIx.locals && ownIx.locals.shards, shipIx.locals.shards) } : null,
			moved: { intervals: this.phys.length, owned: this.owned.length } };
		return this.index;
	}

	// New shard keys of the old shards keys (their code moved).
	newKeys(keys) {
		const out = new Set();
		for (const k of keys)
			for (const [a, b, d] of mgClip(this.physOld, k * this.size, (k + 1) * this.size))
				for (let s = Math.floor((a + d) / this.size); s <= Math.floor((b + d - 1) / this.size); s++)
					out.add(s);
		return [...out];
	}

	// Whether new address pa is in code compiled here.
	isOwned(pa) {
		const k = mgAt(this.owned, pa);
		return k >= 0 && pa < this.owned[k][1];
	}

	// New address of an old one (iv: physOld or virtOld), or null.
	moved(x, iv = this.physOld) {
		const k = mgAt(iv, x);
		return k >= 0 && x < iv[k][1] ? x + iv[k][2] : null;
	}

	// The parts of new [lo, hi): [[old table key, old a, old b, d], ...] for
	// CI's shards of kind (whose index list is keys) outside the owned code,
	// and [[own key, a, b, 0]] for the owned code in the files made here.
	parts(lo, hi, shipKeys, ownKeys) {
		const out = [];
		for (const [a, b, d] of mgClip(this.phys, lo, hi))
			for (const [x, y] of mgLess(this.owned, a, b))
				for (let k = Math.floor((x - d) / this.size); k <= Math.floor((y - d - 1) / this.size); k++)
					if (shipKeys.has(k))
						out.push([false, k, Math.max(x - d, k * this.size), Math.min(y - d, (k + 1) * this.size), d]);
		const s = Math.floor(lo / this.size);
		if (ownKeys.has(s))
			for (const [a, b] of mgClip(this.ownedIv, lo, hi))
				out.push([true, s, a, b, 0]);
		return out;
	}

	// What dbginfo.js would fetch: kind and key as mgRead's.
	get(kind, key) {
		if (kind === "index")
			return this.open();
		const lo = key * this.size, hi = lo + this.size;
		if (kind === "line") {
			this.lineKeys = this.lineKeys || new Set(this.shipIx.shards);
			return this.load("line", this.parts(lo, hi, this.lineKeys, new Set(this.ownTables.keys()))).then(mgLines);
		}
		if (kind === "cfi") {
			this.cfiKeys = this.cfiKeys || new Set(this.shipIx.cfi && this.shipIx.cfi.shards || []);
			return this.load("cfi", this.parts(lo, hi, this.cfiKeys, this.ownCfi)).then(mgCfi);
		}
		if (kind === "locals")
			return this.locals(lo, hi);
		if (kind === "ownLocals")
			return this.ownGet("locals", key);
		if (kind === "funcs")
			return this.shipGet("funcs", key).then((F) => this.facts(F));
		if (kind === "types")
			return this.shipGet("types", 0).then((T) => this.types(T));
		if (kind === "files")
			return this.files();
		return Promise.reject(new Error("no " + kind));
	}

	// The parts with their tables.
	async load(kind, parts) {
		return Promise.all(parts.map(async ([own, k, a, b, d]) => [await (own ? this.ownGet(kind, k) : this.shipGet(kind, k)), a, b, d]));
	}

	// A location (dwarflocals.js) of CI's moved: list ranges and DW_OP_addr.
	moveLoc(loc) {
		if (typeof loc === "string")
			return this.moveExpr(loc);
		if (!Array.isArray(loc))
			return loc;
		const out = [];
		for (const [lo, len, e] of loc) {
			const n = len ? this.moved(lo) : 0;
			if (n !== null)
				out.push([n, len, this.moveExpr(e)]);
		}
		return out;
	}

	moveExpr(hex) {
		if (!hex.includes("03"))
			return hex;
		const b = Uint8Array.from(hex.match(/../g) || [], (x) => parseInt(x, 16));
		for (let p = 0; p >= 0 && p < b.length; p = dwOpEnd(b, p))
			if (b[p] === 0x03 && p + 5 <= b.length) {
				const v = (b[p + 1] | b[p + 2] << 8 | b[p + 3] << 16 | b[p + 4] << 24) >>> 0;
				const n = this.moved(v, this.virtOld);
				if (n !== null)
					for (let i = 0; i < 4; i++)
						b[p + 1 + i] = n / 2 ** (8 * i) & 0xff;
			}
		return Array.from(b, (x) => (x < 16 ? "0" : "") + x.toString(16)).join("");
	}

	// Ranges [lo, len, ...] moved; null when a part did not move or is owned.
	moveRanges(r) {
		const out = [];
		for (let k = 0; k < r.length; k += 2) {
			const n = this.moved(r[k]);
			if (n === null || this.isOwned(n))
				return null;
			out.push(n, r[k + 1]);
		}
		return out;
	}

	// CI's scope groups of the functions whose code is in new [lo, hi), moved.
	async locals(lo, hi) {
		this.localKeys = this.localKeys || new Set(this.shipIx.locals ? this.shipIx.locals.shards : []);
		const parts = this.parts(lo, hi, this.localKeys, new Set());
		const fns = [], seen = new Set();
		for (const [L, a, b] of await this.load("locals", parts))
			for (const g of L.fns) {
				const r = g[0][2];
				let hit = false;
				for (let k = 0; k < r.length && !hit; k += 2)
					hit = r[k] < b && r[k] + r[k + 1] > a;
				const id = r[0] + ":" + g[0][3];
				if (!hit || seen.has(id))
					continue;
				seen.add(id);
				const m = this.moveGroup(g);
				if (m)
					fns.push(m);
			}
		return { format: 1, fns };
	}

	moveGroup(g) {
		const out = [];
		for (const [kind, parent, ranges, name, fb, vars] of g) {
			const r = this.moveRanges(ranges);
			if (!r)
				return null;
			out.push([kind, parent, r, name, fb === null ? null : this.moveLoc(fb), vars.map(([n, ty, fl, loc]) => [n, ty, fl, this.moveLoc(loc)])]);
		}
		return out;
	}

	// CI's function facts: out-of-line code and inlined copies moved (an
	// owned or vanished one dropped: the files made here have the owned).
	facts(F) {
		return { ...F, fns: F.fns.map((e) => {
			const x = e.slice();
			x[7] = e[7].map((r) => this.moveRanges(r)).filter(Boolean);
			x[8] = e[8].map((c) => this.moved(c)).filter((c) => c !== null && !this.isOwned(c));
			return x;
		}) };
	}

	// The facts of name in the files made here (owned code), or null.
	ownFactsOf(base) {
		return this.ownFacts.get(base) || null;
	}

	// CI's variables at their new addresses (one that did not move is not
	// in the rebuilt kernel where CI's DWARF says), and the variables of the
	// units compiled here that CI's list lacks there (a global the edit
	// added, or one whose size changed), their types copied into CI's table.
	types(T) {
		const vars = [];
		for (const v of T.vars) {
			const n = this.moved(v[1]);
			if (n !== null) {
				const x = v.slice();
				x[1] = n;
				vars.push(x);
			}
		}
		const out = { ...T, vars };
		for (const key of ["shrunk", "pieces"])
			if (T[key])
				out[key] = T[key].map(([pa, ...rest]) => [this.moved(pa), ...rest]).filter((x) => x[0] !== null);
		if (this.ownT)
			mgAddVars(out, this.ownT);
		vars.sort((a, b) => a[1] - b[1] || b[3] - a[3]);
		for (const key of ["shrunk", "pieces"])
			if (out[key])
				out[key].sort((a, b) => a[0] - b[0]);
		return out;
	}

	// files.json: CI's functions that moved whole and the owned ones of the
	// files made here; every file of both, its shards the new ones; lines
	// with code of a file both list: the larger count.
	async files() {
		const [S, O] = await Promise.all([this.shipGet("files", 0), this.ownGet("files", 0)]);
		const byPath = new Map();
		const add = (p, n, shards) => {
			if (!byPath.has(p))
				byPath.set(p, [0, new Set()]);
			const e = byPath.get(p);
			e[0] = Math.max(e[0], n);
			for (const s of shards)
				e[1].add(s);
		};
		for (const [p, n, shards] of S.files)
			add(p, n, this.newKeys(shards));
		for (const [p, n, shards] of O.files)
			add(p, n, shards);
		const paths = [...byPath.keys()].sort();
		const ix = new Map(paths.map((p, k) => [p, k]));
		const funcs = [];
		for (const f of S.funcs) {
			const r = this.moveRanges(f.slice(4));
			if (r)
				funcs.push([f[0], f[1] >= 0 ? ix.get(S.files[f[1]][0]) : -1, f[2], f[3], ...r]);
		}
		for (const f of O.funcs)
			funcs.push([f[0], f[1] >= 0 ? ix.get(O.files[f[1]][0]) : -1, ...f.slice(2)]);
		return { ...S, files: paths.map((p) => [p, byPath.get(p)[0], [...byPath.get(p)[1]].sort((a, b) => a - b)]), funcs };
	}
}

export { dbgGunzip, mgRead, mgMap, mgLines, mgCfi, DbgMerge };
