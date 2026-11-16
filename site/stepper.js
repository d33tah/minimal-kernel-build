"use strict";

// Single-instruction stepping for v86 0.5.471, without rebuilding v86.
//
// The release v86.wasm runs guest code in main_loop(), at least 100,003
// instructions per call, through its JIT. It exports no single-step entry
// point, but its "name" section still names two internal functions:
//
//   v86::cpu::cpu::read_imm8          fetch the byte at EIP, EIP += 1
//                                     (page walk, #PF on a fault)
//   v86::gen::interpreter::run        execute one instruction given its
//                                     first byte (| is_32 << 8)
//
// patchV86Wasm() adds both to the export section (the code is untouched),
// and Stepper.run() does per instruction what v86's own interpreter loop
// (jit_run_interpreted in src/rust/cpu/cpu.rs) does:
//   previous_ip = eip; op = read_imm8(); run(op | is_32 << 8); counter++.

import * as clock from "./clock.js";
import { revMark, MEV } from "./page.js";
import * as jumps from "./jumps.js";

// A module's imported and exported bindings are cells: optimized code loads
// them on every use, where it folds a private const to its value (V8: a
// small method reading an exported const ran 3x slower than one reading a
// private one). Frames' methods and Stepper.record run per instruction, so
// the constants they read are private: the ones from other modules copied
// here, this module's own exported as one object (STEP_CONST).
const { realNow, CLOCK } = clock;
const { JK } = jumps;

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

const VGA_TEXT = 0xb8000;
const VGA_TEXT_BYTES = 80 * 25 * 2;
// The whole 32 KiB text window: Linux's vgacon scrolls by moving the
// display start, so its text lands anywhere in 0xb8000-0xbffff.
const VGA_WINDOW_BYTES = 0x8000;
const MMAP_BLOCK_BITS = 17; // v86 src/const.js
// Lines the VGA hook watches for as they are written (Stepper.textSeen, J3):
// "Kernel panic - not syncing:" (panic() has begun printing), the panic's
// last line "---[ end Kernel panic - not syncing: ... ]---" complete, and
// Debian's initramfs shell prompt "(initramfs) " at the start of a row.
const TEXT_PANIC = 1, TEXT_END = 2, TEXT_PROMPT = 4;
// Byte offsets of CPU globals in wasm memory (v86 src/cpu.js), as indices.
const IP = 556 >> 2, PREV_IP = 560 >> 2, EIP_PHYS = 624 >> 2, COUNTER = 664 >> 2;
const IN_HLT = 616, IS_32 = 804;
const EAX = 64 >> 2, EBX = (64 + 12) >> 2, ESP = (64 + 16) >> 2;

// Observed jump paths (jumps.js has the kinds, JK): the first opcode byte
// after prefixes -> kind, for the one-byte opcodes that transfer control.
// 0x0f and 0xff are looked at further (Stepper.jumpAfter); prefixes are
// skipped there. Conditional ones (jcc, loop/jecxz, into) record a path
// only when taken.
const JOP_PREFIX = new Uint8Array(256), JOP = new Uint8Array(256);
for (const b of [0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65, 0x66, 0x67, 0xf0, 0xf2, 0xf3])
	JOP_PREFIX[b] = 1;
{
	const k = JK; // jumps.js, loaded first
	for (let b = 0x70; b <= 0x7f; b++)
		JOP[b] = k.jcc;
	for (let b = 0xe0; b <= 0xe3; b++)
		JOP[b] = k.loop;
	JOP[0xe8] = k.call;
	JOP[0xe9] = JOP[0xeb] = k.jmp;
	JOP[0xea] = k.ljmp;
	JOP[0x9a] = k.lcall;
	JOP[0xc2] = JOP[0xc3] = k.ret;
	JOP[0xca] = JOP[0xcb] = k.lret;
	JOP[0xcf] = k.iret;
	JOP[0xcc] = JOP[0xcd] = JOP[0xce] = JOP[0xf1] = k.int;
}
// An instruction is at most 15 bytes: a non-transfer whose next EIP is
// further away than that (or before it) raised an exception.
const INSN_MAX = 15;

// Call frames of the Calls timeline (Stepper.fr). One column per field,
// each a list of typed-array chunks of FR_SIZE frames: frame k is slot
// k & FR_MASK of chunk k >>> FR_BITS, so growing adds a chunk and never
// copies what is recorded (a run records millions of frames). A value that
// does not fit its column (a count past 2^32, a depth past 2^16) is kept
// in that column's overflow Map and its slot holds the column's sentinel:
// every value reads back exactly as it was stored. Reads past the end give
// undefined, as an array's would.
const FR_BITS = 16, FR_SIZE = 1 << FR_BITS, FR_MASK = FR_SIZE - 1;
const FR_BIG = 0xffffffff, FR_BIG16 = 0xffff;
// Flag bits: in an interrupt or exception; a continuation of a call
// suspended by a task switch; entered from no known instruction (-1);
// entered from an address that is no 32-bit number (in fromBig).
const FR_IRQ = 1, FR_CONT = 2, FR_NOFROM = 4, FR_BIGFROM = 8;
// Bytes per frame of the columns below.
const FR_BYTES = 4 * 7 + 2 * 2 + 1;

class Frames {
	constructor() {
		this.n = 0;
		this.maxDepth = 0;   // the deepest frame stored (not lowered by truncate)
		this.base = [];      // per chunk: the start of its first frame
		this.cStart = [];    // Uint32: start - base of the chunk
		this.cEnd = [];      // Uint32: 0 while open, else end - start + 1
		this.cChild = [];    // Uint32: instructions spent in its closed children
		this.cEsp = [];      // Uint32: ESP on arrival
		this.cNode = [];     // Int32: symbol index
		this.cParent = [];   // Int32: parent frame, -1 for none
		this.cFrom = [];     // Uint32: physical address of the instruction that entered it
		this.cDepth = [];    // Uint16
		this.cTask = [];     // Uint16
		this.cFlags = [];    // Uint8: FR_* bits
		this.startBig = new Map();
		this.endBig = new Map();
		this.childBig = new Map();
		this.fromBig = new Map();
		this.depthBig = new Map();
		this.taskBig = new Map();
	}

	grow(start) {
		this.base.push(start);
		this.cStart.push(new Uint32Array(FR_SIZE));
		this.cEnd.push(new Uint32Array(FR_SIZE));
		this.cChild.push(new Uint32Array(FR_SIZE));
		this.cEsp.push(new Uint32Array(FR_SIZE));
		this.cNode.push(new Int32Array(FR_SIZE));
		this.cParent.push(new Int32Array(FR_SIZE));
		this.cFrom.push(new Uint32Array(FR_SIZE));
		this.cDepth.push(new Uint16Array(FR_SIZE));
		this.cTask.push(new Uint16Array(FR_SIZE));
		this.cFlags.push(new Uint8Array(FR_SIZE));
	}

