"use strict";

// Fast-forward: run v86's JIT instead of stepping through the parts of the
// boot nobody watches (BIOS, setup, the bzImage's decompressor), up to the
// kernel's real link entry (startup_32 / _text in the symbols), then stop
// exactly there and continue as before.
//
// The JIT runs in slices of FF_SLICE instructions, measured with v86's
// instruction counter (wasm memory, offset 664; JIT code adds each block's
// instruction count when the block is left, the interpreter counts each
// instruction). Each slice starts with save_state(). After every
// main_loop() call (about 100,000 instructions) the page checks whether EIP
// is inside the target's code section; once it is, the slice's snapshot is
// restored and the stepper steps from there to the target exactly.
// Run to in the Debugger (fastRunTo) uses a software breakpoint instead
// (a HLT byte at the target, put back when hit), once the kernel's code is
// in memory.

const FF_SLICE = 20e6;
const FF_STEP_FIRST = 2e6;
let ffActive = false;    // a fast-forward or fast run-to is running
let ffAbort = false;     // Pause pressed during it
let ffInfo = null;       // last fast-forward: { target, name, steps, ms, slices, restores }
let ffBreaks = null;     // while a fast Run to runs: byte address -> [target, original byte] of its HLTs

const ffEnabled = () => new URLSearchParams(location.search).get("fastforward") !== "0";

// The kernel's link entry from the symbols (startup_32, else _text).
function ffTarget() {
	if (!symbols)
		return null;
	const s = symbols.symbols.find((x) => x.name === "startup_32" && x.paddr >= V86_KERNEL_ADDR) ||
		symbols.symbols.find((x) => x.name === "_text" && x.paddr >= V86_KERNEL_ADDR);
	return s ? { phys: s.paddr, name: s.name } : null;
}

// Code section holding phys: [lo, hi).
function codeRange(phys) {
	const sec = symbols && symbols.sections.find((s) => s.class === "text" && phys >= s.paddr && phys < s.paddr + s.size);
	return sec ? [sec.paddr, sec.paddr + sec.size] : [phys, phys + PAGE];
}

const ffCounter = (st) => new Uint32Array(st.cpu.wasm_memory.buffer, 664, 1)[0];

// Run one v86 main_loop() (JIT, ~100,000 instructions or until hlt) and
// add what it ran to the stepper's count. Returns the instructions run.
// beforeEntry: the fast-forward to the kernel entry (for the coverage note).
// st: the stepper of the machine the loop started on.
function jitChunk(st, beforeEntry = false) {
	const cpu = st.cpu;
	if (cpu.in_hlt[0]) { // let the stepper's idle handling move the clock
		st.run(1, -1, 50);
		return 0;
	}
	const c0 = ffCounter(st);
	CLOCK.extraSteps = () => (ffCounter(st) - c0) >>> 0;
	try {
		cpu.main_loop();
	} finally {
		CLOCK.extraSteps = null;
	}
	const ran = (ffCounter(st) - c0) >>> 0;
	if (ran) {
		st.noteJit(beforeEntry);
		st.markGap(); // the symbol history and the recorded call stack have a gap here
	}
	// The CLOCK keeps its value: steps take over from extraSteps.
	st.steps += ran;
	return ran;
}

const ffYield = () => new Promise((r) => setTimeout(r, 0));

