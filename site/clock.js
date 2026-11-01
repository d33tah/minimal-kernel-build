"use strict";

// Virtual time for v86, like qemu's -icount. Loaded before libv86.js.
//
// v86 0.5.471 takes all device time from one function, v86.microtick,
// which it sets at load time to performance.now.bind(performance): the PIT
// (timer IRQ0 and its counter reads), the RTC, the ACPI timer, the APIC
// timer and the TSC (the wasm import "microtick"). Replacing
// performance.now before libv86.js runs makes that function ours. While
// the stepper drives the CPU the clock is derived from the instruction
// count: now = base + steps / (mips * 1000) ms, so a timer tick arrives
// every so many instructions whatever the stepping speed; when the CPU
// halts, the stepper jumps the clock to the next timer event. At full JIT
// speed the clock follows real time (continuing from the virtual time).
// The page's own timing uses realNow().

const realNow = performance.now.bind(performance);

const CLOCK = {
	virtual: true,  // from page load: device init gets the same timestamps every run
	mips: 100,      // instructions per microsecond while virtual
	base: 0,        // ms at baseSteps
	baseSteps: 0,
	off: 0,         // real mode: now = realNow() + off
	stepper: null,
	now() {
		if (!this.virtual)
			return realNow() + this.off;
		const steps = this.stepper ? this.stepper.steps : 0;
		return this.base + (steps - this.baseSteps) / (this.mips * 1000);
	},
	// Switch modes or attach a stepper, keeping time continuous.
	set(virtual, stepper = this.stepper) {
		const t = this.now();
		this.stepper = stepper;
		this.virtual = virtual;
		this.base = t;
		this.baseSteps = stepper ? stepper.steps : 0;
		this.off = t - realNow();
	},
	// The CPU is halted: let time pass without instructions.
	advance(ms) {
		if (this.virtual)
			this.base += ms;
	},
	// A new machine: virtual time from 0 again.
	reset(mips) {
		this.virtual = true;
		this.mips = mips;
		this.base = 0;
		this.baseSteps = 0;
		this.stepper = null;
	},
};

performance.now = () => CLOCK.now();