	// A new open frame; its depth is its parent's + 1. Returns its index.
	push(start, node, parent, irq, cont, task, from, esp) {
		const k = this.n, c = k >>> FR_BITS, i = k & FR_MASK;
		if (c === this.cStart.length)
			this.grow(start);
		this.n = k + 1;
		const d = start - this.base[c];
		if (d >>> 0 === d && d !== FR_BIG) {
			this.cStart[c][i] = d;
		} else {
			this.cStart[c][i] = FR_BIG;
			this.startBig.set(k, start);
		}
		this.cEnd[c][i] = 0;
		this.cChild[c][i] = 0;
		this.cEsp[c][i] = esp;
		this.cNode[c][i] = node;
		this.cParent[c][i] = parent;
		const pd = parent >= 0 ? this.cDepth[parent >>> FR_BITS][parent & FR_MASK] : -1;
		const depth = pd === FR_BIG16 ? this.depthBig.get(parent) + 1 : pd + 1;
		if (depth > this.maxDepth)
			this.maxDepth = depth;
		if (depth < FR_BIG16) {
			this.cDepth[c][i] = depth;
		} else {
			this.cDepth[c][i] = FR_BIG16;
			this.depthBig.set(k, depth);
		}
		if (task >>> 0 === task && task < FR_BIG16) {
			this.cTask[c][i] = task;
		} else {
			this.cTask[c][i] = FR_BIG16;
			this.taskBig.set(k, task);
		}
		let f = (irq ? FR_IRQ : 0) | (cont ? FR_CONT : 0) | (from === -1 ? FR_NOFROM : 0);
		if (from >>> 0 === from || from === -1) {
			this.cFrom[c][i] = from;
		} else {
			f |= FR_BIGFROM;
			this.fromBig.set(k, from);
		}
		this.cFlags[c][i] = f;
		return k;
	}

	start(k) {
		if (!(k >= 0 && k < this.n))
			return undefined;
		const c = k >>> FR_BITS, d = this.cStart[c][k & FR_MASK];
		return d !== FR_BIG ? this.base[c] + d : this.startBig.get(k);
	}

	// The step after its last instruction, -1 while open.
	end(k) {
		if (!(k >= 0 && k < this.n))
			return undefined;
		const c = k >>> FR_BITS, i = k & FR_MASK, e = this.cEnd[c][i];
		if (e === 0)
			return -1;
		if (e === FR_BIG)
			return this.endBig.get(k);
		const d = this.cStart[c][i];
		return (d !== FR_BIG ? this.base[c] + d : this.startBig.get(k)) + e - 1;
	}

	// Frame k ends at step t: its end, and its instructions go to its
	// parent's children.
	close(k, t) {
		const s = this.start(k);
		this.setEnd(k, t, s);
		const p = this.cParent[k >>> FR_BITS][k & FR_MASK];
		if (p >= 0)
			this.addChild(p, t - s);
	}

	setEnd(k, t, start = this.start(k)) {
		const c = this.cEnd[k >>> FR_BITS], i = k & FR_MASK;
		if (c[i] === FR_BIG)
			this.endBig.delete(k);
		const v = t - start + 1;
		if (v >>> 0 === v && v !== 0 && v !== FR_BIG) {
			c[i] = v;
		} else {
			c[i] = FR_BIG;
			this.endBig.set(k, t);
		}
	}

	child(k) {
		if (!(k >= 0 && k < this.n))
			return undefined;
		const x = this.cChild[k >>> FR_BITS][k & FR_MASK];
		return x !== FR_BIG ? x : this.childBig.get(k);
	}

	addChild(k, d) {
		const c = this.cChild[k >>> FR_BITS], i = k & FR_MASK;
		let v = c[i];
		if (v === FR_BIG) {
			v = this.childBig.get(k);
			this.childBig.delete(k);
		}
		v += d;
		if (v >>> 0 === v && v !== FR_BIG) {
			c[i] = v;
		} else {
			c[i] = FR_BIG;
			this.childBig.set(k, v);
		}
	}

	esp(k) {
		return k >= 0 && k < this.n ? this.cEsp[k >>> FR_BITS][k & FR_MASK] : undefined;
	}

	// espOf, nodeOf, parentOf, irqOf: reads of a frame known to be stored
	// (the stepper's stacks, a walk up the parents), without the bounds check.
	espOf(k) {
		return this.cEsp[k >>> FR_BITS][k & FR_MASK];
	}

	nodeOf(k) {
		return this.cNode[k >>> FR_BITS][k & FR_MASK];
	}

	parentOf(k) {
		return this.cParent[k >>> FR_BITS][k & FR_MASK];
	}

	irqOf(k) {
		return this.cFlags[k >>> FR_BITS][k & FR_MASK] & FR_IRQ;
	}

	// Scans over every frame (the Calls views) go chunk by chunk: frames
	// k = c * FR_SIZE + i, i < chunkLen(c), reading the chunk's own arrays.
	// parentsOf and nodesOf hold the values; flagsOf the FR_* bits;
	// startsRaw, endsRaw, depthsRaw and tasksRaw the stored form, which
	// frStartAt, frEndAt and frSmallAt below turn into the values (only for
	// the frames a scan uses: most views skip most frames).
	chunkCount() {
		return this.cStart.length;
	}

	chunkLen(c) {
		return Math.max(0, Math.min(FR_SIZE, this.n - c * FR_SIZE));
	}

	parentsOf(c) {
		return this.cParent[c];
	}

	nodesOf(c) {
		return this.cNode[c];
	}

	flagsOf(c) {
		return this.cFlags[c];
	}

	baseOf(c) {
		return this.base[c];
	}

	startsRaw(c) {
		return this.cStart[c];
	}

	endsRaw(c) {
		return this.cEnd[c];
	}

	depthsRaw(c) {
		return this.cDepth[c];
	}

	tasksRaw(c) {
		return this.cTask[c];
	}

	node(k) {
		return k >= 0 && k < this.n ? this.cNode[k >>> FR_BITS][k & FR_MASK] : undefined;
	}

	parent(k) {
		return k >= 0 && k < this.n ? this.cParent[k >>> FR_BITS][k & FR_MASK] : undefined;
	}

	depth(k) {
		if (!(k >= 0 && k < this.n))
			return undefined;
		const x = this.cDepth[k >>> FR_BITS][k & FR_MASK];
		return x !== FR_BIG16 ? x : this.depthBig.get(k);
	}

	task(k) {
		if (!(k >= 0 && k < this.n))
			return undefined;
		const x = this.cTask[k >>> FR_BITS][k & FR_MASK];
		return x !== FR_BIG16 ? x : this.taskBig.get(k);
	}

	// 1 inside an interrupt or exception, else 0.
	irq(k) {
		return k >= 0 && k < this.n ? this.cFlags[k >>> FR_BITS][k & FR_MASK] & FR_IRQ : undefined;
	}

	// 1: continuation of a call suspended by a task switch, else 0.
	cont(k) {
		return k >= 0 && k < this.n ? (this.cFlags[k >>> FR_BITS][k & FR_MASK] & FR_CONT) >> 1 : undefined;
	}

	// Physical address of the instruction that entered it (in the caller), -1 unknown.
	from(k) {
		if (!(k >= 0 && k < this.n))
			return undefined;
		const c = k >>> FR_BITS, i = k & FR_MASK, f = this.cFlags[c][i];
		return f & FR_NOFROM ? -1 : f & FR_BIGFROM ? this.fromBig.get(k) : this.cFrom[c][i];
	}

	// Keep the first n frames.
	truncate(n) {
		n = this.n = Math.min(n, this.n);
		const chunks = (n + FR_MASK) >>> FR_BITS;
		for (const a of [this.base, this.cStart, this.cEnd, this.cChild, this.cEsp, this.cNode, this.cParent, this.cFrom,
			this.cDepth, this.cTask, this.cFlags])
			a.length = chunks;
		for (const m of [this.startBig, this.endBig, this.childBig, this.fromBig, this.depthBig, this.taskBig])
			for (const k of m.keys())
				if (k >= n)
					m.delete(k);
	}

	clearChildren() {
		for (const c of this.cChild)
			c.fill(0);
		this.childBig.clear();
	}

	// Bytes held: the chunks, and an estimate for the overflow entries.
	bytes() {
		let big = 0;
		for (const m of [this.startBig, this.endBig, this.childBig, this.fromBig, this.depthBig, this.taskBig])
			big += m.size;
		return this.cStart.length * FR_SIZE * FR_BYTES + this.base.length * 8 + big * 64;
	}
}

