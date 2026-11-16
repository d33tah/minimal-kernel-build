// stepper.js's Frames (the Calls frames in typed-array chunks) against a
// plain-array model: random pushes, ends, child counts, truncations and
// clears, with values that fit no column (counts past 2^32, ends before
// starts, depths and tasks past 2^16, entry addresses that are no 32-bit
// number) mixed in. Every field of every frame must read back as the model
// has it, reads past the end give undefined, and full chunks cost
// FR_BYTES per frame.
// Usage: node site/test_frames.mjs [ROUNDS]
import { Frames, FR_SIZE, FR_BYTES, frStartAt, frEndAt, frSmallAt, FramePath } from "./stepper.js";

const rounds = Number(process.argv[2] || 40);
let seed = 4242;
const rnd = (n) => {
	seed = (seed * 1103515245 + 12345) & 0x7fffffff;
	return seed % n;
};
const FIELDS = ["start", "end", "node", "depth", "parent", "irq", "cont", "child", "esp", "task", "from"];
let failures = 0;
const fail = (msg) => {
	if (failures++ < 20)
		console.error("FAIL " + msg);
};

class Model {
	constructor() {
		for (const f of FIELDS)
			this[f] = [];
	}
	push(start, node, parent, irq, cont, task, from, esp) {
		const k = this.start.length;
		this.start.push(start);
		this.end.push(-1);
		this.node.push(node);
		this.depth.push(parent >= 0 ? this.depth[parent] + 1 : 0);
		this.parent.push(parent);
		this.irq.push(irq);
		this.cont.push(cont);
		this.child.push(0);
		this.esp.push(esp);
		this.task.push(task);
		this.from.push(from);
		return k;
	}
}

const compare = (fr, m, when) => {
	if (fr.n !== m.start.length)
		fail(when + ": n " + fr.n + ", want " + m.start.length);
	for (let k = 0; k < m.start.length; k++) {
		for (const f of FIELDS)
			if (fr[f](k) !== m[f][k]) {
				fail(when + ": frame " + k + " " + f + " " + fr[f](k) + ", want " + m[f][k]);
				return;
			}
		if (fr.espOf(k) !== m.esp[k] || fr.nodeOf(k) !== m.node[k] || fr.irqOf(k) !== m.irq[k]) {
			fail(when + ": frame " + k + ": espOf/nodeOf/irqOf differ from esp/node/irq");
			return;
		}
	}
	// The chunk scans the views use.
	let seen = 0;
	for (let c = 0; c < fr.chunkCount(); c++) {
		const base = fr.baseOf(c), ds = fr.startsRaw(c), de = fr.endsRaw(c), dd = fr.depthsRaw(c), dt = fr.tasksRaw(c);
		const par = fr.parentsOf(c), nodes = fr.nodesOf(c), fl = fr.flagsOf(c);
		for (let i = 0; i < fr.chunkLen(c); i++, seen++) {
			const k = c * FR_SIZE + i, s = frStartAt(fr, base, ds, i, k);
			if (s !== m.start[k] || frEndAt(fr, de, i, k, s) !== m.end[k] || par[i] !== m.parent[k] || nodes[i] !== m.node[k] ||
			    frSmallAt(fr, dd, i, k, "depth") !== m.depth[k] || frSmallAt(fr, dt, i, k, "task") !== m.task[k] ||
			    (fl[i] & 1) !== m.irq[k] || (fl[i] & 2) >> 1 !== m.cont[k] || fr.parentOf(k) !== m.parent[k]) {
				fail(when + ": chunk scan differs at frame " + k);
				return;
			}
		}
	}
	if (seen !== m.start.length)
		fail(when + ": chunk scans saw " + seen + " frames, want " + m.start.length);
	// FramePath: at each frame, its callers as a walk up the model's parents
	// sees them (symbols counted, the marked ones nearest last); the walk on
	// every 97th frame (the chains are thousands deep), length and parent
	// on all.
	const mark = (q) => (m.node[q] % 3 === 0 && !m.irq[q] ? 1 : 0);
	const path = new FramePath(fr, 70000, mark);
	for (let k = 0; k < m.start.length; k++) {
		path.toParent(m.parent[k], m.depth[k] - 1);
		if (path.len !== m.depth[k] || (m.parent[k] >= 0 && path.at[path.len - 1] !== m.parent[k])) {
			fail(when + ": FramePath at frame " + k + ": length " + path.len + ", want " + m.depth[k]);
			return;
		}
		if (k % 97) {
			path.push(k, m.node[k], mark(k));
			continue;
		}
		const up = [];
		for (let p = m.parent[k]; p >= 0; p = m.parent[p])
			up.push(p);
		const marked = up.filter((p) => mark(p)).reverse();
		const same = up.filter((p) => m.node[p] === m.node[k]).length;
		if (path.len !== up.length || path.count[m.node[k]] !== same || path.markedLen !== marked.length ||
		    marked.some((p, j) => path.marked[j] !== p) || up.some((p, j) => path.at[up.length - 1 - j] !== p)) {
			fail(when + ": FramePath differs at frame " + k + " (path " + path.len + ", callers " + up.length + ")");
			return;
		}
		path.push(k, m.node[k], mark(k));
	}
	for (const k of [-1, m.start.length, m.start.length + 1, FR_SIZE * 3])
		for (const f of FIELDS)
			if (fr[f](k) !== undefined)
				fail(when + ": " + f + "(" + k + ") is " + fr[f](k) + ", want undefined");
};

