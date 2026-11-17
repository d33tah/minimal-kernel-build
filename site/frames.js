"use strict";

// The Calls timeline's store, the frames Stepper.fr records (stepper.js),
// and FramePath, the walk over them that the Calls views use.

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

export { FR_BITS, FR_SIZE, FR_MASK, FR_BIG, FR_BIG16, FR_IRQ, FR_CONT, FR_BYTES, Frames, frStartAt, frEndAt,
	frSmallAt, FramePath };
