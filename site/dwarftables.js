"use strict";

// DWARF tables (dwarf.js): the ELF's sections, the line and inline
// tables lookups read, and their shard files.

import { u32le, u16le, cstrAt, textDecoder } from "./dwarfread.js";

// ---- ELF -----------------------------------------------------------------

// PT_LOAD segments and sections of an ELF32 little-endian file, through
// read(off, len) -> Uint8Array (a File slice in the Worker, fs in node).
function dwarfElf(read) {
	const h = read(0, 52);
	if (u32le(h, 0) !== 0x464c457f)
		throw new Error("not an ELF file");
	if (h[4] !== 1 || h[5] !== 1)
		throw new Error("not a 32-bit little-endian ELF (only i386 kernels boot here)");
	const phoff = u32le(h, 0x1c), shoff = u32le(h, 0x20);
	const phentsize = u16le(h, 0x2a), phnum = u16le(h, 0x2c), shentsize = u16le(h, 0x2e), shnum = u16le(h, 0x30);
	const shstrndx = u16le(h, 0x32);
	const ph = read(phoff, phentsize * phnum);
	const loads = [];
	for (let i = 0; i < phnum; i++) {
		const o = i * phentsize;
		if (u32le(ph, o) === 1 && u32le(ph, o + 20))
			loads.push([u32le(ph, o + 8), u32le(ph, o + 8) + u32le(ph, o + 20), u32le(ph, o + 12)]);
	}
	const sh = read(shoff, shentsize * shnum);
	const secs = [];
	for (let i = 0; i < shnum; i++) {
		const o = i * shentsize;
		secs.push({ name: u32le(sh, o), type: u32le(sh, o + 4), flags: u32le(sh, o + 8), addr: u32le(sh, o + 12),
			offset: u32le(sh, o + 16), size: u32le(sh, o + 20) });
	}
	const strs = secs[shstrndx] ? read(secs[shstrndx].offset, secs[shstrndx].size) : new Uint8Array(0);
	const sections = {};
	const alloc = []; // [name, addr, end] of the sections in memory (SHF_ALLOC), .bss included
	for (const s of secs) {
		const name = cstrAt(strs, s.name);
		if (s.type !== 8) // SHT_NOBITS
			sections[name] = s;
		if (s.flags & 2 && s.size)
			alloc.push([name, s.addr, s.addr + s.size]);
	}
	// Virtual -> physical through the PT_LOAD segments; null outside them.
	const toPhys = (va) => {
		for (const [lo, hi, pa] of loads)
			if (va >= lo && va < hi)
				return va - lo + pa;
		return null;
	};
	return { loads, sections, alloc, toPhys };
}

// For DwarfTypes.addUnit: virtual address -> [index in names, per-CPU],
// names collecting the section names met.
function dwarfSectionOf(elf, names) {
	const ix = new Map();
	return (va) => {
		const s = elf.alloc.find(([, lo, hi]) => va >= lo && va < hi);
		const n = s ? s[0] : "?";
		if (!ix.has(n)) {
			ix.set(n, names.length);
			names.push(n);
		}
		return [ix.get(n), n === ".data..percpu"];
	};
}

// The DwarfReader input for an ELF: small sections read whole, .debug_info
// and .debug_line read a unit or a line program at a time. null when the
// ELF has no .debug_info.
function dwarfSections(elf, read) {
	const s = elf.sections;
	const whole = (n) => s[n] && s[n].size ? read(s[n].offset, s[n].size) : null;
	const part = (n) => s[n] ? (off, len) => read(s[n].offset + off, Math.max(0, Math.min(len, s[n].size - off))) : null;
	if (!s[".debug_info"] || !s[".debug_info"].size)
		return null;
	return {
		readInfo: part(".debug_info"), infoSize: s[".debug_info"].size,
		readLine: part(".debug_line"), lineSize: s[".debug_line"] ? s[".debug_line"].size : 0,
		abbrev: whole(".debug_abbrev"), str: whole(".debug_str"), line_str: whole(".debug_line_str"),
		str_offsets: whole(".debug_str_offsets"), addr: whole(".debug_addr"), rnglists: whole(".debug_rnglists"),
		ranges: whole(".debug_ranges"), aranges: whole(".debug_aranges"),
		// Location lists (18.6 MB for a distro kernel): read when a
		// variable's location is first asked for.
		loclists: () => whole(".debug_loclists"), loc: () => whole(".debug_loc"),
	};
}

