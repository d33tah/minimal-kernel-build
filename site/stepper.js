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
// patchV86Wasm() adds both to the export section (the code is untouched),
// and Stepper.run() does per instruction what v86's own interpreter loop
// (jit_run_interpreted in src/rust/cpu/cpu.rs) does:
//   previous_ip = eip; op = read_imm8(); run(op | is_32 << 8); counter++.

const STEP_EXPORTS = {
	mkb_read_imm8: /3cpu3cpu9read_imm8$/,
	mkb_run: /3gen11interpreter3run$/,
};

function wasmReader(b, p) {
	const r = {
		p,
		leb() {
			let v = 0, sh = 0, x;
			do { x = b[r.p++]; v += (x & 0x7f) * 2 ** sh; sh += 7; } while (x & 0x80);
			return v;
		},
		str() { const n = r.leb(); const s = new TextDecoder().decode(b.subarray(r.p, r.p + n)); r.p += n; return s; },
	};
	return r;
}

function lebBytes(v) {
	const out = [];
	do { let x = v & 0x7f; v = Math.floor(v / 128); if (v) x |= 0x80; out.push(x); } while (v);
	return out;
}

// Returns { bytes, found: {export: function index} } or throws.
function patchV86Wasm(buffer) {
	const b = new Uint8Array(buffer);
	const sections = [];
	const r = wasmReader(b, 8);
	while (r.p < b.length) {
		const start = r.p, id = b[r.p++], len = r.leb(), body = r.p;
		sections.push({ id, start, body, end: body + len });
		r.p = body + len;
	}
	const names = new Map();
	for (const s of sections) {
		if (s.id !== 0)
			continue;
		const q = wasmReader(b, s.body);
		if (q.str() !== "name")
			continue;
		while (q.p < s.end) {
			const sub = b[q.p++], len = q.leb(), end = q.p + len;
			if (sub === 1)
				for (let n = q.leb(); n > 0; n--) {
					const idx = q.leb();
					names.set(q.str(), idx);
				}
			q.p = end;
		}
	}
	const found = {};
	for (const [exp, re] of Object.entries(STEP_EXPORTS)) {
		const hits = [...names].filter(([n]) => re.test(n));
		if (hits.length !== 1)
			throw new Error("v86.wasm: " + hits.length + " functions match " + re);
		found[exp] = hits[0][1];
	}
	const ex = sections.find((s) => s.id === 7);
	if (!ex)
		throw new Error("v86.wasm has no export section");
	const q = wasmReader(b, ex.body);
	const count = q.leb();
	const entries = Array.from(b.subarray(q.p, ex.end));
	for (const [name, idx] of Object.entries(found)) {
		const nb = new TextEncoder().encode(name);
		entries.push(...lebBytes(nb.length), ...nb, 0, ...lebBytes(idx));
	}
	const body = [...lebBytes(count + Object.keys(found).length), ...entries];
	const head = [7, ...lebBytes(body.length)];
	const out = new Uint8Array(b.length - (ex.end - ex.start) + head.length + body.length);
	out.set(b.subarray(0, ex.start), 0);
	out.set(head, ex.start);
	out.set(body, ex.start + head.length);
	out.set(b.subarray(ex.end), ex.start + head.length + body.length);
	return { bytes: out, found };
}

const VGA_TEXT = 0xb8000;
const VGA_TEXT_BYTES = 80 * 25 * 2;
const MMAP_BLOCK_BITS = 17; // v86 src/const.js
// Byte offsets of CPU globals in wasm memory (v86 src/cpu.js), as indices.
const IP = 556 >> 2, PREV_IP = 560 >> 2, EIP_PHYS = 624 >> 2, COUNTER = 664 >> 2;
const IN_HLT = 616, IS_32 = 804;