// Frame k = c * FR_SIZE + i read from its chunk's stored columns: its start
// (base = baseOf(c), ds = startsRaw(c)), its end given that start s
// (de = endsRaw(c); -1 while open), and its depth or task (d = depthsRaw(c)
// or tasksRaw(c); get: "depth" or "task").
const frStartAt = (fr, base, ds, i, k) => (ds[i] !== FR_BIG ? base + ds[i] : fr.start(k));
const frEndAt = (fr, de, i, k, s) => (de[i] === 0 ? -1 : de[i] !== FR_BIG ? s + de[i] - 1 : fr.end(k));
const frSmallAt = (fr, d, i, k, get) => (d[i] !== FR_BIG16 ? d[i] : fr[get](k));

// A scan of the frames in order (k = 0, 1, ...) with each frame's callers
// at hand: at[0..len-1] is the path from a root frame down to the frame
// entered last (at[d] has depth d). A parent comes before its children and
// is one shallower, so entering the next frame pops a few frames off the
// path and pushes one; only a frame of another chain (another task's, after
// a switch) walks up its parents, to the first one on the path. count[node]:
// frames of that symbol on the path; marked[0..markedLen-1]: the frames on
// the path pushed as marked (mark(k) for the ones a walk up pushes), nearest
// last. Replaces a walk up the parents per frame.
class FramePath {
	constructor(fr, nodes, mark = null) {
		const size = fr.maxDepth + 2;
		this.fr = fr;
		this.len = 0;
		this.at = new Int32Array(size);
		this.nodeAt = new Int32Array(size);
		this.markAt = new Uint8Array(size);
		this.count = new Int32Array(nodes);
		this.mark = mark;
		this.marked = new Int32Array(size);
		this.markedLen = 0;
	}

	popTo(len) {
		while (this.len > len) {
			const d = --this.len;
			this.count[this.nodeAt[d]]--;
			this.markedLen -= this.markAt[d];
		}
	}

	push(k, node, marked) {
		const d = this.len++;
		this.at[d] = k;
		this.nodeAt[d] = node;
		this.count[node]++;
		this.markAt[d] = marked;
		if (marked)
			this.marked[this.markedLen++] = k;
	}

	// The path ends at frame p of depth dp (-1: at no frame): the parent of
	// the frame entered next.
	toParent(p, dp) {
		if (p < 0 || (dp < this.len && this.at[dp] === p)) {
			this.popTo(p < 0 ? 0 : dp + 1);
			return;
		}
		const up = [];
		for (; p >= 0 && !(dp < this.len && this.at[dp] === p); p = this.fr.parentOf(p), dp--)
			up.push(p);
		this.popTo(dp + 1);
		for (let j = up.length - 1; j >= 0; j--)
			this.push(up[j], this.fr.nodeOf(up[j]), this.mark ? this.mark(up[j]) : 0);
	}
}

// Drives a stopped v86 instance one instruction at a time and counts.
const POPCOUNT8 = Uint8Array.from({ length: 256 }, (_, b) => { let n = 0; for (; b; b &= b - 1) n++; return n; });

class Stepper {
	constructor(emulator, kernelLo, kernelHi) {
		const cpu = emulator.v86.cpu;
		this.cpu = cpu;
		this.ex = cpu.wm.exports;
		if (!this.ex.mkb_read_imm8 || !this.ex.mkb_run)
			throw new Error("this v86.wasm has no step exports");
		this.res = this.ex.v86_malloc(8);
		this.kernelLo = kernelLo;
		this.kernelHi = kernelHi;
		this.steps = 0;          // instructions executed by this stepper
		this.sinceEntry = -1;    // steps when EIP first reached kernelLo, -1 before
		this.unique = 0;         // distinct physical addresses executed
		this.uniqueKernel = 0;   // ... of which in [kernelLo, kernelHi)
		this.lastPhys = -1;
		this.seen = new Uint8Array((cpu.mem8.length + 7) >> 3);
		this.seenHigh = new Set();
		// Observed jump paths: from * 2^32 + to -> [from, to, kind mask, count],
		// also listed per source and per target (the same arrays).
		this.jumps = new Map();
		this.jumpOut = new Map();
		this.jumpIn = new Map();
		this.jumpCount = 0;  // transfers recorded
		this.jPend = -1;     // the instruction whose transfer lands at the next fetch
		this.jPendKind = 0;
		this.jLast = -1;     // the last instruction stepped (since a gap)
		// v86's JIT ran code, which seen does not record: in the fast-forward
		// to the kernel entry, or at full speed.
		this.jitBeforeEntry = false;
		this.jitAfterEntry = false;
		this.text = new Uint8Array(VGA_WINDOW_BYTES);
		this.textSeen = 0;       // TEXT_* lines written so far
		this.vgaDirty = false;
		this.milestones = [];    // { text, found: null | {...} }
		this.exit = null;        // first exit/exit_group syscall: { code, ...snapshot }
		this.end = null;         // { kind: "panic" | "halt", ...snapshot }
		this.panicEntered = null; // snapshot when EIP first entered panic()
		this.panicBudget = 50e6;  // instructions after that without an end
		this.lastTimers = 0;
		this.idleMs = 0;         // virtual time skipped while halted
		this.irq0 = 0;           // timer interrupts (IRQ0) raised by the PIT
		// Reverse execution (R2, reverse.js): after stepping back, the
		// instructions up to `ahead` (the furthest count reached) run again
		// without being recorded again (rerun); what was recorded after the
		// count stepped back to waits in `future` ({ steps, key, i, rec },
		// by count) until the count gets there.
		this.ahead = 0;
		this.future = [];
		const raise = cpu.device_raise_irq;
		cpu.device_raise_irq = (irq) => {
			if (irq === 0)
				this.irq0++;
			raise(irq);
		};
		this.setSymbols([]);
		this.hookVga();
	}

	// Code symbols ({paddr, size, name}, sorted by paddr) for the symbol
	// history: which function the CPU is in and which ones it was in before.
	setSymbols(list) {
		this.symStart = list.map((s) => s.paddr);
		this.symEnd = list.map((s) => s.paddr + s.size);
		this.symName = list.map((s) => s.name);
		this.curSym = -2;      // index into the lists, -1 = no symbol, -2 = unknown
		this.curLo = 1;        // [curLo, curHi): addresses that stay in curSym
		this.curHi = 0;
		this.history = [];     // most recent last: { sym, last } (last = final EIP in it)
		this.panicSym = this.symName.indexOf("panic");
		this.resetGraph();
	}

