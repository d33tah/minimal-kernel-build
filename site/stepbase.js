"use strict";

// The stepper's setup and its cold methods (stepper.js extends them with
// the ones that run per instruction): the constructor and the VGA text
// window it copies, symbols and the call graph's reset, rewind and branch
// bookkeeping, milestones, the registers and the next instruction.

import { Frames } from "./frames.js";

const VGA_TEXT = 0xb8000;
const VGA_TEXT_BYTES = 80 * 25 * 2;
// The whole 32 KiB text window: Linux's vgacon scrolls by moving the
// display start, so its text lands anywhere in 0xb8000-0xbffff.
const VGA_WINDOW_BYTES = 0x8000;

class StepperBase {
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
		this.sinceEntry = -1;    // steps when EIP first reached entryAt, -1 before
		// The kernel's entry: 1 MiB, or the link entry the fast-forward runs
		// to (Debian's 16 MiB: its bzImage's decompressor runs at 1 MiB, A3d).
		this.entryAt = kernelLo;
		this.unique = 0;         // distinct physical addresses executed
		this.uniqueKernel = 0;   // ... of which in [kernelLo, kernelHi)
		// A3c: seen holds addresses of another timeline (a restored snapshot,
		// a run that went another way), so a count taken from here is not
		// this point's: snapshot() marks it and the table says "not known".
		this.uniqueOff = false;
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
		this.stepToEnd = false;  // A3b: endAhead() after the run went another way
		// A3h: the run steps on at full speed with the count off, as it did
		// past the kernel entry: the boot stepped there (the entry phase) and
		// the stepping went on. A stop and Continue keep it; the JIT from
		// there was another run (the minified Linux halted at 2,129,295 for
		// its panic at 2,129,674; Debian's prompt 62 M instructions late).
		// A speed, count or clock picked decides again (runto.js).
		this.stepAtFull = false;
		// A3h: where a Step or a Run to stopped a run that goes on with the
		// JIT (-1: none since): stepping and the JIT deliver the timer's
		// interrupts at other instructions, so from there it is another run
		// than one not stopped (the minified Linux: Run to start_kernel, then
		// the JIT, halted at 2,129,295; the JIT from the reset vector at
		// 2,129,385). Continue says so (run.js).
		this.steppedOff = -1;
		this.panicEntered = null; // snapshot when EIP first entered panic()
		this.panicBudget = 50e6;  // instructions after that without an end
		this.lastTimers = 0;
		this.idleMs = 0;         // virtual time skipped while halted
		this.irq0 = 0;           // timer interrupts (IRQ0) raised by the PIT
		this.irq0Known = true;   // A3g: false after restoring a record whose count may be wrong
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
		// away (the page sets it: vartasks.js varsTaskNow reads the leaving task's
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

	// No observed path across what the stepper did not run (the JIT, a
	// restored snapshot): the next instruction starts a new record.
	jumpForget() {
		this.jPend = -1;
		this.jLast = -1;
	}


	symLabel(i) {
		return i >= 0 ? this.symName[i] : i === -3 ? "(v86's JIT ran here: not recorded)" : "(no symbol)";
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

	// A3b: this run has an end ahead of it, recorded or dropped when the run
	// went another way (a patch, a restored snapshot).
	endAhead() { return !this.end && (!!this.stepToEnd || this.future.some((f) => f.key === "end")); }

	// A3b: the stages reached so far, kept in a snapshot (restoreStops).
	stagesNow() {
		return { endAhead: this.endAhead(), found: this.milestones.map((m) => m.found), sinceEntry: this.sinceEntry,
			exit: this.exit, panicEntered: this.panicEntered, irq0: this.irq0, irq0Exact: this.irq0Known, jitBeforeEntry: this.jitBeforeEntry,
			stepAtFull: this.stepAtFull };
	}

	// A3h: the JIT ran a slice (the stepper saw none of its instructions):
	// the lines it wrote that the stages look for are reached, at a count
	// the page does not know ("run by the JIT"), so a row does not say "not
	// reached" under a line the screen shows.
	jitMilestones() {
		if (!this.vgaDirty)
			return false;
		this.vgaDirty = false;
		let hit = false, screen = null;
		for (const m of this.milestones) {
			if (m.found)
				continue;
			screen = screen || this.screenText();
			if (screen.includes(m.text)) {
				m.found = { jit: true, ...this.snapshot() };
				hit = true;
			}
		}
		return hit;
	}

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
		// A3g: whether an end lies ahead is the snapshot's run's, which its
		// record keeps (since A3b; also after a reload, when this machine has
		// recorded nothing): this machine's own end, recorded by a run on from
		// there, made a run that went on under the JIT step after a Restore
		// (the minified Linux's end at 2,129,674 for 2,129,295). Older
		// records have only this machine's.
		if (typeof stop.endAhead === "boolean")
			this.stepToEnd = stop.endAhead;
		else if (this.end && this.end.steps > t)
			this.stepToEnd = true;
		for (const key of ["end", "prompt", "exit", "panicEntered"])
			if (this[key] && this[key].steps > t)
				this[key] = null;
		if (this.panicTextFrom !== undefined && this.panicTextFrom > t)
			this.panicTextFrom = undefined;
		// A3d: a stage reached after t is not reached yet on this machine.
		for (const m of this.milestones)
			if (m.found && m.found.steps > t)
				m.found = null;
		if (stop.found)
			stop.found.forEach((f, i) => {
				if (f && f.steps <= t && this.milestones[i] && !this.milestones[i].found)
					this.milestones[i].found = f;
			});
		// The snapshot's: this machine's may be another boot's (A3d).
		if (typeof stop.sinceEntry === "number")
			this.sinceEntry = stop.sinceEntry;
		// A3e: whether the JIT ran the snapshot's way to the entry; this
		// machine's own boot may have done otherwise.
		if (typeof stop.jitBeforeEntry === "boolean")
			this.jitBeforeEntry = stop.jitBeforeEntry;
		// A3h: the way the snapshot's run went on at full speed (older
		// records: decided by the settings, as before).
		this.stepAtFull = stop.stepAtFull === true;
		this.steppedOff = -1;
		for (const key of ["exit", "panicEntered"])
			if (!this[key] && stop[key])
				this[key] = stop[key];
		if (stop.text && stop.text.length === this.text.length)
			this.text.set(stop.text);
		this.textSeen = stop.textSeen;
		this.idleMs = stop.idleMs;
		if (typeof stop.irq0 === "number")
			this.irq0 = stop.irq0;
		this.uniqueOff = true;
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
		this.uniqueOff = true;
		this.stepToEnd = this.stepToEnd || this.future.some((f) => f.key === "end");
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
			// Stepped back, the counts are the furthest point's (A3c).
			...(this.uniqueOff || this.behind() ? { uniqueOff: true } : {}),
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

export { VGA_TEXT, VGA_TEXT_BYTES, VGA_WINDOW_BYTES, StepperBase };
