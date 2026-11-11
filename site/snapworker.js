"use strict";
// Snapshot work off the page's main thread: compression (C1), diffs (S1)
// and delta snapshots (R1). Snapshots are v86 save_state() output as an
// LZ4 frame (lz4frame.js: `lz4 -d` reads a download), or gzip for those
// stored before C1, or raw. Answers:
//   { op: "pack", id, raw, base }  { data, ms, hashes, rawLen, full, delta }:
//                                raw (an ArrayBuffer, transferred) as an LZ4
//                                frame; with base ({ key, hashes }) a delta
//                                of it when that is smaller (below)
//   { op: "unpack", id, s, hashes } { raw, ms, hashes }: snapshot s as a
//                                full v86 state (a delta: rebuilt from its
//                                chain), with its page hashes if asked
//   { op: "export", id, s }      { data }: the full state of s as an LZ4 frame
//   { op: "rebase", id, s, base } { data, rawLen, full, delta }: s again, as a
//                                delta of base (a snapshot) or full (null)
//   { op: "diff", id, a, b }     changed 4 KiB pages ([page, bytes changed]),
//                                how many pages were compared and the CPU
//                                fields of both sides
//   { op: "pages", id, a, b, p } page p of both sides (two 4096-byte arrays)
// where s, a and b are { key, blob, codec, size, chain } (a stored
// snapshot: codec "lz4", "gzip" or "raw", size = the stored state's length,
// chain = for a delta its bases, the full one first) or
// { key: "now", raw: ArrayBuffer } (a save_state() taken for the diff,
// sent once per diff). Diffs keep the last few snapshots decoded.
//
// v86 0.5.471's state layout (src/state.js save_state): a 16-byte header of
// four int32 (magic 0x86768676, version 6, total length, info block
// length), the info block (UTF-8 JSON { buffer_infos: [{ offset, length }],
// state }), then the buffers from the next 4-byte boundary, each at its
// offset (aligned to 4). In state, a typed array is
// { __state_type__: "Int32Array", buffer_id: n }. The CPU's state
// (src/cpu.js get_state): [0] RAM size, [2] segment bases (Int32Array, CS
// at 1), [10] CR0..CR7, [26] flags (arithmetic flags may be lazy: [27]
// flags_changed), [37] instruction_pointer (linear EIP), [39] the eight
// general registers (EAX ECX EDX EBX ESP EBP ESI EDI), [40] the segment
// selectors (ES CS SS DS FS GS), [77] the non-zero pages packed in order,
// [78] the bitmap of which pages they are (bit p & 7 of byte p >> 3).
//
// A delta snapshot (R1) is the same layout with the info block's
// "mkb_delta_of" set to its base's id: [78] marks the pages that differ
// from the base (a page that became zero included) and [77] holds them;
// every other buffer is v86's own (CPU, devices, VGA). Which pages differ
// comes from two 32-bit hashes per page (FNV-1a and a multiply-xorshift
// over the page's 1,024 words) against the base's. Its RAM is the base's
// RAM with those pages on top; restored, diffed or downloaded, a delta is
// rebuilt into a full state (v86's [77] and [78] again), which v86 reads.

importScripts("lz4frame.js" + self.location.search);
const lz4Ready = () => lz4Init("lz4/lz4_wasm_bg.wasm" + self.location.search);

const PAGE = 4096;
const STATE_MAGIC = 0x86768676 | 0;
const KEEP = 2; // decoded snapshots kept besides "now" (64 MiB each for Debian)
const decoded = new Map(); // key -> { ram: Uint8Array, cpu, base, changed }
const DELTA_OF = "mkb_delta_of";

async function gunzip(blob) {
	if (typeof DecompressionStream !== "function")
		throw new Error("this browser has no DecompressionStream to read a gzip snapshot");
	const s = (blob instanceof Blob ? blob : new Blob([blob])).stream().pipeThrough(new DecompressionStream("gzip"));
	return new Response(s).arrayBuffer();
}

async function pack(raw) {
	await lz4Ready();
	return lz4FramePack(new Uint8Array(raw));
}

async function unpack(s) {
	const blob = s.blob instanceof Blob ? s.blob : new Blob([s.blob]);
	if (s.codec === "gzip")
		return gunzip(blob);
	if (s.codec !== "lz4")
		return blob.arrayBuffer();
	await lz4Ready();
	return lz4FrameUnpack(new Uint8Array(await blob.arrayBuffer()), s.size);
}

