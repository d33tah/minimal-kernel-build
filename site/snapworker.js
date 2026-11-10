"use strict";
// Snapshot diffs off the page's main thread (S1). Takes snapshots (v86
// save_state() output, gzip or raw) and "now" (a copy of guest RAM and the
// CPU fields read on the page), keeps the last few decoded, and answers:
//   { op: "diff", id, a, b }     changed 4 KiB pages ([page, bytes changed])
//                                and the CPU fields of both sides
//   { op: "pages", id, a, b, p } page p of both sides (two 4096-byte arrays)
// where a and b are { key, blob, gzip } (a stored snapshot) or
// { key: "now", raw: ArrayBuffer } (a save_state() taken for the diff,
// sent once per diff).
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

const PAGE = 4096;
const STATE_MAGIC = 0x86768676 | 0;
const KEEP = 2; // decoded snapshots kept besides "now" (64 MiB each for Debian)
const decoded = new Map(); // key -> { ram: Uint8Array, cpu }

async function gunzip(blob) {
	const s = (blob instanceof Blob ? blob : new Blob([blob])).stream().pipeThrough(new DecompressionStream("gzip"));
	return new Response(s).arrayBuffer();
}

function parseState(buf) {
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
	const words = (o, T) => {
		const b = bytes(o);
		return new T(b.slice().buffer);
	};
	const st = info.state;
	const size = st[0] >>> 0;
	const packed = bytes(st[77]), bitmap = bytes(st[78]);
	const ram = new Uint8Array(size);
	let k = 0;
	for (let p = 0; p < size / PAGE; p++)
		if ((bitmap[p >> 3] >> (p & 7)) & 1)
			ram.set(packed.subarray(k * PAGE, ++k * PAGE), p * PAGE);
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
	return { ram, cpu: { regs, linear: lin, ramSize: size } };
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
		const buf = s.gzip ? await gunzip(s.blob) : await (s.blob instanceof Blob ? s.blob.arrayBuffer() : s.blob);
		d = parseState(buf);
		decoded.set(s.key, d);
		const old = [...decoded.keys()].filter((k) => k !== "now");
		while (old.length > KEEP)
			decoded.delete(old.shift());
	}
	return d;
}

function changedPages(A, B) {
	const n = Math.max(A.length, B.length) / PAGE;
	const a32 = new Int32Array(A.buffer, A.byteOffset, A.length >> 2);
	const b32 = new Int32Array(B.buffer, B.byteOffset, B.length >> 2);
	const out = [];
	for (let p = 0; p < n; p++) {
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
		const A = await side(m.a), B = await side(m.b);
		if (m.op === "diff") {
			const t0 = performance.now();
			const pages = changedPages(A.ram, B.ram);
			self.postMessage({ id: m.id, pages, a: A.cpu, b: B.cpu, ms: Math.round(performance.now() - t0) });
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