// ---- tables: what lookups read ------------------------------------------
//
// A table covers some address range: sorted line rows (addr, file string
// index or -1 for "no line information", line) and function/inlined-call
// ranges (lo, hi, depth, name, call file, call line). Addresses are
// physical (what the page works in). Built from decoded units by
// dwarfTable(), stored as shards by gen-dwarf.js (dwarfEncode), read back
// by dwarfDecode.

// units: [{ files, rows, ranges }] with addresses mapped by toPhys (null
// drops the row or range: code outside the loaded image, e.g. discarded
// sections at 0).
function dwarfTable(units, toPhys, strings = [], strIdx = new Map()) {
	const sid = (s) => {
		if (s === null || s === undefined || s === "")
			return -1;
		let k = strIdx.get(s);
		if (k === undefined) {
			k = strings.length;
			strings.push(s);
			strIdx.set(s, k);
		}
		return k;
	};
	let n = 0;
	for (const u of units)
		n += u.rows.length / 4 + (u.cover ? u.cover.length : 0);
	// Rows: addr, file, line, kind (0 end of sequence first at equal
	// addresses, then the rows in program order: the last one wins),
	// is_stmt.
	let ra = new Float64Array(n), rf = new Int32Array(n), rl = new Uint32Array(n), rk = new Float64Array(n), rs = new Uint8Array(n);
	let m = 0, order = 0;
	for (const u of units) {
		const fid = u.files.map(sid);
		// Line rows count only inside the unit's own address ranges (what
		// llvm-symbolizer looks up first): an end marker closes each range,
		// rows outside them are dropped. A sequence that starts outside the
		// image (discarded code at 0) stays out.
		const cover = [];
		for (const [lo, hi] of (u.cover || []).slice().sort((x, y) => x[0] - y[0]))
			if (cover.length && lo <= cover[cover.length - 1][1])
				cover[cover.length - 1][1] = Math.max(hi, cover[cover.length - 1][1]);
			else
				cover.push([lo, hi]);
		const inCover = (va) => {
			if (!u.cover)
				return true;
			let a = 0, b = cover.length;
			while (a < b) { const q = (a + b) >> 1; if (cover[q][0] <= va) a = q + 1; else b = q; }
			return a > 0 && va < cover[a - 1][1];
		};
		const rows = u.rows.slice();
		for (const [, e] of cover)
			rows.push(e, 0, 0, 1);
		let start = true, ok = true;
		for (let k = 0; k < rows.length; k += 4) {
			const end = rows[k + 3] === 1;
			if (start)
				ok = toPhys(rows[k]) !== null;
			start = end;
			if (!end && !inCover(rows[k]))
				continue;
			const pa = !ok ? null : end ? (toPhys(rows[k] - 1) ?? -1) + 1 : toPhys(rows[k]);
			if (!pa)
				continue;
			ra[m] = pa;
			rf[m] = end ? -2 : fid[rows[k + 1]] ?? -1;
			rl[m] = end ? 0 : rows[k + 2];
			rk[m] = end ? 0 : 1 + order++;
			rs[m] = rows[k + 3] === 2 ? 1 : 0;
			m++;
		}
	}
	const idx = new Uint32Array(m);
	for (let k = 0; k < m; k++)
		idx[k] = k;
	idx.sort((x, y) => ra[x] - ra[y] || rk[x] - rk[y]);
	// One row per address (the last one there wins), then one per run of
	// the same file:line. Breakpoint addresses of a line: each is_stmt row
	// that does not continue an is_stmt row of the same line in the same
	// sequence (one more statement on that line is not a new place to
	// stop); lk marks a run whose first address is one, sx holds the others.
	const ua = [], uf = [], ul = [], us = [], ue = [];
	for (let q = 0; q < m; q++) {
		const k = idx[q];
		const a = ra[k], f = rf[k] === -2 ? -1 : rf[k], l = rf[k] === -2 ? 0 : rl[k];
		const last = ua.length - 1;
		if (last >= 0 && ua[last] === a) {
			uf[last] = f; ul[last] = l; us[last] = rs[k];
			continue;
		}
		ua.push(a); uf.push(f); ul.push(l); us.push(rs[k]); ue.push(rf[k] === -2 ? 1 : 0); // ue: a sequence ends here
	}
	ra = rf = rl = rk = rs = null;
	const la = [], lf = [], ll = [], lk = [], sx = [];
	for (let q = 0; q < ua.length; q++) {
		const f = uf[q], l = ul[q];
		const stmt = us[q] && f >= 0 && l > 0 && !(q > 0 && !ue[q] && us[q - 1] && uf[q - 1] === f && ul[q - 1] === l);
		const last = la.length - 1;
		if (last >= 0 && lf[last] === f && ll[last] === l) {
			if (stmt)
				sx.push(ua[q]);
			continue;
		}
		la.push(ua[q]); lf.push(f); ll.push(l); lk.push(stmt ? 1 : 0);
	}
	const rg = [];
	for (const u of units) {
		const fid = u.files;
		const r = u.ranges;
		for (let k = 0; k < r.length; k += 6) {
			const lo = toPhys(r[k]);
			if (lo === null)
				continue;
			rg.push([lo, lo + (r[k + 1] - r[k]), r[k + 2], sid(r[k + 3] || "?"), r[k + 4] >= 0 ? sid(fid[r[k + 4]]) : -1, r[k + 5]]);
		}
	}
	rg.sort((x, y) => x[0] - y[0] || x[2] - y[2]);
	return dwarfPack(strings, la, lf, ll, rg, lk, sx);
}

