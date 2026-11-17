"use strict";

// The v86.wasm patch for stepping: patchV86Wasm exports the two internal
// functions Stepper.run calls (stepper.js has why and how).

const STEP_EXPORTS = {
	mkb_read_imm8: /3cpu3cpu9read_imm8$/,
	mkb_run: /3gen11interpreter3run$/,
};

function wasmReader(b, p) {
	const r = {
		p,
		leb() {
			let v = 0, sh = 0, x;
			do { x = b[r.p++]; v += (x & 0x7f) * 2 ** sh; sh += 7; } while (x & 0x80);
			return v;
		},
		str() { const n = r.leb(); const s = new TextDecoder().decode(b.subarray(r.p, r.p + n)); r.p += n; return s; },
	};
	return r;
}

function lebBytes(v) {
	const out = [];
	do { let x = v & 0x7f; v = Math.floor(v / 128); if (v) x |= 0x80; out.push(x); } while (v);
	return out;
}

// Returns { bytes, found: {export: function index} } or throws.
function patchV86Wasm(buffer) {
	const b = new Uint8Array(buffer);
	const sections = [];
	const r = wasmReader(b, 8);
	while (r.p < b.length) {
		const start = r.p, id = b[r.p++], len = r.leb(), body = r.p;
		sections.push({ id, start, body, end: body + len });
		r.p = body + len;
	}
	const names = new Map();
	for (const s of sections) {
		if (s.id !== 0)
			continue;
		const q = wasmReader(b, s.body);
		if (q.str() !== "name")
			continue;
		while (q.p < s.end) {
			const sub = b[q.p++], len = q.leb(), end = q.p + len;
			if (sub === 1)
				for (let n = q.leb(); n > 0; n--) {
					const idx = q.leb();
					names.set(q.str(), idx);
				}
			q.p = end;
		}
	}
	const found = {};
	for (const [exp, re] of Object.entries(STEP_EXPORTS)) {
		const hits = [...names].filter(([n]) => re.test(n));
		if (hits.length !== 1)
			throw new Error("v86.wasm: " + hits.length + " functions match " + re);
		found[exp] = hits[0][1];
	}
	const ex = sections.find((s) => s.id === 7);
	if (!ex)
		throw new Error("v86.wasm has no export section");
	const q = wasmReader(b, ex.body);
	const count = q.leb();
	const entries = Array.from(b.subarray(q.p, ex.end));
	for (const [name, idx] of Object.entries(found)) {
		const nb = new TextEncoder().encode(name);
		entries.push(...lebBytes(nb.length), ...nb, 0, ...lebBytes(idx));
	}
	const body = [...lebBytes(count + Object.keys(found).length), ...entries];
	const head = [7, ...lebBytes(body.length)];
	const out = new Uint8Array(b.length - (ex.end - ex.start) + head.length + body.length);
	out.set(b.subarray(0, ex.start), 0);
	out.set(head, ex.start);
	out.set(body, ex.start + head.length);
	out.set(b.subarray(ex.end), ex.start + head.length + body.length);
	return { bytes: out, found };
}
// For the tests: a failing one in its place.
const setPatchV86Wasm = (f) => { patchV86Wasm = f; };

export { patchV86Wasm, setPatchV86Wasm };
