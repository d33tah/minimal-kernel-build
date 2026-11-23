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
// patchV86Wasm() (v86patch.js) adds both to the export section (the code
// is untouched), and Stepper.run() does per instruction what v86's own
// interpreter loop (jit_run_interpreted in src/rust/cpu/cpu.rs) does:
//   previous_ip = eip; op = read_imm8(); run(op | is_32 << 8); counter++.

import * as clock from "./clock.js";
import { api } from "./page.js";
import * as jumps from "./jumps.js";
import { FR_BITS, FR_SIZE, FR_MASK, FR_BIG, FR_BIG16, FR_IRQ, FR_CONT, FR_BYTES } from "./frames.js";
import { VGA_WINDOW_BYTES, VGA_TEXT, VGA_TEXT_BYTES, StepperBase } from "./stepbase.js";

// A module's imported and exported bindings are cells: optimized code loads
// them on every use, where it folds a private const to its value (V8: a
// small method reading an exported const ran 3x slower than one reading a
// private one). Stepper.record (here) and Frames' methods (frames.js) run
// per instruction, so the constants they read are private to their module:
// the ones from other modules copied here, this module's own exported as
// one object (STEP_CONST). The cold methods are StepperBase's (stepbase.js).
const { realNow, CLOCK } = clock;
const { JK } = jumps;
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

// Drives a stopped v86 instance one instruction at a time and counts.
const POPCOUNT8 = Uint8Array.from({ length: 256 }, (_, b) => { let n = 0; for (; b; b &= b - 1) n++; return n; });

class Stepper extends StepperBase {

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
		// (debugui.js), as does a halt with interrupts off or panicBudget more
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

	// Run up to n instructions. Stops early at stopPhys, an address or a
	// Set of them (before executing the instruction there, unless it is
	// the first one: with leave given, only unless it is the first one
	// and at leave, -1 for none), when the CPU halts, when a milestone is
	// reached, at the end (the panic's last line written, or panicBudget),
	// at the initramfs prompt (the first halt after it), or after
	// budgetMs. Returns a reason: "count", "addr", "hlt", "milestone",
	// "end", "prompt", "time".
	run(n, stopPhys = -1, budgetMs = Infinity, leave = undefined) {
		if (typeof api.step.revMark === "function")
			api.step.revMark(this, "step"); // R5: the run goes on stepped from here
		const r = this.runOn(n, stopPhys, budgetMs, leave);
		if (typeof api.events.MEV === "object")
			api.events.MEV.tick(this); // memevents.js checking mode
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
		const seen = this.seen, lo = this.kernelLo, hi = this.kernelHi, entry = this.entryAt;
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
			if (phys === entry && this.sinceEntry < 0)
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
		const res = this.res >> 2, entry = this.entryAt;
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
			if (phys === entry && this.sinceEntry < 0)
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
}

// The constants other modules use (a module reading them copies them into
// its own consts: const { FR_BITS } = STEP_CONST).
const STEP_CONST = Object.freeze({ VGA_WINDOW_BYTES, TEXT_PANIC, TEXT_END, TEXT_PROMPT, EAX, EBX, ESP, FR_BITS,
	FR_SIZE, FR_MASK, FR_BIG, FR_BIG16, FR_IRQ, FR_CONT, FR_BYTES });

export { Stepper, STEP_CONST };