	// Symbol graph: nodes are symbol indices, plus N = BIOS/setup (below
	// the kernel) and N + 1 = kernel-range code that is no symbol (e.g. the
	// relocation stub). Edges count transitions from -> to; "calls" are
	// the ones that land on the first byte of a symbol.
	resetGraph() {
		const n = this.symName.length + 2;
		this.nodeSteps = new Float64Array(n);   // instructions executed inside
		this.nodeFirst = new Float64Array(n).fill(-1); // step when first entered
		this.nodeOrder = [];                      // node ids in first-seen order
		this.nodeParent = new Int32Array(n).fill(-1); // node it was first entered from
		this.edges = new Map();                   // from * n + to -> { from, to, count, calls, first }
		this.recent = [];                         // last edge keys, newest last
		this.curNode = -1;
		this.graphVersion = 0;
		// Call stack reconstructed from the transitions (see stackEnter).
		this.entryKind = new Uint8Array(n); // 1: interrupt/exception entry stub
		for (let k = 0; k < this.symName.length; k++)
			if (/^(irq_entries_start|asm_|entry_INT80|nmi_stub)/.test(this.symName[k]))
				this.entryKind[k] = 1;
		// Per frame (Frames): first instruction (step index), the step after
		// its last one (-1 while open), symbol node, depth, parent frame,
		// interrupt flag, instructions in its closed children, ESP on arrival
		// at its first instruction, continuation flag, task (see taskOf), the
		// physical address of the instruction that entered it.
		this.fr = new Frames();
		// Tasks: a task is identified by the kernel stack it switches away
		// on (ESP >> 13, THREAD_SIZE is 8 KiB on i386). Task 0 is the boot
		// task (swapper: the boot stack, then init_task's own stack).
		this.task = 0;
		this.taskOf = new Map();   // stack region -> task
		this.taskCount = 1;
		this.taskRegion = [-1]; // task -> the stack region it switched away on (or started on)
		// task -> what onTaskLeave(task, step, dead) said as it last switched
		// away (the page sets it: vars.js varsTaskNow reads the leaving task's
		// task_struct, its name and pid, through current_task).
		this.taskSeen = [];
		this.contFrames = 0;
		// Task switches: step, incoming task, 1 when it left from inside
		// interrupt handling (preemption on an interrupt's return).
		this.swStep = [];
		this.swTask = [];
		this.swIrq = [];
		this.freshTask = false;
		this.tailEsp = -1;   // ESP __switch_to arrived with in a new task's switch tail
		this.suspended = new Map(); // task -> its open call chain, root first
		// Open frames per stack region (ESP >> 13: Linux's 8 KiB task and
		// IRQ stacks). `stack` is the lane of the current ESP; a lane's first
		// frame hangs below the frame that was running when ESP moved there.
		this.lanes = new Map();
		this.stack = [];
		this.lane = -1;
		this.lastTop = -1;  // top frame of the lane we came from
		if (this.espUnwind === undefined)
			this.espUnwind = true;
	}

	allOpen() {
		if (!this.lanes.size)
			return this.stack.slice();
		const out = [];
		for (const l of this.lanes.values())
			out.push(...l);
		return out;
	}

	pushFrame(node, t, irq, esp) {
		const fr = this.fr;
		const parent = this.stack.length ? this.stack[this.stack.length - 1] : this.lastTop;
		this.stack.push(fr.push(t, node, parent, irq || (parent >= 0 ? fr.irqOf(parent) : 0), 0, this.task, this.lastPhys, esp));
	}

	// Resume a task: reopen its suspended call chain as continuation frames
	// (same symbols, ESPs and stack lanes as the calls they continue), so
	// what it runs next gets its real callers.
	resumeTask(chain, t) {
		const fr = this.fr;
		let parent = -1;
		for (const f of chain) {
			const esp = fr.esp(f), region = esp >>> 13;
			if (!this.lanes.has(region))
				this.lanes.set(region, []);
			const k = fr.push(t, fr.node(f), parent, fr.irq(f), 1, this.task, fr.from(f), esp);
			this.contFrames++;
			this.lanes.get(region).push(k);
			parent = k;
		}
		return parent;
	}

	popFrame(t) {
		this.fr.close(this.stack.pop(), t);
	}

	// A transition into `node` at step index t: a call when it lands on the
	// first byte of a symbol, an interrupt or exception when the target is an
	// entry stub; otherwise a return into a frame on the stack (pop to it),
	// or, when the symbol is not on the stack (a tail jump, a task switch),
	// a replacement of the top frame.
	stackEnter(node, isCall, t, esp) {
		// No frames for code without a symbol (BIOS, setup, the relocation
		// stub): its instructions count to the caller's self time.
		if (node >= this.symName.length)
			return;
		if (this.espUnwind) {
			// Switch lanes when ESP is in another stack region.
			const region = esp >>> 13;
			if (region !== this.lane) {
				// A stack change inside a task (IRQ/softirq stacks through
				// call_on_stack, the early switch from the boot stack to the
				// kernel's own) keeps the frames: the new stack's frames hang
				// below the code that switched. A task switch (through
				// __switch_to_asm and friends) starts the new task at the root.
				const prev = this.curNode >= 0 && this.curNode < this.symName.length ? this.symName[this.curNode] : "";
				const top = this.stack.length ? this.stack[this.stack.length - 1] : -1;
				const nested = !/switch_to/.test(prev);
				this.lastTop = nested ? top : -1;
				// A task switch suspends the task we leave: its open calls
				// (including those it made on the boot stack before it had its
				// own) end here and their chain is saved; it is reopened as
				// continuation frames when the task runs again. A task that
				// switches away from do_task_dead never runs again (its stack
				// may be reused by a new task). Every other open frame ends
				// too, so nothing overlaps the next task in the timeline.
				if (!nested) {
					const chain = [];
					for (let p = top; p >= 0; p = this.fr.parent(p))
						chain.push(p);
					chain.reverse();
					const dead = chain.some((f) => /^do_task_dead$/.test(this.symName[this.fr.node(f)]));
					if (this.lane >= 0) {
						this.taskRegion[this.task] = this.lane;
						const seen = this.onTaskLeave ? this.onTaskLeave(this.task, t, dead) : null;
						if (seen)
							this.taskSeen[this.task] = seen;
						if (dead) {
							this.taskOf.delete(this.lane);
							this.suspended.delete(this.task);
						} else {
							this.taskOf.set(this.lane, this.task);
							this.suspended.set(this.task, chain);
						}
					}
					for (const l of this.lanes.values()) {
						this.stack = l;
						while (l.length)
							this.popFrame(t);
					}
					let task = this.taskOf.get(region);
					this.freshTask = task === undefined;
					this.tailEsp = -1;
					if (task === undefined) {
						task = this.taskCount++;
						this.taskRegion[task] = region;
					}
					this.task = task;
					this.swStep.push(t);
					this.swTask.push(task);
					this.swIrq.push(top >= 0 ? this.fr.irq(top) : 0);
					const saved = this.suspended.get(task);
					if (saved) {
						this.suspended.delete(task);
						this.lastTop = this.resumeTask(saved, t);
					}
				}
				if (!this.lanes.has(region))
					this.lanes.set(region, []);
				this.stack = this.lanes.get(region);
				this.lane = region;
			}
			// A return (ret, iret, longjmp-like stack resets) unwinds every
			// frame of this lane that lives below the current ESP.
			// The outermost frame of the lane that lives below ESP returned,
			// and with it everything above it. A call arriving at the same
			// ESP as an open frame replaces it: that frame was a sibling that
			// returned elsewhere (e.g. on another stack) or tail-called us.
			const st = this.stack;
			const call = isCall || this.entryKind[node];
			const fr = this.fr;
			let i = 0;
			for (let e; i < st.length && ((e = fr.espOf(st[i])) > esp || (!call && e === esp));)
				i++;
			while (st.length > i)
				this.popFrame(t);
		}
		// A new task starts in the tail of the switch (__switch_to, which
		// returns into ret_from_fork): no root frame for it, nor for what it
		// calls (at -O0 its static inline helpers are out-of-line calls,
		// below the ESP __switch_to arrived with).
		if (this.freshTask) {
			if (!this.stack.length && /switch_to/.test(this.symName[node])) {
				if (this.tailEsp < 0)
					this.tailEsp = esp;
				return;
			}
			if (!this.stack.length && this.tailEsp >= 0 && esp < this.tailEsp)
				return;
			this.freshTask = false;
			this.tailEsp = -1;
		}
		const nf = this.fr.n;
		if (nf >= 2000000000 || this.framesFull)
			return;
		// No fixed cap below 2 billion frames, but stop recording before the
		// tab runs out of JavaScript heap (Chrome's performance.memory; the
		// frames' typed arrays live outside it and count on top): the
		// counters keep counting, only the flame timeline stops growing.
		if ((nf & 0xffff) === 0 && nf && typeof performance !== "undefined" && performance.memory &&
		    performance.memory.usedJSHeapSize + this.fr.bytes() > 0.8 * performance.memory.jsHeapSizeLimit) {
			this.framesFull = nf;
			return;
		}
		const irq = this.entryKind[node];
		if (isCall || irq) {
			this.pushFrame(node, t, irq, esp);
			return;
		}
		const st = this.stack;
		let k = st.length - 1;
		while (k >= 0 && this.fr.nodeOf(st[k]) !== node)
			k--;
		if (k >= 0) {
			while (st.length - 1 > k)
				this.popFrame(t);
			return;
		}
		// Into the middle of a symbol that is not on the stack. With ESP
		// unwinding, a top frame that lives above ESP is still running: this
		// is code it reached by a jump (a cold path, a function that falls
		// through into the next symbol), so the new frame goes below it.
		// Otherwise (no ESP, or the top is at our level) it replaces the top.
		if (st.length && !(this.espUnwind && this.fr.espOf(st[st.length - 1]) > esp))
			this.popFrame(t);
		this.pushFrame(node, t, 0, esp);
	}