// lk: 1 where a line row's address is a breakpoint address (is_stmt), sx:
// the breakpoint addresses inside rows (sorted). Tables decoded from a
// version 1 shard or remembered from before have neither: every row start
// counts then.
function dwarfPack(strings, la, lf, ll, rg, lk, sx) {
	const t = { strings, la: Float64Array.from(la), lf: Int32Array.from(lf), ll: Uint32Array.from(ll),
		lk: Uint8Array.from(lk), sx: Float64Array.from(sx),
		ra: new Float64Array(rg.length), rh: new Float64Array(rg.length), rd: new Uint8Array(rg.length),
		rn: new Int32Array(rg.length), rf: new Int32Array(rg.length), rl: new Uint32Array(rg.length), maxLen: 0 };
	rg.forEach((x, k) => {
		t.ra[k] = x[0]; t.rh[k] = x[1]; t.rd[k] = Math.min(255, x[2]); t.rn[k] = x[3]; t.rf[k] = x[4]; t.rl[k] = x[5];
		t.maxLen = Math.max(t.maxLen, x[1] - x[0]);
	});
	return t;
}

// Index of the line row in force at pa, or -1.
function dwarfRow(t, pa) {
	let a = 0, b = t.la.length;
	while (a < b) { const m = (a + b) >> 1; if (t.la[m] <= pa) a = m + 1; else b = m; }
	return a - 1;
}

// Frames at physical address pa, innermost first: [{ func, file, line }]
// (func null: no DWARF function here, e.g. assembly; file null: no line
// information), or null when the table knows nothing about pa.
function dwarfFrames(t, pa) {
	const k = dwarfRow(t, pa);
	const file = k >= 0 && t.lf[k] >= 0 ? t.strings[t.lf[k]] : null;
	const line = k >= 0 && t.lf[k] >= 0 ? t.ll[k] : 0;
	// Ranges covering pa, the innermost (latest start) per depth.
	let a = 0, b = t.ra.length;
	while (a < b) { const m = (a + b) >> 1; if (t.ra[m] <= pa) a = m + 1; else b = m; }
	const byDepth = [];
	for (let q = a - 1; q >= 0 && t.ra[q] >= pa - t.maxLen; q--)
		if (t.rh[q] > pa && byDepth[t.rd[q]] === undefined)
			byDepth[t.rd[q]] = q;
	let deepest = -1;
	while (byDepth[deepest + 1] !== undefined)
		deepest++;
	if (deepest < 0)
		return k >= 0 && file !== null ? [{ func: null, file, line }] : null;
	const frames = [];
	for (let d = deepest; d >= 0; d--) {
		const q = byDepth[d];
		const func = t.strings[t.rn[q]] || null;
		if (d === deepest)
			frames.push({ func, file, line });
		else {
			const inner = byDepth[d + 1];
			frames.push({ func, file: t.rf[inner] >= 0 ? t.strings[t.rf[inner]] : null, line: t.rf[inner] >= 0 ? t.rl[inner] : 0 });
		}
	}
	return frames;
}