async function fastForward(target, name) {
	// Boot replaces the machine while this runs (each await lets it in):
	// the loop then stops at its next check; it uses only st and em.
	const st = stepper, em = emulator;
	const gone = () => st !== stepper;
	const [lo, hi] = codeRange(target);
	const inside = () => { const p = st.nextPhys(); return p >= lo && p < hi; };
	const t0 = realNow();
	ffActive = true;
	ffAbort = false;
	ffInfo = { target, name, steps: 0, ms: 0, slices: 0, restores: 0, start: st.steps };
	speedReset();
	updateDebugUi();
	let reached = inside();
	try {
		// The BIOS is stepped (about 1.4 M instructions, a fraction of a
		// second): v86 0.5.471 cannot restore a snapshot taken while SeaBIOS
		// is still setting up PCI (pci.js set_state reads a BAR that does not
		// exist yet). A target reached on the way is stopped at exactly.
		while (!reached && !ffAbort && !gone() && st.steps < FF_STEP_FIRST && !st.dead()) {
			const r = st.run(FF_STEP_FIRST - st.steps, target, 100);
			if (r === "addr" || st.nextPhys() === target) {
				reached = true;
				break;
			}
			if (inside())
				break;
			updateDebugUi(); // the count of the stepped BIOS part, live
			await ffYield();
		}
		ffInfo.stepped = st.steps - ffInfo.start;
		while (!reached && !ffAbort && !gone() && !st.dead()) {
			// save_state() fails before the BIOS has set up PCI (a device
			// without its BARs yet): run on until it works.
			let snap = null;
			for (let tries = 0; !snap && tries < 200 && !inside(); tries++) {
				try {
					snap = await em.save_state();
				} catch (e) {
					jitChunk(st, true);
				}
				if (gone())
					break;
			}
			if (!snap || gone())
				break;
			const at = { steps: st.steps, base: CLOCK.base, baseSteps: CLOCK.baseSteps };
			ffInfo.slices++;
			let ran = 0;
			while (ran < FF_SLICE && !ffAbort && !st.dead()) {
				ran += jitChunk(st, true);
				if (inside()) {
					reached = true;
					break;
				}
			}
			if (reached) {
				// Back to the slice start and step exactly to the target.
				try {
					await em.restore_state(snap);
				} catch (e) {
					ffInfo.error = "restore_state failed (" + e + "): stopped past the target";
					break;
				}
				st.steps = at.steps;
				st.jumpForget();
				CLOCK.base = at.base;
				CLOCK.baseSteps = at.baseSteps;
				ffInfo.restores++;
				ffInfo.jitSteps = st.steps - ffInfo.start;
				let r = "count";
				while (r !== "addr" && !ffAbort && !gone()) {
					r = st.run(5e6, target, 200);
					if (st.nextPhys() === target)
						r = "addr";
					if (r === "hlt" && st.dead())
						break;
					await ffYield();
				}
			}
			ffInfo.steps = st.steps - ffInfo.start;
			ffInfo.ms = realNow() - t0;
			updateDebugUi();
			await ffYield();
		}
	} finally {
		if (!gone())
			ffActive = false;
	}
	if (gone())
		return false;
	ffInfo.steps = st.steps - ffInfo.start;
	ffInfo.ms = realNow() - t0;
	ffInfo.stopPhys = st.nextPhys();
	const ok = st.nextPhys() === target;
	// The kernel entry is where we stopped: "from entry" counts start here.
	if (ok && !ffInfo.runTo)
		st.sinceEntry = st.steps;
	ffInfo.reached = ok;
	return ok;
}

// Run to with software breakpoints: HLT bytes over the first instruction
// at each target, the JIT runs, and when the CPU halts right after one the
// bytes are put back and EIP set to that target. The kernel's code must
// already be in memory. Every byte of the instruction gets a HLT because
// v86's HLT with interrupts enabled handles an interrupt that is due at
// once instead of halting: the handler returns to the byte after the HLT
// (with one HLT, the middle of the target instruction: Run to
// kernel_execve in the -O0 minified Linux ran into garbage and oopsed).
// With the next byte a HLT too the CPU halts there; nothing of the target
// instruction has run, the interrupt came just before it.
// The build's bytes of the instruction at phys (disasm.json), or null.
function expectedBytes(phys) {
	if (!disasm || !disasm.insns || typeof insnIndex !== "function")
		return null;
	const i = insnIndex(phys), ins = disasm.insns[i];
	if (!ins || ins[0] !== phys)
		return null;
	const hexs = ins[1];
	const out = new Uint8Array(hexs.length / 2);
	for (let k = 0; k < out.length; k++)
		out[k] = parseInt(hexs.substr(k * 2, 2), 16);
	return out;
}