	// Close every open frame (before a stretch the JIT runs unrecorded).
	closeStack(t) {
		for (const l of this.lanes.values()) {
			this.stack = l;
			while (l.length)
				this.popFrame(t);
		}
		while (this.stack.length)
			this.popFrame(t);
	}

	nodeName(id) {
		const n = this.symName.length;
		return id < n ? this.symName[id] : id === n ? "(BIOS, option ROM, setup)" : "(kernel range, no symbol)";
	}

	graphEnter(i, phys) {
		const n = this.symName.length;
		const node = i >= 0 ? i : phys < this.kernelLo ? n : n + 1;
		if (node === this.curNode)
			return;
		if (this.nodeFirst[node] < 0) {
			this.nodeFirst[node] = this.steps;
			this.nodeParent[node] = this.curNode;
			this.nodeOrder.push(node);
		}
		if (this.curNode >= 0) {
			const key = this.curNode * (n + 2) + node;
			let e = this.edges.get(key);
			if (!e) {
				e = { from: this.curNode, to: node, count: 0, calls: 0, first: this.steps };
				this.edges.set(key, e);
			}
			e.count++;
			if (i >= 0 && phys === this.symStart[i])
				e.calls++;
			this.recent.push(key);
			if (this.recent.length > 10)
				this.recent.shift();
		}
		this.stackEnter(node, i >= 0 && phys === this.symStart[i], this.steps - 1, this.espBefore >>> 0);
		this.curNode = node;
		this.graphVersion++;
	}

	symbolOf(phys) {
		const st = this.symStart;
		let a = 0, b = st.length;
		while (a < b) { const m = (a + b) >> 1; if (st[m] <= phys) a = m + 1; else b = m; }
		for (let i = a - 1; i >= 0 && i >= a - 8; i--)
			if (phys < this.symEnd[i])
				return i;
		return -1;
	}

	// The instruction at phys runs; `from` ran before it.
	enterSymbol(phys, from) {
		const i = this.symbolOf(phys);
		if (i >= 0) {
			this.curLo = this.symStart[i];
			this.curHi = this.symEnd[i];
		} else {
			// A gap between symbols, or outside the kernel: stay until a symbol starts.
			const st = this.symStart;
			let a = 0, b = st.length;
			while (a < b) { const m = (a + b) >> 1; if (st[m] <= phys) a = m + 1; else b = m; }
			this.curLo = phys;
			this.curHi = a < st.length ? st[a] : 2 ** 32;
			if (phys < this.kernelLo)
				this.curHi = Math.min(this.curHi, this.kernelLo);
		}
		this.graphEnter(i, phys);
		// Entering panic() is a milestone, not the end: a printk kernel
		// still prints "---[ end Kernel panic ...", which ends the run
		// (app.js), as does a halt with interrupts off or panicBudget more
		// instructions without either.
		if (i === this.panicSym && i >= 0 && !this.panicEntered)
			this.panicEntered = this.snapshot(phys);
		if (i !== this.curSym) {
			if (this.curSym !== -2) {
				this.history.push({ sym: this.curSym, last: from });
				if (this.history.length > 16)
					this.history.shift();
			}
			this.curSym = i;
		}
	}

	// The CPU is halted with interrupts off: the machine has stopped.
	markHalt() {
		if (!this.end)
			this.end = { kind: this.panicEntered ? "panic" : "halt", ...this.snapshot() };
	}

	// v86's JIT runs (or ran) code the stepper does not see: seen misses it.
	// beforeEntry: the fast-forward to the kernel entry.
	noteJit(beforeEntry) {
		if (beforeEntry)
			this.jitBeforeEntry = true;
		else
			this.jitAfterEntry = true;
	}

	// Whether an instruction started at physical address phys while stepping.
	executed(phys) {
		const seen = this.seen;
		return phys < seen.length * 8 ? !!(seen[phys >> 3] & (1 << (phys & 7))) : this.seenHigh.has(phys);
	}

	// How many addresses in [lo, hi) an instruction started at while stepping.
	countIn(lo, hi) {
		const seen = this.seen, end = Math.min(hi, seen.length * 8);
		let n = 0, a = lo;
		for (; a < end && (a & 7); a++)
			n += seen[a >> 3] >> (a & 7) & 1;
		for (; a + 8 <= end; a += 8)
			n += POPCOUNT8[seen[a >> 3]];
		for (; a < end; a++)
			n += seen[a >> 3] >> (a & 7) & 1;
		if (hi > seen.length * 8)
			for (const x of this.seenHigh)
				if (x >= lo && x < hi)
					n++;
		return n;
	}

	// Called when v86's JIT ran in between: the history has a gap.
	markGap() {
		if (this.curSym !== -2) {
			this.history.push({ sym: this.curSym, last: this.lastPhys });
			this.history.push({ sym: -3, last: -1 });
		}
		this.curSym = -2;
		this.curLo = 1;
		this.curHi = 0;
		this.curNode = -1; // no edge across a stretch the JIT ran
		this.closeStack(this.steps);
		this.jumpForget();
	}

	// No observed path across what the stepper did not run (the JIT, a
	// restored snapshot): the next instruction starts a new record.
	jumpForget() {
		this.jPend = -1;
		this.jLast = -1;
	}

	jumpRecord(from, to, kind) {
		const key = from * 4294967296 + to;
		let e = this.jumps.get(key);
		if (!e) {
			e = [from, to, 0, 0];
			this.jumps.set(key, e);
			const o = this.jumpOut.get(from), i = this.jumpIn.get(to);
			if (o) o.push(e); else this.jumpOut.set(from, [e]);
			if (i) i.push(e); else this.jumpIn.set(to, [e]);
		}
		e[2] |= 1 << kind;
		e[3]++;
		this.jumpCount++;
	}

