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
// speed the page runs v86's main_loop itself (app.js jitLoop) and the clock
// follows v86's instruction counter (fastforward.js jitChunk); with the
// "real" clock v86's own loop runs on real time.
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
		// extraSteps: instructions the JIT ran in the current fast-forward
		// slice, not yet added to the stepper (fastforward.js).
		const steps = (this.stepper ? this.stepper.steps : 0) + (this.extraSteps ? this.extraSteps() : 0);
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
	// Put the clock at t ms, counting from the stepper's current count (a
	// restored snapshot: its time and count).
	setTime(t) {
		this.base = t;
		this.baseSteps = this.stepper ? this.stepper.steps : 0;
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

// ---- the guest's date and random numbers (J2) ------------------------------
//
// Two more inputs reach the guest from the browser. v86's RTC reads
// Date.now() (rtc_time at construction, every timer() call, and register B
// writes that start the periodic, alarm and update interrupts; SeaBIOS turns
// the time of day into the BIOS tick count), and RDRAND reads
// crypto.getRandomValues() (v86's get_rand_int, one Int32Array(1) per call;
// Debian's kernel uses it). The NIC's MAC is Math.random() at construction.
// While the clock is virtual the RTC instead runs from a start date on the
// virtual time and those numbers come from a seeded generator, so a run
// repeats exactly; snapshots keep the start date and the generator's state.
// With the real-time clock the browser's date and random numbers are used.

const RealDate = Date;
const realGetRandomValues = crypto.getRandomValues.bind(crypto);

const GUEST = {
	DEFAULT_RTC: "2026-01-01T00:00:00Z",
	DEFAULT_SEED: 1,
	rtcStart: RealDate.parse("2026-01-01T00:00:00Z"), // the RTC's date at virtual time 0, ms since the epoch
	seed: 1,
	rng: 1,        // the generator's state (mulberry32: one 32-bit word)
	why: "",       // a ?rtc= or ?seed= that could not be read
	dateNow() { return this.rtcStart + CLOCK.now(); },
	next() {
		let t = this.rng = (this.rng + 0x6d2b79f5) >>> 0;
		t = Math.imul(t ^ (t >>> 15), t | 1);
		t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
		return (t ^ (t >>> 14)) >>> 0;
	},
	// A new machine: start date and seed from the URL (?rtc=ISO, ?seed=N).
	reset(params) {
		this.why = "";
		const rtc = params.get("rtc"), seed = params.get("seed");
		const t = rtc ? RealDate.parse(rtc) : NaN;
		if (rtc && !Number.isFinite(t))
			this.why = "?rtc=" + rtc + " is not a date (ISO 8601, e.g. " + this.DEFAULT_RTC + "). ";
		this.rtcStart = Number.isFinite(t) ? t : RealDate.parse(this.DEFAULT_RTC);
		let s = this.DEFAULT_SEED;
		if (seed !== null && /^\s*(0x[0-9a-f]+|\d+)\s*$/i.test(seed))
			s = Number(seed) >>> 0;
		else if (seed !== null)
			this.why += "?seed=" + seed + " is not a number. ";
		this.seed = this.rng = s;
	},
	// The RTC, the NIC and the VMware port of a machine just built
	// (emulator-loaded), before its first instruction.
	install(cpu) {
		const rtc = cpu.devices.rtc;
		if (CLOCK.virtual)
			rtc.rtc_time = rtc.last_update = this.dateNow();
		const timer = rtc.timer;
		rtc.timer = function (t, legacy) {
			if (!CLOCK.virtual)
				return timer.call(this, t, legacy);
			const now = RealDate.now;
			RealDate.now = guestDateNow;
			try {
				return timer.call(this, t, legacy);
			} finally {
				RealDate.now = now;
			}
		};
		// Register B also reads new Date() (the alarm), and VMware's
		// GETTIME the time zone: the whole Date for those two port handlers.
		guestDatePort(cpu.io.ports[0x71], "write8", () => rtc.cmos_index === 0x0b);
		guestDatePort(cpu.io.ports[0x5658], "read32", () => true);
		const net = cpu.devices.net;
		if (CLOCK.virtual && net && net.mac && net.memory) {
			for (let i = 3; i < 6; i++)
				net.mac[i] = this.next() & 0xff;
			for (let i = 0; i < 6; i++)
				net.memory[i << 1] = net.memory[(i << 1) | 1] = net.mac[i];
		}
	},
	// For snapshots: what a restore needs to go on the same way.
	state() { return { rtcStart: this.rtcStart, seed: this.seed, rng: this.rng }; },
	setState(s) {
		this.rtcStart = s.rtcStart;
		this.seed = s.seed;
		this.rng = s.rng;
	},
};

const guestDateNow = () => GUEST.dateNow();

class GuestDate extends RealDate {
	constructor(...a) {
		if (a.length)
			super(...a);
		else
			super(GUEST.dateNow());
	}
	static now() { return GUEST.dateNow(); }
	getTimezoneOffset() { return 0; }
}

// v86 calls its port handlers as handler.call(device, value).
function guestDatePort(entry, fn, when) {
	const orig = entry[fn];
	entry[fn] = function (v) {
		if (!CLOCK.virtual || !when())
			return orig.call(this, v);
		globalThis.Date = GuestDate;
		try {
			return orig.call(this, v);
		} finally {
			globalThis.Date = RealDate;
		}
	};
}

// v86's get_rand_int (RDRAND) is the only caller with an Int32Array(1).
crypto.getRandomValues = function (a) {
	if (CLOCK.virtual && a instanceof Int32Array && a.length === 1) {
		a[0] = GUEST.next() | 0;
		return a;
	}
	return realGetRandomValues(a);
};