// targets: the physical addresses to stop at (the first one reached
// wins: "Run to file:line" sets one per breakpoint address of the line);
// label names them in the status line.
async function fastRunTo(targets, label) {
	const st = stepper, cpu = st.cpu, em = emulator;
	const gone = () => st !== stepper;
	const set = new Set(targets);
	if (set.has(st.nextPhys()))
		return true;
	const t0 = realNow();
	ffActive = true;
	ffAbort = false;
	ffInfo = Object.assign(ffInfo || {}, { runTo: label });
	speedReset();
	// The breakpoints go in only once the targets hold the build's code.
	// Before that the bytes there can be something else that the kernel
	// moves into place later: the minified Linux's bzImage is loaded at
	// 0x100000, where its vmlinux also runs, so a HLT written there at the
	// entry corrupted the payload being copied (Run to rest_init then
	// ended halted in exc_page_fault after 72,644 instructions). With
	// disasm.json, step (stopping at a target) until the instruction at
	// every target matches the build; without it (vmlinux elsewhere, as
	// for the Debian sample) arm right away as before.
	const want = targets.map((pa) => [pa, expectedBytes(pa)]).filter(([, b]) => b);
	if (want.length) {
		const same = () => want.every(([pa, b]) => em.read_memory(pa, b.length).every((x, i) => x === b[i]));
		while (!same() && !ffAbort && !gone() && !st.dead()) {
			const r = st.run(10000, set, 100);
			if (r === "addr" || set.has(st.nextPhys()))
				break;
			await ffYield();
		}
		if (gone())
			return false;
		if (set.has(st.nextPhys())) {
			ffActive = false;
			ffInfo = Object.assign(ffInfo || {}, { runTo: label, runToMs: realNow() - t0 });
			return true;
		}
	}
	// Byte -> [target, original byte] over each target's first instruction.
	const bp = new Map();
	for (const pa of targets) {
		const n = (expectedBytes(pa) || [0]).length;
		const now = em.read_memory(pa, n);
		for (let k = 0; k < n; k++)
			if (!bp.has(pa + k))
				bp.set(pa + k, [pa, now[k]]);
	}
	const arm = () => {
		for (const [a, x] of bp) {
			const b = em.read_memory(a, 1)[0];
			if (b !== 0xf4) { // (re)written by the guest since: remember the new byte
				x[1] = b;
				em.write_memory(new Uint8Array([0xf4]), a);
			}
		}
	};
	let hit = -1;
	try {
		ffBreaks = bp; // a listing drawn meanwhile says why these bytes differ
		arm();
		let n = 0;
		while (!ffAbort && !gone() && !st.dead()) {
			jitChunk(st);
			const at = st.nextPhys() - 1;
			if (cpu.in_hlt[0] && bp.has(at) && em.read_memory(at, 1)[0] === 0xf4) {
				const t = bp.get(at)[0];
				cpu.instruction_pointer[0] = cpu.instruction_pointer[0] - (at + 1 - t);
				cpu.in_hlt[0] = 0;
				st.steps -= at + 1 - t; // the HLTs were not the kernel's instructions
				ffInfo = Object.assign(ffInfo || {}, { runToWakeups: at - t });
				hit = t;
				break;
			}
			arm();
			if (++n % 50 === 0) {
				updateDebugUi();
				await ffYield();
			}
		}
	} finally {
		for (const [a, [, b]] of bp)
			if (em.read_memory(a, 1)[0] === 0xf4)
				em.write_memory(new Uint8Array([b]), a);
		if (ffBreaks === bp)
			ffBreaks = null;
		if (!gone())
			ffActive = false;
	}
	if (gone())
		return false;
	ffInfo = Object.assign(ffInfo || {}, { runTo: label, runToMs: realNow() - t0 });
	return hit >= 0;
}
