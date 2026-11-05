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

const ffCounter = () => new Uint32Array(stepper.cpu.wasm_memory.buffer, 664, 1)[0];

// Run one v86 main_loop() (JIT, ~100,000 instructions or until hlt) and
// add what it ran to the stepper's count. Returns the instructions run.
// beforeEntry: the fast-forward to the kernel entry (for the coverage note).
function jitChunk(beforeEntry = false) {
	const st = stepper, cpu = st.cpu;
	if (cpu.in_hlt[0]) { // let the stepper's idle handling move the clock
		st.run(1, -1, 50);
		return 0;
	}
	const c0 = ffCounter();
	CLOCK.extraSteps = () => (ffCounter() - c0) >>> 0;
	try {
		cpu.main_loop();
	} finally {
		CLOCK.extraSteps = null;
	}
	const ran = (ffCounter() - c0) >>> 0;
	if (ran)
		st.noteJit(beforeEntry);
	// The CLOCK keeps its value: steps take over from extraSteps.
	st.steps += ran;
	return ran;
}

const ffYield = () => new Promise((r) => setTimeout(r, 0));

async function fastForward(target, name) {
	const st = stepper;
	const [lo, hi] = codeRange(target);
	const inside = () => { const p = st.nextPhys(); return p >= lo && p < hi; };
	const t0 = realNow();
	ffActive = true;
	ffAbort = false;
	ffInfo = { target, name, steps: 0, ms: 0, slices: 0, restores: 0, start: st.steps };
	updateDebugUi();
	let reached = inside();
	try {
		// The BIOS is stepped (about 1.4 M instructions, a fraction of a
		// second): v86 0.5.471 cannot restore a snapshot taken while SeaBIOS
		// is still setting up PCI (pci.js set_state reads a BAR that does not
		// exist yet). A target reached on the way is stopped at exactly.
		while (!reached && !ffAbort && st.steps < FF_STEP_FIRST && !st.dead()) {
			const r = st.run(FF_STEP_FIRST - st.steps, target, 100);
			if (r === "addr" || st.nextPhys() === target) {
				reached = true;
				break;
			}
			if (inside())
				break;
			await ffYield();
		}
		ffInfo.stepped = st.steps - ffInfo.start;
		while (!reached && !ffAbort && !st.dead()) {
			// save_state() fails before the BIOS has set up PCI (a device
			// without its BARs yet): run on until it works.
			let snap = null;
			for (let tries = 0; !snap && tries < 200 && !inside(); tries++) {
				try {
					snap = await emulator.save_state();
				} catch (e) {
					jitChunk(true);
				}
			}
			if (!snap)
				break;
			const at = { steps: st.steps, base: CLOCK.base, baseSteps: CLOCK.baseSteps };
			ffInfo.slices++;
			let ran = 0;
			while (ran < FF_SLICE && !ffAbort && !st.dead()) {
				ran += jitChunk(true);
				if (inside()) {
					reached = true;
					break;
				}
			}
			if (reached) {
				// Back to the slice start and step exactly to the target.
				try {
					await emulator.restore_state(snap);
				} catch (e) {
					ffInfo.error = "restore_state failed (" + e + "): stopped past the target";
					break;
				}
				st.steps = at.steps;
				CLOCK.base = at.base;
				CLOCK.baseSteps = at.baseSteps;
				ffInfo.restores++;
				ffInfo.jitSteps = st.steps - ffInfo.start;
				let r = "count";
				while (r !== "addr" && !ffAbort) {
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
		ffActive = false;
	}
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

// Run to `phys` with a software breakpoint: a HLT byte at the target, the
// JIT runs, and when the CPU halts right after it the byte is put back and
// EIP set to the target. The kernel's code must already be in memory.
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

async function fastRunTo(phys) {
	const st = stepper, cpu = st.cpu;
	if (st.nextPhys() === phys)
		return true;
	const t0 = realNow();
	ffActive = true;
	ffAbort = false;
	// The breakpoint goes in only once the target holds the build's code.
	// Before that the bytes there can be something else that the kernel
	// moves into place later: the minified Linux's bzImage is loaded at
	// 0x100000, where its vmlinux also runs, so a HLT written there at the
	// entry corrupted the payload being copied (Run to rest_init then
	// ended halted in exc_page_fault after 72,644 instructions). With
	// disasm.json, step (stopping at the target) until the instruction at
	// the target matches the build; without it (vmlinux elsewhere, as for
	// the Debian sample) arm right away as before.
	const want = expectedBytes(phys);
	if (want) {
		const same = () => emulator.read_memory(phys, want.length).every((b, i) => b === want[i]);
		while (!same() && !ffAbort && !st.dead()) {
			const r = st.run(10000, phys, 100);
			if (r === "addr" || st.nextPhys() === phys)
				break;
			await ffYield();
		}
		if (st.nextPhys() === phys) {
			ffActive = false;
			ffInfo = Object.assign(ffInfo || {}, { runTo: phys, runToMs: realNow() - t0 });
			return true;
		}
	}
	let orig = emulator.read_memory(phys, 1)[0];
	const arm = () => {
		const b = emulator.read_memory(phys, 1)[0];
		if (b !== 0xf4) { // (re)written by the guest since: remember the new byte
			orig = b;
			emulator.write_memory(new Uint8Array([0xf4]), phys);
		}
	};
	let hit = false;
	try {
		arm();
		let n = 0;
		while (!ffAbort && !st.dead()) {
			jitChunk();
			if (cpu.in_hlt[0] && st.nextPhys() === phys + 1 && emulator.read_memory(phys, 1)[0] === 0xf4) {
				emulator.write_memory(new Uint8Array([orig]), phys);
				cpu.instruction_pointer[0] = cpu.instruction_pointer[0] - 1;
				cpu.in_hlt[0] = 0;
				st.steps -= 1; // the HLT was not the kernel's instruction
				hit = true;
				break;
			}
			arm();
			if (++n % 50 === 0) {
				updateDebugUi();
				await ffYield();
			}
		}
	} finally {
		if (!hit && emulator.read_memory(phys, 1)[0] === 0xf4)
			emulator.write_memory(new Uint8Array([orig]), phys);
		ffActive = false;
	}
	ffInfo = Object.assign(ffInfo || {}, { runTo: phys, runToMs: realNow() - t0 });
	return hit;
}