	// After the instruction at phys (linear eip, first byte op) ran and
	// the next one is at linear next: whether it transferred control, and
	// how (recorded at the next fetch, where the target's physical address
	// is known). Bytes after the first are read at phys + k: an instruction
	// crossing into a page that is not physically next is misread.
	// far: the kind when an instruction that transfers nothing ends up far
	// away (an exception, or an interrupt v86 took within the step).
	jumpAfter(phys, eip, op, next, far) {
		const m = this.cpu.mem8;
		let n = 0, b = op, op16 = false;
		while (JOP_PREFIX[b] && n < INSN_MAX - 1) {
			if (b === 0x66)
				op16 = true;
			b = m[phys + ++n];
		}
		let kind = 0, len = 0;
		if (b === 0x0f) {
			const b2 = m[phys + n + 1];
			if (b2 >= 0x80 && b2 <= 0x8f) {
				kind = JK.jcc;
				len = n + 2 + (op16 ? 2 : 4);
			} else if (b2 === 0x0b || b2 === 0xb9 || b2 === 0xff)
				kind = JK.ud2;
			else if (b2 === 0x05 || b2 === 0x07 || b2 === 0x34 || b2 === 0x35)
				kind = JK.sys;
		} else if (b === 0xff) {
			const r = m[phys + n + 1] >> 3 & 7;
			kind = r === 2 ? JK.icall : r === 3 ? JK.lcall : r === 4 ? JK.ijmp : r === 5 ? JK.ljmp : 0;
		} else {
			kind = JOP[b];
			if (kind === JK.jcc || kind === JK.loop)
				len = n + 2;
			else if (b === 0xce) // into
				len = n + 1;
		}
		if (!kind) {
			if (((next - eip) >>> 0) > INSN_MAX) {
				this.jPend = phys;
				this.jPendKind = b === 0xf4 ? JK.irq : far;
			}
			return;
		}
		if (len && next === ((eip + len) | 0))
			return; // not taken
		this.jPend = phys;
		this.jPendKind = kind;
	}


	symLabel(i) {
		return i >= 0 ? this.symName[i] : i === -3 ? "(v86's JIT ran here: not recorded)" : "(no symbol)";
	}

	hookVga() {
		const blk = VGA_TEXT >>> MMAP_BLOCK_BITS;
		const cpu = this.cpu;
		const w8 = cpu.memory_map_write8[blk], w32 = cpu.memory_map_write32[blk];
		const text = this.text;
		cpu.memory_map_write8[blk] = (a, v) => {
			const o = a - VGA_TEXT;
			if (o >= 0 && o < VGA_WINDOW_BYTES) {
				text[o] = v;
				this.vgaDirty = true;
				if (!(o & 1))
					this.textCheck(o);
			}
			w8(a, v);
		};
		cpu.memory_map_write32[blk] = (a, v) => {
			const o = a - VGA_TEXT;
			for (let i = 0; i < 4; i++)
				if (o + i >= 0 && o + i < VGA_WINDOW_BYTES) { text[o + i] = v >>> (8 * i) & 0xff; this.vgaDirty = true; }
			for (let i = 0; i < 4; i += 2)
				if (o + i >= 0 && o + i < VGA_WINDOW_BYTES && !((o + i) & 1))
					this.textCheck(o + i);
			w32(a, v);
		};
	}

	// A character was written at window offset o: whether it completes one
	// of the TEXT_* lines. Cheap for every other character: the stop is
	// found at the very write (J3), where polling the screen found it up to
	// ~1 M instructions later (D0).
	textCheck(o) {
		const t = this.text, c = t[o];
		const before = (s) => {
			for (let k = 0, p = o - 2 * s.length; k < s.length; k++, p += 2)
				if (p < 0 || t[p] !== s.charCodeAt(k))
					return false;
			return true;
		};
		if (c === 0x2d) { // "-" of "]---"
			if (!(this.textSeen & TEXT_END) && before("]--") && this.panicText() !== null)
				this.textSeen |= TEXT_END;
		} else if (c === 0x3a) { // ":"
			if (!(this.textSeen & TEXT_PANIC) && before("Kernel panic - not syncing"))
				this.textSeen |= TEXT_PANIC;
		} else if (c === 0x20 && o % 160 === 22) { // the space after "(initramfs)" at a row's start
			if (!(this.textSeen & TEXT_PROMPT) && before("(initramfs)"))
				this.textSeen |= TEXT_PROMPT;
		}
	}

	// Characters of the first screen (bytes = VGA_TEXT_BYTES) or of the
	// whole text window.
	screenText(bytes = VGA_TEXT_BYTES) {
		let s = "";
		for (let i = 0; i < bytes; i += 2)
			s += String.fromCharCode(this.text[i]);
		return s;
	}

