"use strict";
// LZ4 frames for the snapshots (C1), used by snapworker.js. The blocks are
// compressed by lz4-wasm 0.9.2 (lz4_flex's LZ4 block compressor in
// WebAssembly, vendored in lz4/, site/THIRD_PARTY.md); this file is the
// glue the module needs and the LZ4 frame format around its blocks
// (github.com/lz4/lz4, doc/lz4_Frame_format.md), so a downloaded snapshot
// is a .lz4 file that `lz4 -d` reads.
//
// A frame here: magic 0x184D2204; FLG 0x68 (version 01, independent
// blocks, content size present); BD 0x70 (blocks of up to 4 MiB); the
// content size (64-bit little endian); the header checksum (second byte of
// xxh32 of FLG..content size); then per 4 MiB of input a 32-bit block size
// and the block (high bit set: stored, when LZ4 would not make it
// smaller); a zero size ends it. No block or content checksums.
//
// lz4_flex has one compressor, LZ4's fast one (no levels): what the lz4
// tool runs at its default level 1.

const LZ4_MAGIC = 0x184d2204;
const LZ4_BLOCK = 4 << 20;
let lz4Exports = null;
const lz4Strings = []; // error messages the module hands over (__wbindgen_string_new)

async function lz4Init(url) {
	if (lz4Exports)
		return;
	const resp = await fetch(url);
	if (!resp.ok)
		throw new Error(url + ": HTTP " + resp.status);
	const imports = {
		"./lz4_wasm_bg.js": {
			__wbindgen_string_new: (p, n) => lz4Strings.push(new TextDecoder().decode(new Uint8Array(lz4Exports.memory.buffer, p, n))) - 1,
		},
	};
	const { instance } = await WebAssembly.instantiate(await resp.arrayBuffer(), imports);
	lz4Exports = instance.exports;
}

// lz4-wasm's compress / decompress (the package's lz4_wasm_bg.js does the
// same): input copied into the module's memory, which takes ownership; the
// result is [ptr, len] (and for decompress [.., error, failed]) at retptr.
// compress returns the input's length (32-bit LE) and one LZ4 block;
// decompress takes the same and returns the bytes.
function lz4Call(fn, input) {
	const w = lz4Exports;
	const ret = w.__wbindgen_add_to_stack_pointer(-16);
	try {
		const p = w.__wbindgen_malloc(input.length);
		new Uint8Array(w.memory.buffer).set(input, p);
		w[fn](ret, p, input.length);
		const r = new Int32Array(w.memory.buffer, ret, 4).slice();
		if (fn === "decompress" && r[3])
			throw new Error("LZ4 block: " + (lz4Strings[r[2]] || "corrupt"));
		const out = new Uint8Array(w.memory.buffer, r[0], r[1]).slice();
		w.__wbindgen_free(r[0], r[1]);
		return out;
	} finally {
		w.__wbindgen_add_to_stack_pointer(16);
	}
}

// xxh32 with seed 0 of fewer than 16 bytes (the frame descriptor).
function lz4Xxh32Short(b) {
	const P1 = 0x9e3779b1, P2 = 0x85ebca77, P3 = 0xc2b2ae3d, P4 = 0x27d4eb2f, P5 = 0x165667b1;
	const rotl = (x, r) => (x << r) | (x >>> (32 - r));
	let h = (P5 + b.length) | 0, i = 0;
	for (; i + 4 <= b.length; i += 4)
		h = Math.imul(rotl((h + Math.imul(b[i] | b[i + 1] << 8 | b[i + 2] << 16 | b[i + 3] << 24, P3)) | 0, 17), P4);
	for (; i < b.length; i++)
		h = Math.imul(rotl((h + Math.imul(b[i], P5)) | 0, 11), P1);
	h ^= h >>> 15;
	h = Math.imul(h, P2);
	h ^= h >>> 13;
	h = Math.imul(h, P3);
	h ^= h >>> 16;
	return h >>> 0;
}

// src (Uint8Array) as one LZ4 frame; returns an ArrayBuffer.
function lz4FramePack(src) {
	const head = new Uint8Array(15);
	const hv = new DataView(head.buffer);
	hv.setUint32(0, LZ4_MAGIC, true);
	head[4] = 0x68;
	head[5] = 0x70;
	hv.setUint32(6, src.length >>> 0, true);
	hv.setUint32(10, Math.floor(src.length / 2 ** 32), true);
	head[14] = (lz4Xxh32Short(head.subarray(4, 14)) >>> 8) & 0xff;
	const parts = [head];
	let total = head.length + 4;
	for (let o = 0; o < src.length; o += LZ4_BLOCK) {
		const chunk = src.subarray(o, o + LZ4_BLOCK);
		const c = lz4Call("compress", chunk);
		const stored = c.length - 4 >= chunk.length;
		const block = stored ? chunk : c.subarray(4);
		const size = new Uint8Array(4);
		new DataView(size.buffer).setUint32(0, stored ? (chunk.length | 0x80000000) >>> 0 : block.length, true);
		parts.push(size, block);
		total += 4 + block.length;
	}
	parts.push(new Uint8Array(4));
	const out = new Uint8Array(total);
	let o = 0;
	for (const p of parts) {
		out.set(p, o);
		o += p.length;
	}
	return out.buffer;
}

// One LZ4 frame (src, Uint8Array) back to its bytes; size is the expected
// length (used when the frame does not carry its content size). Frames with
// dependent blocks or a dictionary are not ones this page writes: refused.
function lz4FrameUnpack(src, size) {
	const dv = new DataView(src.buffer, src.byteOffset, src.byteLength);
	if (src.length < 7 || dv.getUint32(0, true) !== LZ4_MAGIC)
		throw new Error("not an LZ4 frame");
	const flg = src[4], bd = src[5];
	if (flg >> 6 !== 1 || flg & 1 || !(flg & 0x20))
		throw new Error("LZ4 frame with FLG " + flg.toString(16) + ": only version 01, independent blocks, no dictionary are read");
	const blockMax = 1 << (8 + 2 * ((bd >> 4) & 7));
	let i = 6;
	if (flg & 0x08) {
		size = dv.getUint32(i, true) + dv.getUint32(i + 4, true) * 2 ** 32;
		i += 8;
	}
	if (src[i] !== ((lz4Xxh32Short(src.subarray(4, i)) >>> 8) & 0xff))
		throw new Error("LZ4 frame header checksum mismatch");
	i++;
	const out = new Uint8Array(size);
	let o = 0;
	for (;;) {
		const n = dv.getUint32(i, true);
		i += 4;
		if (n === 0)
			break;
		const len = n & 0x7fffffff;
		if (n & 0x80000000) {
			out.set(src.subarray(i, i + len), o);
			o += len;
		} else {
			const want = Math.min(blockMax, size - o);
			const input = new Uint8Array(4 + len);
			new DataView(input.buffer).setUint32(0, want, true);
			input.set(src.subarray(i, i + len), 4);
			// lz4-wasm fails a block that does not decode to exactly want bytes.
			const b = lz4Call("decompress", input);
			out.set(b, o);
			o += b.length;
		}
		i += len + (flg & 0x10 ? 4 : 0);
	}
	if (o !== size)
		throw new Error("LZ4 frame holds " + o + " bytes, " + size + " expected");
	return out.buffer;
}