// The state's header, info block and buffers, without copying RAM.
function parseRaw(buf) {
	const head = new Int32Array(buf, 0, 4);
	if (head[0] !== STATE_MAGIC)
		throw new Error("not a v86 state (magic " + (head[0] >>> 0).toString(16) + ")");
	if (head[1] !== 6)
		throw new Error("v86 state version " + head[1] + ", this page reads version 6");
	const infoLen = head[3];
	const info = JSON.parse(new TextDecoder().decode(new Uint8Array(buf, 16, infoLen)));
	const start = (16 + infoLen + 3) & ~3;
	const bytes = (o) => {
		const bi = info.buffer_infos[o.buffer_id];
		return new Uint8Array(buf, start + bi.offset, bi.length);
	};
	const st = info.state;
	return { buf, info, start, st, bytes, size: st[0] >>> 0, packed: bytes(st[77]), bitmap: bytes(st[78]),
		deltaOf: info[DELTA_OF] || null };
}

const bit = (bm, p) => (bm[p >> 3] >> (p & 7)) & 1;

function cpuOf(P) {
	const words = (o, T) => new T(P.bytes(o).slice().buffer);
	const st = P.st;
	const reg = words(st[39], Int32Array), seg = words(st[2], Int32Array), sreg = words(st[40], Uint16Array);
	const cr = words(st[10], Int32Array);
	const lin = st[37] >>> 0;
	const names = ["EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI"];
	const regs = {};
	names.forEach((n, i) => (regs[n] = reg[i] >>> 0));
	regs.EIP = (lin - seg[1]) >>> 0;
	// EFLAGS: exact only when no arithmetic flag is pending (the page adds
	// the value read with get_eflags() at save time otherwise).
	if (!st[27])
		regs.EFLAGS = st[26] >>> 0;
	["ES", "CS", "SS", "DS", "FS", "GS"].forEach((n, i) => (regs[n] = sreg[i]));
	for (const i of [0, 2, 3, 4])
		regs["CR" + i] = cr[i] >>> 0;
	return { regs, linear: lin, ramSize: P.size };
}

// The pages [77] holds, in order ([78]'s set bits).
function markedPages(P) {
	const out = [];
	for (let p = 0; p < P.size / PAGE; p++)
		if (bit(P.bitmap, p))
			out.push(p);
	return out;
}

// Copy [77]'s pages to where [78] says: a full state's RAM (into zeros) or
// a delta's pages on top of its base's RAM.
function applyPages(ram, P) {
	const list = markedPages(P);
	list.forEach((p, k) => ram.set(P.packed.subarray(k * PAGE, (k + 1) * PAGE), p * PAGE));
	return list;
}

function parseState(buf) {
	const P = parseRaw(buf);
	if (P.deltaOf)
		throw new Error("a delta snapshot read without its base");
	const ram = new Uint8Array(P.size);
	applyPages(ram, P);
	return { ram, cpu: cpuOf(P) };
}

// ---- page hashes (R1) -----------------------------------------------------

function hashPage(w, o, out, j) {
	let h1 = 0x811c9dc5 | 0, h2 = 0x9e3779b9 | 0;
	for (let i = o, e = o + PAGE / 4; i < e; i++) {
		const x = w[i];
		h1 = Math.imul(h1 ^ x, 0x01000193);
		h2 = Math.imul(h2 + x, 0x85ebca6b);
		h2 ^= h2 >>> 13;
	}
	out[j] = h1;
	out[j + 1] = h2;
}

const ZERO_HASH = new Uint32Array(2);
hashPage(new Int32Array(PAGE / 4), 0, ZERO_HASH, 0);

const words32 = (u8) => new Int32Array(u8.buffer, u8.byteOffset, u8.length >> 2);

// Two 32-bit hashes per page of a full state (zero pages: ZERO_HASH).
function hashState(P) {
	const n = P.size / PAGE, out = new Uint32Array(2 * n), w = words32(P.packed);
	let k = 0;
	for (let p = 0; p < n; p++) {
		if (bit(P.bitmap, p))
			hashPage(w, (k++) * PAGE / 4, out, 2 * p);
		else
			out.set(ZERO_HASH, 2 * p);
	}
	return out;
}

function hashRam(ram) {
	const n = ram.length / PAGE, out = new Uint32Array(2 * n), w = words32(ram);
	for (let p = 0; p < n; p++)
		hashPage(w, p * PAGE / 4, out, 2 * p);
	return out;
}

// ---- building states ------------------------------------------------------

// The layout of P's state with [77] and [78] replaced by buffers of these
// lengths (v86's save_state layout): the info block and the total length.
function layout(P, len77, len78, deltaOf) {
	const id77 = P.st[77].buffer_id, id78 = P.st[78].buffer_id;
	const infos = [];
	let total = 0;
	P.info.buffer_infos.forEach((bi, i) => {
		const length = i === id77 ? len77 : i === id78 ? len78 : bi.length;
		infos.push({ offset: total, length });
		total = (total + length + 3) & ~3;
	});
	const obj = { buffer_infos: infos, state: P.st };
	if (deltaOf)
		obj[DELTA_OF] = deltaOf;
	const infoBytes = new TextEncoder().encode(JSON.stringify(obj));
	const start = (16 + infoBytes.length + 3) & ~3;
	return { infos, infoBytes, start, length: start + total };
}