// fn(a, b, frames) for each line row of table t in [lo, hi) that has a
// file:line, clipped to [lo, hi); frames as dwarfFrames at a. What has
// code and what ran (the page's coverage, gen-dwarf.js's totals) is
// counted per row like this: every frame with a line, so the call line of
// inlined code counts as well as the inlined line.
function dwarfEachLine(t, lo, hi, fn) {
	for (let k = Math.max(0, dwarfRow(t, lo)); k < t.la.length && t.la[k] < hi; k++) {
		if (t.lf[k] < 0)
			continue;
		const a = Math.max(lo, t.la[k]), b = Math.min(hi, k + 1 < t.la.length ? t.la[k + 1] : hi);
		const frames = b > a ? dwarfFrames(t, a) : null;
		if (frames)
			fn(a, b, frames);
	}
}

// The code of file `path` in table t within [lo, hi), per line: Map line
// -> { rows: [[a, b, breakpoint addresses (is_stmt) in [a, b)], ...], the
// line rows naming it, calls: [[a, b, name, depth], ...], the ranges of
// functions inlined by a call on it }. A table without is_stmt data (an
// older shard, a unit kept from before) gives each row's start as its
// breakpoint address and says so (noStmt).
function dwarfFileAddrs(t, path, lo = 0, hi = Infinity, out = new Map()) {
	const fi = new Set();
	t.strings.forEach((s, k) => { if (s === path) fi.add(k); });
	if (!fi.size)
		return out;
	const noStmt = !t.lk || !!t.noStmt;
	const at = (l) => {
		if (!out.has(l))
			out.set(l, { rows: [], calls: [], noStmt: false });
		return out.get(l);
	};
	for (let k = Math.max(0, dwarfRow(t, lo)); k < t.la.length && t.la[k] < hi; k++) {
		if (!fi.has(t.lf[k]) || !t.ll[k])
			continue;
		const a = Math.max(lo, t.la[k]), b = Math.min(hi, k + 1 < t.la.length ? t.la[k + 1] : hi);
		if (!(b > a))
			continue;
		const bp = [];
		if (noStmt) {
			if (t.la[k] >= lo)
				bp.push(a);
		} else {
			if (t.lk[k] && t.la[k] >= lo)
				bp.push(a);
			let x = 0, y = t.sx.length;
			while (x < y) { const q = (x + y) >> 1; if (t.sx[q] < a) x = q + 1; else y = q; }
			for (; x < t.sx.length && t.sx[x] < b; x++)
				if (t.sx[x] > a || !bp.length)
					bp.push(t.sx[x]);
		}
		const e = at(t.ll[k]);
		e.rows.push([a, b, bp]);
		e.noStmt = e.noStmt || noStmt;
	}
	for (let q = 0; q < t.ra.length; q++)
		if (fi.has(t.rf[q]) && t.rl[q] && t.ra[q] < hi && t.rh[q] > lo)
			at(t.rl[q]).calls.push([t.ra[q], t.rh[q], t.strings[t.rn[q]] || null, t.rd[q]]);
	return out;
}

// The code of file `path` over several tables (address shards, or compile
// units with lo 0 and hi Infinity), any iterable of [lo, hi, t], per line:
// Map line -> { rows, calls, bps, noStmt }. Rows are merged where one goes
// on in the next shard, each inlined call is listed once, and bps are the
// addresses a "Run to path:line" stops at: every breakpoint address of
// its rows plus the start of every inlined call's range (code inlined by
// a call on the line runs that line, and its first row usually names the
// inlined function's line, not this one).
function dwarfFileCode(tables, path) {
	const acc = new Map();
	for (const [lo, hi, t] of tables)
		dwarfFileAddrs(t, path, lo, hi, acc);
	const out = new Map();
	for (const [line, e] of acc) {
		e.rows.sort((x, y) => x[0] - y[0]);
		const rows = [];
		for (const r of e.rows) {
			const last = rows[rows.length - 1];
			if (last && last[1] === r[0])
				last[1] = r[1], last[2] = last[2].concat(r[2]);
			else
				rows.push([r[0], r[1], r[2].slice()]);
		}
		const seen = new Map();
		for (const c of e.calls)
			seen.set(c.join("\0"), c);
		const calls = [...seen.values()].sort((x, y) => x[0] - y[0] || x[3] - y[3]);
		const bps = [...new Set([...rows.flatMap((r) => r[2]), ...calls.map((c) => c[0])])].sort((x, y) => x - y);
		out.set(line, { rows, calls, bps, noStmt: e.noStmt });
	}
	return out;
}