	// "---[ end Kernel panic - not syncing: MSG ]---" anywhere in the text
	// window (distro kernels print it and then spin): the message, or null.
	panicText() {
		const m = /end Kernel panic - not syncing: (.*?) *\]---/.exec(this.screenText(VGA_WINDOW_BYTES));
		return m ? m[1].replace(/ {2,}/g, " ").trim() : null;
	}

	timers() {
		const cpu = this.cpu;
		const t = cpu.run_hardware_timers(cpu.acpi_enabled[0], performance.now());
		cpu.handle_irqs();
		return t;
	}

	halted() { return this.cpu.in_hlt[0] !== 0; }
	// hlt with IF clear: only an NMI or reset wakes it.
	dead() { return this.halted() && !(this.cpu.get_eflags() & 0x200); }

	// R2: the stepper is back at a count it has recorded past.
	behind() { return this.steps < this.ahead; }

	// R2: a checkpoint at count t was restored (reverse.js), coming from
	// count from. What was recorded after t (the end, the prompt, the exit,
	// panic() entered, milestones) waits in this.future; the kernel entry
	// is found again by the re-run. The recordings of paths, symbols, calls
	// and coverage stay as they were at the furthest count.
	rewind(t, from) {
		this.ahead = Math.max(this.ahead, from);
		const keep = (key, rec, i) => {
			if (!rec || rec.steps <= t)
				return false;
			this.future.push({ steps: rec.steps, key, i, rec });
			return true;
		};
		for (const key of ["end", "prompt", "exit", "panicEntered"])
			if (keep(key, this[key]))
				this[key] = null;
		this.milestones.forEach((m, i) => {
			if (keep("milestone", m.found, i))
				m.found = null;
		});
		this.future.sort((a, b) => a.steps - b.steps);
		if (this.sinceEntry >= t)
			this.sinceEntry = -1;
	}

	// R2: the re-run is at this.steps: what was recorded up to here comes
	// back. Returns the stop the first run made here: "end" (the panic's
	// last line, panicBudget), "prompt", "milestone", or "".
	catchUp() {
		let why = "";
		while (this.future.length && this.future[0].steps <= this.steps) {
			const f = this.future.shift();
			if (f.key === "milestone") {
				this.milestones[f.i].found = f.rec;
				why = why || "milestone";
			} else {
				this[f.key] = f.rec;
				if (f.key === "prompt")
					why = "prompt";
				else if (f.key === "end" && (f.rec.kind === "panic-text" || f.rec.kind === "panic-budget"))
					why = "end";
			}
		}
		return why;
	}

	// R2: back to the furthest count's recordings without going there (a
	// snapshot restored: it goes on from the recordings as they were).
	unrewind() {
		for (const f of this.future) {
			if (f.key === "milestone")
				this.milestones[f.i].found = f.rec;
			else
				this[f.key] = f.rec;
		}
		this.future = [];
		this.ahead = 0;
	}

	// R4: a snapshot taken at count t was restored: the stops recorded after
	// t (the end, the prompt, the exit, panic() entered) are not this
	// machine's yet, and the text mirror and the lines seen are the
	// snapshot's ({ text, textSeen, idleMs } from snapshots.js).
	restoreStops(t, stop) {
		for (const key of ["end", "prompt", "exit", "panicEntered"])
			if (this[key] && this[key].steps > t)
				this[key] = null;
		if (this.panicTextFrom !== undefined && this.panicTextFrom > t)
			this.panicTextFrom = undefined;
		if (stop.text && stop.text.length === this.text.length)
			this.text.set(stop.text);
		this.textSeen = stop.textSeen;
		this.idleMs = stop.idleMs;
		this.vgaDirty = false;
	}

	// R2: the run goes another way from this count than it went before
	// (keys typed after stepping back, the JIT, another clock): the
	// recordings ahead are not its future. The calls and task switches
	// recorded after here are dropped and the open ones closed here, so the
	// Calls timeline stays one run; then a gap, as for a stretch the JIT
	// runs. Counts of paths, symbols and coverage keep what was stepped.
	branch() {
		if (!this.behind() && !this.future.length)
			return;
		const t = this.steps;
		this.future = [];
		this.ahead = 0;
		const fr = this.fr;
		let k = fr.n;
		while (k > 0 && fr.start(k - 1) > t)
			k--;
		fr.truncate(k);
		let s = this.swStep.length;
		while (s > 0 && this.swStep[s - 1] > t)
			s--;
		this.swStep.length = this.swTask.length = this.swIrq.length = s;
		this.task = s ? this.swTask[s - 1] : 0;
		this.contFrames = 0;
		fr.clearChildren();
		for (let f = 0; f < k; f++) {
			const e = fr.end(f);
			if (e < 0 || e > t)
				fr.setEnd(f, t);
			this.contFrames += fr.cont(f);
		}
		for (let f = 0; f < k; f++) {
			const p = fr.parent(f);
			if (p >= 0)
				fr.addChild(p, fr.end(f) - fr.start(f));
		}
		this.stack = [];
		this.lanes = new Map();
		this.lane = -1;
		this.lastTop = -1;
		this.suspended = new Map();
		this.freshTask = false;
		this.tailEsp = -1;
		this.markGap();
		this.graphVersion++;
	}

	// Run up to n instructions. Stops early at stopPhys, an address or a
	// Set of them (before executing the instruction there, unless it is
	// the first one: with leave given, only unless it is the first one
	// and at leave, -1 for none), when the CPU halts, when a milestone is
	// reached, at the end (the panic's last line written, or panicBudget),
	// at the initramfs prompt (the first halt after it), or after
	// budgetMs. Returns a reason: "count", "addr", "hlt", "milestone",
	// "end", "prompt", "time".
	run(n, stopPhys = -1, budgetMs = Infinity, leave = undefined) {
		if (typeof revMark === "function")
			revMark(this, "step"); // R5: the run goes on stepped from here
		const r = this.runOn(n, stopPhys, budgetMs, leave);
		if (typeof MEV === "object")
			MEV.tick(this); // memevents.js checking mode
		return r;
	}

	runOn(n, stopPhys, budgetMs, leave) {
		let i0 = 0;
		if (this.steps < this.ahead) {
			const r = this.rerun(n, stopPhys, budgetMs, leave);
			if (r !== "ahead")
				return r;
			i0 = this.iters; // caught up: recording again from here
		}
		return this.record(n, stopPhys, budgetMs, leave, i0);
	}

	record(n, stopPhys, budgetMs, leave, i0) {
		// v86's cpu.reg32 etc. are Proxies that build a typed array on
		// every access; the loop reads wasm memory directly instead (byte
		// offsets of the globals as in v86 src/cpu.js).
		const cpu = this.cpu, read = this.ex.mkb_read_imm8, exec = this.ex.mkb_run;
		const mem = cpu.wasm_memory;
		let i32 = new Int32Array(mem.buffer), u8 = new Uint8Array(mem.buffer);
		const res = this.res >> 2;
		const seen = this.seen, lo = this.kernelLo, hi = this.kernelHi;
		const t0 = realNow();
		const stopSet = stopPhys instanceof Set ? stopPhys : null;
		for (let i = i0; i < n; i++) {
			if (i32.length === 0) { // the buffer was detached by a memory.grow
				i32 = new Int32Array(mem.buffer);
				u8 = new Uint8Array(mem.buffer);
			}
			if (u8[IN_HLT]) {
				// The initramfs shell's prompt is on the screen and the CPU
				// halts for the first time since: the shell waits for input.
				if ((this.textSeen & TEXT_PROMPT) && !this.prompt) {
					this.prompt = { exact: true, ...this.snapshot() };
					return "prompt";
				}
				const ip0 = i32[IP];
				let t = this.timers();
				// Virtual time: a halted CPU waits for the next timer event,
				// so let the clock jump there (as qemu -icount does).
				for (let k = 0; k < 64 && u8[IN_HLT] && typeof CLOCK === "object" && CLOCK.virtual &&
					(cpu.get_eflags() & 0x200); k++) {
					CLOCK.advance(Math.max(t, 0.001));
					this.idleMs += Math.max(t, 0.001);
					t = this.timers();
					i32 = new Int32Array(mem.buffer);
					u8 = new Uint8Array(mem.buffer);
				}
				i32 = new Int32Array(mem.buffer);
				u8 = new Uint8Array(mem.buffer);
				if (i32[IP] !== ip0 && this.jLast >= 0) {
					this.jPend = this.jLast;
					this.jPendKind = JK.irq;
				}
				if (u8[IN_HLT])
					return "hlt";
			}
			if ((this.steps & 1023) === 0) {
				if (realNow() - t0 > budgetMs)
					return "time";
				const ip0 = i32[IP];
				this.timers();
				i32 = new Int32Array(mem.buffer);
				u8 = new Uint8Array(mem.buffer);
				if (i32[IP] !== ip0 && this.jLast >= 0) {
					this.jPend = this.jLast;
					this.jPendKind = JK.irq;
				}
			}
			const eip = i32[IP];
			i32[PREV_IP] = eip;
			read(this.res);
			if (i32.length === 0) {
				i32 = new Int32Array(mem.buffer);
				u8 = new Uint8Array(mem.buffer);
			}
			if (i32[res] !== 0) { // fetch faulted: #PF delivered, nothing executed
				if (this.jPend < 0)
					this.jPend = this.jLast;
				this.jPendKind = JK.exc;
				continue;
			}
			const phys = (i32[EIP_PHYS] ^ eip) >>> 0;
			if ((stopSet ? stopSet.has(phys) : phys === stopPhys) && (i > 0 || (leave !== undefined && phys !== leave))) {
				i32[IP] = eip;
				return "addr";
			}
			if (this.jPend >= 0) {
				this.jumpRecord(this.jPend, phys, this.jPendKind);
				this.jPend = -1;
			}
			const op = i32[res + 1];
			// v86's sti with interrupts off runs the next instruction too
			// (the interrupt shadow), then takes a due interrupt.
			const sti = op === 0xfb && !(cpu.get_eflags() & 0x200);
			// int 0x80 with EAX = 1 (exit) or 252 (exit_group): note EBX.
			let exitCode = null;
			if (op === 0xcd && !this.exit && cpu.mem8[phys + 1] === 0x80 && (i32[EAX] === 1 || i32[EAX] === 252))
				exitCode = i32[EBX];
			// ESP when the instruction starts: at a symbol transition this is
			// the ESP the new code arrives with (call: just below the return
			// address; ret/iret: the caller's), used to unwind returns.
			this.espBefore = i32[ESP];
			exec(op | u8[IS_32] << 8);
			if (i32.length === 0) {
				i32 = new Int32Array(mem.buffer);
				u8 = new Uint8Array(mem.buffer);
			}
			i32[COUNTER] = i32[COUNTER] + 1;
			this.steps++;
			if (sti) {
				this.jumpAfter(phys + 1, eip + 1, cpu.mem8[phys + 1], i32[IP], JK.irq);
				this.jLast = phys + 1;
			} else {
				if (JOP[op] || op === 0x0f || op === 0xff || JOP_PREFIX[op])
					this.jumpAfter(phys, eip, op, i32[IP], JK.exc);
				else if (((i32[IP] - eip) >>> 0) > INSN_MAX) {
					// v86's hlt takes a due interrupt itself.
					this.jPend = phys;
					this.jPendKind = op === 0xf4 ? JK.irq : JK.exc;
				}
				this.jLast = phys;
			}
			if (phys < this.curLo || phys >= this.curHi)
				this.enterSymbol(phys, this.lastPhys);
			if (this.curNode >= 0)
				this.nodeSteps[this.curNode]++;
			this.lastPhys = phys;
			if (exitCode !== null)
				this.exit = { code: exitCode, ...this.snapshot(phys) };
			if (this.panicEntered && !this.end && this.steps - this.panicEntered.steps >= this.panicBudget) {
				this.end = { kind: "panic-budget", ...this.snapshot(phys) };
				return "end";
			}
			if (phys === lo && this.sinceEntry < 0)
				this.sinceEntry = this.steps - 1;
			if (phys < seen.length * 8) {
				const bit = 1 << (phys & 7);
				if (!(seen[phys >> 3] & bit)) {
					seen[phys >> 3] |= bit;
					this.unique++;
					if (phys >= lo && phys < hi)
						this.uniqueKernel++;
				}
			} else if (!this.seenHigh.has(phys)) {
				this.seenHigh.add(phys);
				this.unique++;
			}
			if (this.vgaDirty) {
				this.vgaDirty = false;
				// The instruction that wrote the panic's last line's last character.
				if ((this.textSeen & TEXT_END) && !this.end) {
					this.end = { kind: "panic-text", message: this.panicText(), exact: true, ...this.snapshot(phys) };
					return "end";
				}
				if (this.checkMilestones(phys))
					return "milestone";
			}
		}
		// n ran out on the instruction that halted after the prompt: the
		// prompt stop is here as well. The next run() would see the halt
		// first, but a replay puts the keys recorded at this count in before
		// it, and the keyboard interrupt wakes the CPU (R3).
		if (i32.length === 0)
			u8 = new Uint8Array(mem.buffer);
		if (u8[IN_HLT] && (this.textSeen & TEXT_PROMPT) && !this.prompt) {
			this.prompt = { exact: true, ...this.snapshot() };
			return "prompt";
		}
		return "count";
	}

	// R2: run() below this.ahead: what run() does to the machine (the
	// timers every 1,024 instructions, the idle clock while halted, v86's
	// counter, the kernel entry, the VGA text the hook mirrors) and its
	// stops (stopPhys and leave as in run(), and the stops the first run
	// made, from this.future), nothing recorded again. Returns as run(), or
	// "ahead" when it got to this.ahead with iterations left (this.iters
	// used).
	rerun(n, stopPhys, budgetMs, leave) {
		const cpu = this.cpu, read = this.ex.mkb_read_imm8, exec = this.ex.mkb_run;
		const mem = cpu.wasm_memory;
		let i32 = new Int32Array(mem.buffer), u8 = new Uint8Array(mem.buffer);
		const res = this.res >> 2, lo = this.kernelLo;
		const t0 = realNow();
		const stopSet = stopPhys instanceof Set ? stopPhys : null;
		const fresh = () => {
			i32 = new Int32Array(mem.buffer);
			u8 = new Uint8Array(mem.buffer);
		};
		let i = 0;
		for (; i < n; i++) {
			if (this.steps >= this.ahead) {
				this.iters = i;
				return "ahead";
			}
			if (i32.length === 0)
				fresh();
			if (this.future.length && this.future[0].steps <= this.steps && this.catchUp() === "prompt")
				return "prompt";
			if (u8[IN_HLT]) {
				let t = this.timers();
				for (let k = 0; k < 64 && u8[IN_HLT] && typeof CLOCK === "object" && CLOCK.virtual &&
					(cpu.get_eflags() & 0x200); k++) {
					CLOCK.advance(Math.max(t, 0.001));
					this.idleMs += Math.max(t, 0.001);
					t = this.timers();
					fresh();
				}
				fresh();
				if (u8[IN_HLT])
					return "hlt";
			}
			if ((this.steps & 1023) === 0) {
				if (realNow() - t0 > budgetMs)
					return "time";
				this.timers();
				fresh();
			}
			const eip = i32[IP];
			i32[PREV_IP] = eip;
			read(this.res);
			if (i32.length === 0)
				fresh();
			if (i32[res] !== 0)
				continue; // fetch faulted: #PF delivered, nothing executed
			const phys = (i32[EIP_PHYS] ^ eip) >>> 0;
			if ((stopSet ? stopSet.has(phys) : phys === stopPhys) && (i > 0 || (leave !== undefined && phys !== leave))) {
				i32[IP] = eip;
				return "addr";
			}
			exec(i32[res + 1] | u8[IS_32] << 8);
			if (i32.length === 0)
				fresh();
			i32[COUNTER] = i32[COUNTER] + 1;
			this.steps++;
			if (phys === lo && this.sinceEntry < 0)
				this.sinceEntry = this.steps - 1;
			this.vgaDirty = false;
			if (this.future.length && this.future[0].steps <= this.steps) {
				const why = this.catchUp();
				if (why)
					return why;
			}
		}
		return "count";
	}

	checkMilestones(phys) {
		let hit = false;
		let screen = null;
		for (const m of this.milestones) {
			if (m.found)
				continue;
			screen = screen || this.screenText();
			if (screen.includes(m.text)) {
				m.found = this.snapshot(phys);
				hit = true;
			}
		}
		return hit;
	}

	// Counters as of now; phys = the instruction that just ran.
	snapshot(phys = this.lastPhys) {
		return {
			steps: this.steps,
			sinceEntry: this.sinceEntry < 0 ? null : this.steps - this.sinceEntry,
			unique: this.unique,
			uniqueKernel: this.uniqueKernel,
			phys,
		};
	}

	regs() {
		const c = this.cpu;
		return {
			eip: c.get_real_eip() >>> 0,
			linear: c.instruction_pointer[0] >>> 0,
			eflags: c.get_eflags() >>> 0,
			eax: c.reg32[0] >>> 0, ecx: c.reg32[1] >>> 0, edx: c.reg32[2] >>> 0, ebx: c.reg32[3] >>> 0,
			esp: c.reg32[4] >>> 0, ebp: c.reg32[5] >>> 0, esi: c.reg32[6] >>> 0, edi: c.reg32[7] >>> 0,
			es: c.sreg[0], cs: c.sreg[1], ss: c.sreg[2], ds: c.sreg[3], fs: c.sreg[4], gs: c.sreg[5],
			cr0: c.cr[0] >>> 0, cr2: c.cr[2] >>> 0, cr3: c.cr[3] >>> 0, cr4: c.cr[4] >>> 0,
			cpl: c.cpl[0], is32: new Uint8Array(c.wasm_memory.buffer, 804, 1)[0],
			hlt: c.in_hlt[0] !== 0,
		};
	}

	// Physical address of the next instruction, or -1 when it is not mapped.
	// read_imm8 caches the translation of EIP's page (last_virt_eip,
	// eip_phys); other pages go through translate_address_system_read,
	// which traps on a fault (caught: -1).
	nextPhys() {
		const c = this.cpu;
		const lin = c.instruction_pointer[0];
		if (!(c.cr[0] & 0x80000000))
			return lin >>> 0;
		if (((lin & ~0xfff) ^ c.last_virt_eip[0]) === 0)
			return (c.eip_phys[0] ^ lin) >>> 0;
		try {
			return c.translate_address_system_read(lin) >>> 0;
		} catch (e) {
			return -1;
		}
	}
}

// The constants other modules use (a module reading them copies them into
// its own consts: const { FR_BITS } = STEP_CONST).
const STEP_CONST = Object.freeze({ VGA_WINDOW_BYTES, TEXT_PANIC, TEXT_END, TEXT_PROMPT, EAX, EBX, ESP, FR_BITS,
	FR_SIZE, FR_MASK, FR_BIG, FR_BIG16, FR_IRQ, FR_CONT, FR_BYTES });

export { patchV86Wasm, setPatchV86Wasm, STEP_CONST, Frames, frStartAt, frEndAt, frSmallAt, FramePath, Stepper };