// P's state with [77] and [78] replaced.
function serialize(P, packed, bitmap, deltaOf) {
	const id77 = P.st[77].buffer_id, id78 = P.st[78].buffer_id;
	const bufs = P.info.buffer_infos.map((bi, i) => (i === id77 ? packed : i === id78 ? bitmap :
		new Uint8Array(P.buf, P.start + bi.offset, bi.length)));
	const { infos, infoBytes, start, length } = layout(P, packed.length, bitmap.length, deltaOf);
	const out = new ArrayBuffer(length);
	new Int32Array(out, 0, 4).set([STATE_MAGIC, 6, out.byteLength, infoBytes.length]);
	const u8 = new Uint8Array(out);
	u8.set(infoBytes, 16);
	bufs.forEach((b, i) => u8.set(b, start + infos[i].offset));
	return out;
}

// The pages in list (sorted) of ram, and the bitmap marking them.
function gather(ram, list) {
	const bitmap = new Uint8Array(((ram.length / PAGE) + 7) >> 3);
	const packed = new Uint8Array(list.length * PAGE);
	list.forEach((p, k) => {
		bitmap[p >> 3] |= 1 << (p & 7);
		packed.set(ram.subarray(p * PAGE, (p + 1) * PAGE), k * PAGE);
	});
	return { packed, bitmap };
}

function nonzeroPages(ram) {
	const w = words32(ram), out = [];
	for (let p = 0; p < ram.length / PAGE; p++) {
		for (let i = p * PAGE / 4, e = i + PAGE / 4; i < e; i++)
			if (w[i]) {
				out.push(p);
				break;
			}
	}
	return out;
}

// RAM of s: its chain's full state, then each delta's pages on top. The
// nearest decoded snapshot of the chain stands in for the ones before it.
async function chainRam(s) {
	const list = [...(s.chain || []), s];
	let i = list.length - 1;
	while (i >= 0 && !decoded.has(list[i].key))
		i--;
	let ram;
	if (i >= 0) {
		ram = decoded.get(list[i].key).ram.slice();
	} else {
		const P = parseRaw(await unpack(list[0]));
		if (P.deltaOf)
			throw new Error("the snapshot this delta was taken against is missing");
		ram = new Uint8Array(P.size);
		applyPages(ram, P);
		i = 0;
	}
	for (let j = i + 1; j < list.length; j++) {
		const P = parseRaw(await unpack(list[j]));
		if (P.deltaOf !== list[j - 1].key)
			throw new Error("snapshot " + list[j].key + " is not a delta of " + list[j - 1].key);
		applyPages(ram, P);
	}
	return ram;
}

// s as a full v86 state ({ raw, ram } with the RAM when rebuilt).
async function fullState(s) {
	if (!s.chain || !s.chain.length)
		return { raw: await unpack(s) };
	const ram = await chainRam(s);
	const P = parseRaw(await unpack(s));
	const { packed, bitmap } = gather(ram, nonzeroPages(ram));
	return { raw: serialize(P, packed, bitmap, null), ram };
}

// The new state P as a delta of the page hashes base, or null when the
// delta would hold more pages than the full state (its non-zero pages).
function deltaOf(P, hashes, base) {
	if (!base || !base.hashes || base.hashes.length !== hashes.length)
		return null;
	const b = base.hashes, changed = [];
	for (let p = 0; p < hashes.length / 2; p++)
		if (hashes[2 * p] !== b[2 * p] || hashes[2 * p + 1] !== b[2 * p + 1])
			changed.push(p);
	const full = P.packed.length / PAGE;
	if (changed.length > full)
		return null;
	// The changed pages' bytes: from [77] where [78] has them, zero otherwise.
	const packed = new Uint8Array(changed.length * PAGE);
	const bitmap = new Uint8Array(P.bitmap.length);
	let k = 0, c = 0;
	for (let p = 0; p < P.size / PAGE && c < changed.length; p++) {
		const here = bit(P.bitmap, p);
		if (p === changed[c]) {
			bitmap[p >> 3] |= 1 << (p & 7);
			if (here)
				packed.set(P.packed.subarray(k * PAGE, (k + 1) * PAGE), c * PAGE);
			c++;
		}
		k += here;
	}
	return { raw: serialize(P, packed, bitmap, base.key), pages: changed.length };
}

