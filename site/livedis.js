"use strict";

// Live x86 disassembly of guest memory with iced-x86 1.21.0 (MIT,
// github.com/icedland/iced; fetched by fetch-v86.sh into site/iced/,
// SHA-256 checked). The npm build targets Node (require("fs") for the
// wasm, a synchronous WebAssembly.Module that browsers refuse for big
// modules), so loadIced() runs its JavaScript with a small require() shim
// and instantiates the wasm asynchronously. Loaded only when a listing
// needs it.

// The "live decode" toggle (Page tab): decode guest memory even where
// disasm.json has the build's instructions.
const liveDecode = () => { const el = document.getElementById("live_decode"); return !!(el && el.checked); };

let iced = null;
let icedLoading = null;
let icedError = null;

function loadIced() {
	if (iced || icedError)
		return Promise.resolve(iced);
	if (!icedLoading)
		icedLoading = (async () => {
			try {
				const [code, bytes] = await Promise.all([
					fetch(withV("iced/iced_x86.js")).then((r) => { if (!r.ok) throw new Error("iced_x86.js: HTTP " + r.status); return r.text(); }),
					fetch(withV("iced/iced_x86_bg.wasm")).then((r) => { if (!r.ok) throw new Error("iced_x86_bg.wasm: HTTP " + r.status); return r.arrayBuffer(); }),
				]);
				const tail = code.lastIndexOf("const path = require('path')");
				if (tail < 0)
					throw new Error("unexpected iced_x86.js");
				const body = code.slice(0, tail) +
					"\nmodule.exports.__imports = imports;\nmodule.exports.__setWasm = (e) => { wasm = e; };\n";
				const module = { exports: {} };
				const req = (name) => (name === "util" ? { TextDecoder, TextEncoder } : {});
				new Function("module", "exports", "require", "__dirname", body)(module, module.exports, req, "");
				const { instance } = await WebAssembly.instantiate(bytes, module.exports.__imports);
				module.exports.__setWasm(instance.exports);
				const lib = module.exports;
				const fmt = new lib.Formatter(lib.FormatterSyntax.Gas);
				fmt.gasShowMnemonicSizeSuffix = true; // movl, calll: as llvm-objdump prints
				fmt.spaceAfterOperandSeparator = true;
				fmt.smallHexNumbersInDecimal = false; // $0x9, not $9
				lib.fmt = fmt;
				iced = lib;
			} catch (e) {
				icedError = String(e);
				console.warn("iced-x86:", e);
			}
			if (typeof inspect === "function")
				inspect(); // redraw the listing that asked for it
			if (typeof renderContext === "function")
				renderContext();
			return iced;
		})();
	return icedLoading;
}

// Decode `bytes` (guest memory starting at physical `pa`, linked at
// virtual `va`) until `end` bytes or `max` instructions. Entries have the
// shape of disasm.json's: [paddr, "hex bytes", "instruction"].
function decodeLive(bytes, pa, va, bitness, end = bytes.length, max = Infinity) {
	if (!iced)
		return null;
	const out = [];
	const dec = new iced.Decoder(bitness, bytes.subarray(0, end), iced.DecoderOptions.None);
	dec.ip = BigInt(va >>> 0);
	try {
		let off = 0;
		while (dec.canDecode && out.length < max) {
			const ins = dec.decode();
			const len = ins.length;
			let raw = "";
			for (let i = 0; i < len; i++)
				raw += bytes[off + i].toString(16).padStart(2, "0");
			const text = ins.code === 0 ? "(bad)" : iced.fmt.format(ins); // Code.INVALID
			ins.free();
			out.push([pa + off, raw, text]);
			off += len;
		}
	} finally {
		dec.free();
	}
	return out;
}

// Virtual minus physical for the kernel's symbols (0xC0000000 for i386
// Linux, 0 for hello and for code below the kernel entry).
function virtOffset(pa) {
	if (pa < V86_KERNEL_ADDR || !symbols || !symbols.symbols.length)
		return 0;
	const s = symbols.symbols.find((x) => x.paddr >= V86_KERNEL_ADDR) || symbols.symbols[0];
	return (s.vaddr - s.paddr) >>> 0;
}

// Real-mode / 16-bit code: below the kernel entry, or the CPU in real mode.
function bitnessAt(pa) {
	const realMode = stepper ? !(stepper.cpu.cr[0] & 1) : false;
	return pa < V86_KERNEL_ADDR || realMode ? 16 : 32;
}

const isCodeSection = (cls) => cls === "text";

// Live instructions of page p: for each code symbol overlapping the page,
// decode from the symbol's start (so instruction boundaries are right) to
// the end of the symbol or the page; code in text sections outside any
// symbol from the page start (first lines flagged as maybe misaligned).
// Returns { insns: Map(pa -> entry), misaligned: Set(pa), bits } or null.
function liveInsnsForPage(p) {
	if (!iced || !emulator || !emuReady)
		return null;
	const lo = p * PAGE, hi = lo + PAGE;
	const insns = new Map(), misaligned = new Set();
	const bits = bitnessAt(lo);
	const off = virtOffset(lo);
	const code = symbols ? symbols.symbols.filter((s) => s.size > 0 && /^\.(init\.|head\.|exit\.)?text|^\.text/.test(s.section) &&
		s.paddr < hi && s.paddr + s.size > lo) : [];
	const textSecs = symbols ? symbols.sections.filter((s) => isCodeSection(s.class) && s.paddr < hi && s.paddr + s.size > lo) : [];
	const covered = new Uint8Array(PAGE);
	const run = (start, stop, flag) => {
		if (stop <= start || start >= emuRam)
			return;
		const bytes = emulator.read_memory(start, Math.min(stop, emuRam) - start);
		const list = decodeLive(bytes, start, (start + off) >>> 0, bits);
		let first = 3;
		for (const e of list) {
			if (e[0] >= lo && e[0] < hi && !insns.has(e[0])) {
				insns.set(e[0], e);
				if (flag && first-- > 0)
					misaligned.add(e[0]);
			}
			for (let i = Math.max(e[0], lo); i < Math.min(e[0] + e[1].length / 2, hi); i++)
				covered[i - lo] = 1;
		}
	};
	for (const s of code)
		run(Math.max(s.paddr, lo - 0x10000), Math.min(s.paddr + s.size, hi), false);
	// Text without a symbol (or no symbols at all, or code below 1 MiB when asked).
	if (!code.length && (textSecs.length || !symbols || lo < V86_KERNEL_ADDR)) {
		let a = textSecs.length ? Math.max(lo, Math.min(...textSecs.map((s) => s.paddr))) : lo;
		const b = textSecs.length ? Math.min(hi, Math.max(...textSecs.map((s) => s.paddr + s.size))) : hi;
		run(a, b, true);
	}
	return { insns, misaligned, bits };
}
