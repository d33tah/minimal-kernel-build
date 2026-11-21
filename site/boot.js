"use strict";

// Booting the machine: v86 with the patched wasm, the stepper, the run
// from the reset vector.

import { CLOCK, GUEST, realNow } from "./clock.js";
import { withV, $, V86_KERNEL_ADDR, V86_INITRD_ADDR, kib } from "./util.js";
import { wasmPatchError, setWasmPatchError, image, setStepper, setEmuReady, setRunState, emulator, setUsedPages,
	setChangedPages, setEmuRam, initrd, setEmulator, emuRam, stepper, prefSpeed, entryPhase, setEntryPhase,
	setCounting, setCountStart, setCountMs, setCountSteps, startPaused, disasm, setStartPaused,
	provide } from "./page.js";
import { patchV86Wasm } from "./v86patch.js";
import { Stepper } from "./stepper.js";
import { graphReset } from "./graph.js";
import { initrdNote } from "./symimport.js";
import { setFfActive, ffEnabled, ffTarget, jitDetInstall, fastForward, ffAbort, ffInfo } from "./fastforward.js";
import { MEV } from "./memevents.js";
import { varsTaskNow } from "./vartasks.js";
import { setPageSnapView, setSnapDiff, renderSnapDiff } from "./snapdiff.js";
import { snapReady } from "./snapshots.js";
import { inputBoot, replayActive } from "./replay.js";
import { ram, checkImage, setStatus, kernelEnd } from "./header.js";
import { setScanCopy, setAutoscan } from "./scan.js";
import { computePageClasses, drawGrid } from "./memview.js";
import { setLive, inspect, insnIndex } from "./listing.js";
import { updateDebugUi, clockMips, finishEntry } from "./debugui.js";
import { vmKeys } from "./tabs.jsx";
import { codeSymbols, afterStop } from "./symview.js";
import { stopDriver, resetCounters, resume } from "./run.js";
import { dbgStatus } from "./dbgtab.js";

// v86.wasm with two internal functions exported for stepping (stepper.js).
// If the patch fails the page still boots, without the debugger
// (wasmPatchError).
// The first boot's bytes are fetched as the page starts (prefetchWasm,
// app.js), while it sets itself up; a later boot fetches them again.
let wasmEarly = null;

function prefetchWasm() {
	wasmEarly = fetch(withV("v86/v86.wasm")).then((r) => (r.ok ? r.arrayBuffer() : null)).catch(() => null);
}

async function loadWasm(env) {
	let bytes = null;
	if (wasmEarly) {
		bytes = await wasmEarly;
		wasmEarly = null;
	}
	if (!bytes)
		bytes = await (await fetch(withV("v86/v86.wasm"))).arrayBuffer();
	let code = bytes;
	try {
		code = patchV86Wasm(bytes).bytes;
		setWasmPatchError(null);
	} catch (e) {
		setWasmPatchError(String(e));
	}
	const { instance } = await WebAssembly.instantiate(code, env);
	return instance.exports;
}