async function side(s) {
	if (s.key === "now") {
		if (s.raw)
			decoded.set("now", parseState(s.raw));
		if (!decoded.has("now"))
			throw new Error("no copy of the machine's RAM: diff again");
		return decoded.get("now");
	}
	let d = decoded.get(s.key);
	if (!d) {
		if (s.chain && s.chain.length) {
			// A delta: its RAM from the chain, its CPU and page list from itself.
			const ram = await chainRam(s);
			const P = parseRaw(await unpack(s));
			d = { ram, cpu: cpuOf(P), base: P.deltaOf, changed: markedPages(P) };
		} else {
			d = parseState(await unpack(s));
		}
		decoded.set(s.key, d);
		const old = [...decoded.keys()].filter((k) => k !== "now");
		while (old.length > KEEP)
			decoded.delete(old.shift());
	}
	return d;
}

// Pages that differ between A and B (of the pages in only, if given).
function changedPages(A, B, only) {
	const n = Math.max(A.length, B.length) / PAGE;
	const a32 = new Int32Array(A.buffer, A.byteOffset, A.length >> 2);
	const b32 = new Int32Array(B.buffer, B.byteOffset, B.length >> 2);
	const out = [];
	const list = only || { length: n };
	for (let q = 0; q < list.length; q++) {
		const p = only ? only[q] : q;
		const lo = p * PAGE;
		if (lo >= A.length || lo >= B.length) {
			// RAM sizes differ: the page exists on one side only
			const X = lo >= A.length ? B : A;
			let c = 0;
			for (let i = lo; i < lo + PAGE; i++)
				c += X[i] !== 0;
			if (c)
				out.push([p, c]);
			continue;
		}
		let w = lo >> 2;
		const end = w + PAGE / 4;
		while (w < end && a32[w] === b32[w])
			w++;
		if (w === end)
			continue;
		let c = 0;
		for (let i = w << 2; i < lo + PAGE; i++)
			c += A[i] !== B[i];
		out.push([p, c]);
	}
	return out;
}

self.onmessage = async (ev) => {
	const m = ev.data;
	try {
		const t0 = performance.now();
		const done = (out, transfer) => self.postMessage({ id: m.id, ...out, ms: Math.round(performance.now() - t0) }, transfer);
		if (m.op === "pack") {
			// The page hashes of the new state; a delta of base when smaller.
			const P = parseRaw(m.raw);
			const hashes = hashState(P);
			const d = deltaOf(P, hashes, m.base);
			const raw = d ? d.raw : m.raw;
			const data = await pack(raw);
			done({ data, hashes, rawLen: raw.byteLength, full: m.raw.byteLength, delta: d ? { pages: d.pages } : null },
				[data, hashes.buffer]);
			return;
		}
		if (m.op === "unpack") {
			const f = await fullState(m.s);
			const hashes = !m.hashes ? null : f.ram ? hashRam(f.ram) : hashState(parseRaw(f.raw));
			done({ raw: f.raw, hashes }, hashes ? [f.raw, hashes.buffer] : [f.raw]);
			return;
		}
		if (m.op === "export") {
			const data = await pack((await fullState(m.s)).raw);
			done({ data }, [data]);
			return;
		}
		if (m.op === "rebase") {
			// s again after its base is deleted: a delta of base, or full.
			const ram = await chainRam(m.s);
			const P = parseRaw(await unpack(m.s));
			const nz = nonzeroPages(ram);
			let raw, delta = null;
			const full = layout(P, nz.length * PAGE, P.bitmap.length, null).length;
			if (m.base) {
				const list = changedPages(await chainRam(m.base), ram).map((x) => x[0]);
				const g = gather(ram, list);
				raw = serialize(P, g.packed, g.bitmap, m.base.key);
				delta = { pages: list.length };
			} else {
				const g = gather(ram, nz);
				raw = serialize(P, g.packed, g.bitmap, null);
			}
			decoded.delete(m.s.key);
			const data = await pack(raw);
			done({ data, rawLen: raw.byteLength, full, delta }, [data]);
			return;
		}
		const A = await side(m.a), B = await side(m.b);
		if (m.op === "diff") {
			// A delta and its base: only the delta's pages can differ.
			const only = B.changed && B.base === m.a.key ? B.changed : A.changed && A.base === m.b.key ? A.changed : null;
			const t1 = performance.now();
			const pages = changedPages(A.ram, B.ram, only);
			self.postMessage({ id: m.id, pages, compared: only ? only.length : -1, a: A.cpu, b: B.cpu,
				ms: Math.round(performance.now() - t1) });
		} else if (m.op === "pages") {
			const one = (X) => {
				const lo = m.p * PAGE;
				return lo < X.ram.length ? X.ram.slice(lo, lo + PAGE) : new Uint8Array(PAGE);
			};
			const pa = one(A), pb = one(B);
			self.postMessage({ id: m.id, a: pa, b: pb }, [pa.buffer, pb.buffer]);
		}
	} catch (e) {
		self.postMessage({ id: m.id, error: String(e && e.message || e) });
	}
};