for (let round = 0; round < rounds; round++) {
	const fr = new Frames(), m = new Model();
	// A step count that grows like a run's, now and then past 2^32 or jumping.
	let t = round % 3 === 0 ? 2 ** 32 - 5000 : rnd(1000);
	const open = [];
	const ops = 2000 + rnd(150000);
	for (let i = 0; i < ops; i++) {
		const what = rnd(100);
		t += rnd(40);
		if (rnd(5000) === 0)
			t += 2 ** 32 + rnd(1000); // a JIT stretch longer than a chunk's span
		if (what < 50 || !open.length) {
			const parent = open.length && rnd(10) ? open[open.length - 1] : rnd(3) ? -1 : rnd(Math.max(1, m.start.length));
			const p = parent < m.start.length ? parent : -1;
			const node = rnd(70000);
			const irq = rnd(4) === 0 ? 1 : 0, cont = rnd(8) === 0 ? 1 : 0;
			const task = rnd(50) === 0 ? 65535 + rnd(3) : rnd(300);
			const from = rnd(30) === 0 ? -1 : rnd(200) === 0 ? 2 ** 32 + rnd(9) : rnd(200) === 0 ? 0xffffffff : rnd(2 ** 31) * 2 + rnd(2);
			const esp = rnd(2 ** 31) * 2 + rnd(2);
			const start = rnd(500) === 0 ? t - 10 - rnd(5) : t;
			const a = fr.push(start, node, p, irq, cont, task, from, esp), b = m.push(start, node, p, irq, cont, task, from, esp);
			if (a !== b)
				fail("push returned " + a + ", want " + b);
			open.push(a);
		} else if (what < 85) {
			const k = open.pop();
			// An end before the start (does not happen in a run, must still round-trip).
			const end = rnd(300) === 0 ? m.start[k] - 1 - rnd(3) : t;
			m.end[k] = end;
			const p = m.parent[k];
			if (rnd(2)) { // what popFrame does
				fr.close(k, end);
				if (p >= 0)
					m.child[p] += end - m.start[k];
			} else {
				fr.setEnd(k, end);
				if (p >= 0) {
					const d = rnd(400) === 0 ? 2 ** 32 + rnd(7) : end - m.start[k];
					fr.addChild(p, d);
					m.child[p] += d;
				}
			}
		} else if (what < 86) {
			const n = rnd(m.start.length + 1);
			fr.truncate(n);
			for (const f of FIELDS)
				m[f].length = n;
			while (open.length && open[open.length - 1] >= n)
				open.pop();
			open.splice(0, open.length, ...open.filter((k) => k < n));
		} else if (what < 87) {
			fr.clearChildren();
			m.child.fill(0);
		} else if (what < 90 && m.start.length) {
			// branch(): an end set again on a closed frame.
			const k = rnd(m.start.length);
			fr.setEnd(k, t);
			m.end[k] = t;
		}
	}
	compare(fr, m, "round " + round + " (" + ops + " operations, " + m.start.length + " frames)");
}

// A chain deeper than 2^16: the depth column's overflow.
{
	const fr = new Frames(), m = new Model();
	let p = -1;
	for (let k = 0; k < 70000; k++)
		p = fr.push(k, 1, p, 0, 0, 0, k, 0) === m.push(k, 1, p, 0, 0, 0, k, 0) ? k : -2;
	compare(fr, m, "deep chain");
	if (fr.maxDepth !== 69999)
		fail("deep chain: maxDepth " + fr.maxDepth);
}

// Memory: full chunks cost FR_BYTES per frame, and growing never copies.
{
	const fr = new Frames();
	const n = FR_SIZE * 8;
	const chunks = [];
	for (let k = 0; k < n; k++) {
		fr.push(k * 7, k & 1023, k & 15 ? k - 1 : -1, 0, 0, 1, k, 0x7000 + k);
		if ((k & (FR_SIZE - 1)) === 0)
			chunks.push(fr.cStart[fr.cStart.length - 1]);
	}
	if (!chunks.every((c, i) => fr.cStart[i] === c))
		fail("a chunk was replaced while growing");
	const per = fr.bytes() / n;
	if (per > FR_BYTES + 0.01)
		fail("bytes per frame " + per + ", want <= " + FR_BYTES);
	console.log("bytes per frame at " + n + " frames: " + per.toFixed(2));
}

if (failures) {
	console.error(failures + " failures");
	process.exit(1);
}
console.log("frames: " + rounds + " random rounds, deep chain and memory OK");