// Drives a stopped v86 instance one instruction at a time and counts.
class Stepper {
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
		this.sinceEntry = -1;    // steps when EIP first reached kernelLo, -1 before
		this.unique = 0;         // distinct physical addresses executed
		this.uniqueKernel = 0;   // ... of which in [kernelLo, kernelHi)
		this.lastPhys = -1;
		this.seen = new Uint8Array((cpu.mem8.length + 7) >> 3);
		this.seenHigh = new Set();
		this.text = new Uint8Array(VGA_TEXT_BYTES);
		this.vgaDirty = false;
		this.milestones = [];    // { text, found: null | {...} }
		this.lastTimers = 0;
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
		if (i !== this.curSym) {
			if (this.curSym !== -2) {
				this.history.push({ sym: this.curSym, last: from });
				if (this.history.length > 16)
					this.history.shift();
			}
			this.curSym = i;
		}
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
	}

	symLabel(i) {
		return i >= 0 ? this.symName[i] : i === -3 ? "(full speed: not recorded)" : "(no symbol)";
	}

	hookVga() {
		const blk = VGA_TEXT >>> MMAP_BLOCK_BITS;
		const cpu = this.cpu;
		const w8 = cpu.memory_map_write8[blk], w32 = cpu.memory_map_write32[blk];
		const text = this.text;
		cpu.memory_map_write8[blk] = (a, v) => {
			const o = a - VGA_TEXT;
			if (o >= 0 && o < VGA_TEXT_BYTES) { text[o] = v; this.vgaDirty = true; }
			w8(a, v);
		};
		cpu.memory_map_write32[blk] = (a, v) => {
			const o = a - VGA_TEXT;
			for (let i = 0; i < 4; i++)
				if (o + i >= 0 && o + i < VGA_TEXT_BYTES) { text[o + i] = v >>> (8 * i) & 0xff; this.vgaDirty = true; }
			w32(a, v);
		};
	}

	screenText() {
		let s = "";
		for (let i = 0; i < VGA_TEXT_BYTES; i += 2)
			s += String.fromCharCode(this.text[i]);
		return s;
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

	// Run up to n instructions. Stops early at stopPhys (before executing
	// the instruction there, unless it is the first one), when the CPU
	// halts, when a milestone is reached, or after budgetMs. Returns a
	// reason: "count", "addr", "hlt", "milestone", "time".
	run(n, stopPhys = -1, budgetMs = Infinity) {
		// v86's cpu.reg32 etc. are Proxies that build a typed array on
		// every access; the loop reads wasm memory directly instead (byte
		// offsets of the globals as in v86 src/cpu.js).
		const cpu = this.cpu, read = this.ex.mkb_read_imm8, exec = this.ex.mkb_run;
		const mem = cpu.wasm_memory;
		let i32 = new Int32Array(mem.buffer), u8 = new Uint8Array(mem.buffer);
		const res = this.res >> 2;
		const seen = this.seen, lo = this.kernelLo, hi = this.kernelHi;
		const t0 = performance.now();
		for (let i = 0; i < n; i++) {
			if (i32.length === 0) { // the buffer was detached by a memory.grow
				i32 = new Int32Array(mem.buffer);
				u8 = new Uint8Array(mem.buffer);
			}
			if (u8[IN_HLT]) {
				this.timers();
				i32 = new Int32Array(mem.buffer);
				u8 = new Uint8Array(mem.buffer);
				if (u8[IN_HLT])
					return "hlt";
			}
			if ((this.steps & 1023) === 0) {
				if (performance.now() - t0 > budgetMs)
					return "time";
				this.timers();
			}
			const eip = i32[IP];
			i32[PREV_IP] = eip;
			read(this.res);
			if (i32.length === 0) {
				i32 = new Int32Array(mem.buffer);
				u8 = new Uint8Array(mem.buffer);
			}
			if (i32[res] !== 0)
				continue; // fetch faulted: #PF delivered, nothing executed
			const phys = (i32[EIP_PHYS] ^ eip) >>> 0;
			if (phys === stopPhys && i > 0) {
				i32[IP] = eip;
				return "addr";
			}
			exec(i32[res + 1] | u8[IS_32] << 8);
			if (i32.length === 0) {
				i32 = new Int32Array(mem.buffer);
				u8 = new Uint8Array(mem.buffer);
			}
			i32[COUNTER] = i32[COUNTER] + 1;
			this.steps++;
			if (phys < this.curLo || phys >= this.curHi)
				this.enterSymbol(phys, this.lastPhys);
			this.lastPhys = phys;
			if (phys === lo && this.sinceEntry < 0)
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
				if (this.checkMilestones(phys))
					return "milestone";
			}
		}
		return "count";
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