// The file a user typed ("init/main.c", "main.c") among `paths`: { path }
// for an exact name or a single one ending in "/" + it, else { why }.
function dwarfResolvePath(paths, q, none) {
	q = q.replace(/^\.?\//, "");
	if (paths.includes(q))
		return { path: q };
	const hits = paths.filter((p) => p.endsWith("/" + q));
	if (hits.length === 1)
		return { path: hits[0] };
	return { why: hits.length ? q + " is ambiguous: " + hits.slice(0, 5).join(", ") + (hits.length > 5 ? ", …" : "") :
		none || "no source file " + q + " with code in this kernel's line table" };
}

// [lo, hi) of the outermost function (depth 0) covering pa, or null.
function dwarfFunction(t, pa) {
	let a = 0, b = t.ra.length;
	while (a < b) { const m = (a + b) >> 1; if (t.ra[m] <= pa) a = m + 1; else b = m; }
	for (let q = a - 1; q >= 0 && t.ra[q] >= pa - t.maxLen; q--)
		if (t.rd[q] === 0 && t.rh[q] > pa)
			return [t.ra[q], t.rh[q]];
	return null;
}

// ---- shard files ----------------------------------------------------------
// Written by gen-dwarf.js (dwarfSlice, dwarfEncode; the page only reads
// them): "MKBD" 2, strings (count, then NUL-terminated UTF-8), line rows (count,
// then address delta, file+1 zigzag delta * 2 + lk, line zigzag delta),
// ranges (count, then start delta, length, depth, name+1, call file+1,
// call line), breakpoint addresses inside rows (count, then address
// deltas), all unsigned LEB128. Version 1 had no lk bit and no sx.

function dwarfDecode(b) {
	const ver = b[4];
	if (b[0] !== 0x4d || b[1] !== 0x4b || b[2] !== 0x42 || b[3] !== 0x44 || (ver !== 1 && ver !== 2))
		throw new Error("not a DWARF shard (MKBD 1 or 2)");
	let p = 5;
	const uv = () => { let r = 0, s = 0, x; do { x = b[p++]; r += (x & 0x7f) * 2 ** s; s += 7; } while (x & 0x80); return r; };
	const unzig = (v) => v % 2 ? -(v + 1) / 2 : v / 2;
	const zz = () => unzig(uv());
	const strings = [];
	for (let n = uv(); n > 0; n--) {
		let e = p;
		while (b[e]) e++;
		strings.push(textDecoder.decode(b.subarray(p, e)));
		p = e + 1;
	}
	const nl = uv();
	const la = new Float64Array(nl), lf = new Int32Array(nl), ll = new Uint32Array(nl), lk = new Uint8Array(nl);
	let pa = 0, pf = 0, pl = 0;
	for (let k = 0; k < nl; k++) {
		pa += uv(); la[k] = pa;
		if (ver === 1)
			pf += zz();
		else {
			const v = uv();
			lk[k] = v % 2;
			pf += unzig(Math.floor(v / 2));
		}
		lf[k] = pf - 1;
		pl += zz(); ll[k] = pl;
	}
	const nr = uv();
	const t = { strings, la, lf, ll, lk, sx: null, ra: new Float64Array(nr), rh: new Float64Array(nr), rd: new Uint8Array(nr),
		rn: new Int32Array(nr), rf: new Int32Array(nr), rl: new Uint32Array(nr), maxLen: 0 };
	pa = 0;
	for (let k = 0; k < nr; k++) {
		pa += uv(); t.ra[k] = pa;
		t.rh[k] = pa + uv(); t.rd[k] = uv(); t.rn[k] = uv() - 1; t.rf[k] = uv() - 1; t.rl[k] = uv();
		t.maxLen = Math.max(t.maxLen, t.rh[k] - pa);
	}
	const ns = ver === 1 ? 0 : uv();
	t.sx = new Float64Array(ns);
	pa = 0;
	for (let k = 0; k < ns; k++) {
		pa += uv(); t.sx[k] = pa;
	}
	if (ver === 1)
		t.noStmt = true;
	return t;
}

export { dwarfElf, dwarfSectionOf, dwarfSections, dwarfTable, dwarfPack, dwarfRow, dwarfFrames, dwarfEachLine,
	dwarfFileCode, dwarfResolvePath, dwarfFunction, dwarfDecode };