// Boot can be pressed again before the last one is done: each call waits
// for every older machine to be destroyed, and only the newest call goes
// on (two at once left two machines and a fast-forward of the old one
// running on the new one's state: "fast-forwarding", nothing running).
let bootGen = 0;
let destroying = Promise.resolve();
async function boot() {
	if (!image)
		return;
	const gen = ++bootGen;
	stopDriver();
	// A fast-forward of the old machine stops at its next check (st !== stepper).
	setStepper(null);
	setEmuReady(false);
	setRunState("off");
	setFfActive(false);
	if (emulator) {
		const old = emulator;
		setEmulator(null);
		destroying = destroying.then(() => old.destroy()).catch((e) => console.warn("destroy:", e));
	}
	updateDebugUi();
	await destroying;
	if (gen !== bootGen)
		return;
	$("scan").disabled = true;
	snapReady(false);
	$("footprint").textContent = "";
	setScanCopy(null);
	setUsedPages(null);
	setChangedPages(null);
	// A snapshot's page or diff belongs to the machine it was read from.
	setPageSnapView(null);
	setSnapDiff(null);
	renderSnapDiff();
	setEmuRam(ram());
	$("screen_text").textContent = "";
	resetCounters();
	updateDebugUi();
	const fatal = checkImage(image.hdr, ram()).some(([level]) => level === "error");
	// v86 throws a RangeError copying a kernel or an initrd past the end of
	// RAM: nothing to start.
	if (V86_KERNEL_ADDR + image.hdr.payload > ram() || (initrd && V86_INITRD_ADDR + initrd.bytes.length > ram())) {
		setStatus("Not booted: " + image.name + " or its initrd does not fit in " + kib(ram()) +
			" of RAM (the header check says why). Pick more RAM.");
		return;
	}
	setStatus("Booting " + image.name + " with " + kib(ram()) + "...");
	try {
		// A new machine starts at virtual time 0 (or real time).
		CLOCK.reset(clockMips() || 10);
		GUEST.reset(new URLSearchParams(location.search));
		if (!clockMips())
			CLOCK.set(false, null);
		setEmulator(new V86({
			memory_size: emuRam,
			vga_memory_size: 256 * 1024,
			screen_container: $("screen_container"),
			wasm_fn: loadWasm,
			bios: { url: withV("v86/seabios.bin") },
			vga_bios: { url: withV("v86/vgabios.bin") },
			bzimage: { buffer: image.bytes.buffer.slice(0) },
			cmdline: $("cmdline").value,
			initrd: initrd ? { buffer: initrd.bytes.buffer.slice(initrd.bytes.byteOffset, initrd.bytes.byteOffset + initrd.bytes.length) } : undefined,
			autostart: false,
			disable_mouse: true,
			// The kernels make no sound on purpose: the minified Linux only touches
			// port 0x61 to gate PIT channel 2 for TSC calibration (speaker data
			// bit cleared), but v86's speaker emulation still clicks while that
			// channel runs. ?sound=1 turns the emulated PC speaker back on.
			disable_speaker: new URLSearchParams(location.search).get("sound") !== "1",
		}));
	} catch (e) {
		setStatus("v86 refused to start: " + e);
		return;
	}
	const em = emulator;
	em.add_listener("emulator-loaded", () => {
		if (em !== emulator)
			return;
		GUEST.install(em.v86.cpu);
		setEmuReady(true);
		vmKeys();
		setStatus("Running " + image.name + " with " + kib(ram()) +
			(fatal ? ". The header check predicts this will not work." : "."));
		$("scan").disabled = false;
		snapReady(true);
		try {
			setStepper(new Stepper(em, V86_KERNEL_ADDR, kernelEnd(image.hdr)));
			stepper.milestones = MILESTONES.map((text) => ({ text, found: null }));
			stepper.setSymbols(codeSymbols());
			stepper.onTaskLeave = varsTaskNow;
			// ?unwind=0: the old reconstruction from symbol transitions only.
			stepper.espUnwind = new URLSearchParams(location.search).get("unwind") !== "0";
			// ?panicbudget=N: instructions after panic() entry before giving up.
			stepper.panicBudget = Number(new URLSearchParams(location.search).get("panicbudget")) || 50e6;
			// Before vmlinux is in place (the bzImage's relocation stub also runs
			// at 1 MiB) the address of EIP belongs to a vmlinux symbol only by
			// coincidence: name a symbol only when the bytes at EIP are the
			// build's instruction bytes.
			const symbolOf = stepper.symbolOf.bind(stepper);
			const mem = em.v86.cpu.mem8;
			stepper.symbolOf = (phys) => {
				const i = symbolOf(phys);
				if (i < 0 || !disasm)
					return i;
				const k = insnIndex(phys), ins = disasm.insns;
				if (k >= ins.length || ins[k][0] !== phys)
					return -1;
				const raw = ins[k][1];
				for (let b = 0; b < raw.length / 2; b++)
					if (mem[phys + b] !== parseInt(raw.substr(b * 2, 2), 16))
						return -1;
				return i;
			};
		} catch (e) {
			setStepper(null);
			dbgStatus("No debugger: " + (wasmPatchError || e));
		}
		graphReset(stepper);
		if (stepper) {
			CLOCK.set(!!clockMips(), stepper);
			jitDetInstall(stepper, em);
		}
		MEV.install(em, stepper); // memevents.js: before the BIOS's first instruction
		inputBoot(em); // keys go in between slices, logged (J4)
		// Default: step BIOS and setup at full stepping speed to the kernel
		// entry, then continue at prefSpeed (1k/s) and count live. With
		// "full", count the whole boot in the background, then run the JIT.
		$("dbg_speed").value = prefSpeed;
		setEntryPhase(!!stepper && prefSpeed !== "full");
		setCounting(!!stepper && (entryPhase || $("count").checked));
		initrdNote();
		$("entry_info").textContent = entryPhase ? "Stepping BIOS and setup to the kernel entry..." : "";
		delete $("entry_info").dataset.steps;
		setCountStart(realNow());
		setCountMs(0);
		setCountSteps(0);
		setAutoscan();
		setLive();
		computePageClasses();
		drawGrid();
		inspect();
		if (startPaused && stepper) {
			setStartPaused(false);
			setRunState("paused");
			dbgStatus("Paused at the reset vector.");
			afterStop();
			if (replayActive())
				resume(true); // the replayed run went on from here
		} else if (stepper && ffEnabled() && ffTarget()) {
			// BIOS, setup and the decompressor at JIT speed, then exactly to
			// the kernel's link entry; from there as before.
			const t = ffTarget();
			setRunState("paused");
			fastForward(t.phys, t.name).then((ok) => {
				if (em !== emulator)
					return;
				if (ffAbort) {
					afterStop();
					return;
				}
				if (!ok)
					dbgStatus("Fast-forward did not reach " + t.name + "; stepping from here." +
						(ffInfo && ffInfo.error ? " " + ffInfo.error : ""));
				else
					finishEntry();
				resume();
			});
		} else {
			resume();
		}
	});
}

// The lines whose first appearance the stepper counts to (Stepper.milestones).
const MILESTONES = ["Hello, World!", "Still alive"];

// The page API's entries of this module (page.js).
provide({ boot });

export { boot, prefetchWasm };
