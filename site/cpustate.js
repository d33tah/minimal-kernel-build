"use strict";
// The Debugger tab's machine state beyond the general registers, read from
// the pinned v86 0.5.471 (commit 94faf3d6b) CPU object and its devices:
//   cpu.*: typed-array views on wasm memory (src/cpu.js, offsets in
//     src/rust/cpu/global_pointers.rs): segment caches, GDTR/IDTR, CR, DR,
//     sysenter MSRs, EFER, PAT, TSC, FPU, XMM, MXCSR.
//   PIC, local APIC, IOAPIC: Rust structs in wasm memory at
//     cpu.get_pic_addr_master/slave(), get_apic_addr(), get_ioapic_addr()
//     (#[repr(C)] layouts in src/rust/cpu/pic.rs, apic.rs, ioapic.rs).
//   PIT, RTC: JS objects in cpu.devices (src/pit.js, src/rtc.js).
// What v86 does not keep is shown as "not emulated" or "not kept".

const CR4_BITS = [[0, "VME"], [1, "PVI"], [2, "TSD"], [3, "DE"], [4, "PSE"], [5, "PAE"], [6, "MCE"], [7, "PGE"],
	[8, "PCE"], [9, "OSFXSR"], [10, "OSXMMEXCPT"], [11, "UMIP"], [16, "FSGSBASE"], [17, "PCIDE"], [18, "OSXSAVE"],
	[20, "SMEP"], [21, "SMAP"]];
const CR0_BITS = [[0, "PE"], [1, "MP"], [2, "EM"], [3, "TS"], [4, "ET"], [5, "NE"], [16, "WP"], [18, "AM"],
	[29, "NW"], [30, "CD"], [31, "PG"]];
const SEG_NAMES = ["ES", "CS", "SS", "DS", "FS", "GS", "TR", "LDTR"]; // v86 sreg order; TR = 6, LDTR = 7
const SYS_TYPES = { 1: "16-bit TSS (available)", 2: "LDT", 3: "16-bit TSS (busy)", 4: "16-bit call gate", 5: "task gate",
	6: "16-bit interrupt gate", 7: "16-bit trap gate", 9: "32-bit TSS (available)", 11: "32-bit TSS (busy)",
	12: "32-bit call gate", 14: "32-bit interrupt gate", 15: "32-bit trap gate" };

const x8 = (v) => "0x" + (v >>> 0).toString(16).padStart(8, "0");
const x4 = (v) => "0x" + (v & 0xffff).toString(16).padStart(4, "0");
const x2 = (v) => "0x" + (v & 0xff).toString(16).padStart(2, "0");
const bitsOf = (v, list) => list.filter(([b]) => v & (1 << b)).map(([, n]) => n).join(" ") || "none";

// Guest physical memory as 32-bit little-endian reads (null past RAM).
function phys32(cpu, a) {
	a >>>= 0;
	const m = cpu.mem8;
	if (a + 4 > m.length)
		return null;
	return (m[a] | m[a + 1] << 8 | m[a + 2] << 16 | m[a + 3] << 24) >>> 0;
}

// Linear -> physical through the current page tables (non-PAE 2-level
// with 4 MiB pages when CR4.PSE, or PAE 3-level). Returns the steps (each
// level's entry address, value and flags) and the physical address or
// null when not present.
function pageWalk(cpu, lin) {
	lin >>>= 0;
	const cr0 = cpu.cr[0] >>> 0, cr3 = cpu.cr[3] >>> 0, cr4 = cpu.cr[4] >>> 0;
	if (!(cr0 & 0x80000000))
		return { phys: lin, steps: [], note: "paging off (CR0.PG = 0): linear = physical" };
	const steps = [];
	const flags = (e, big) => ["P", "RW", "US", "PWT", "PCD", "A", "D", big ? "PS" : "PAT", "G"].filter((_, i) => e & (1 << i)).join(" ");
	if (cr4 & 0x20) { // PAE: CR3 -> 4 PDPTEs -> PD -> PT (64-bit entries, low 32 bits read)
		const pdpteA = (cr3 & ~0x1f) + ((lin >>> 30) & 3) * 8;
		const pdpte = phys32(cpu, pdpteA);
		steps.push({ level: "PDPTE", addr: pdpteA, value: pdpte, flags: pdpte === null ? "" : flags(pdpte, false) });
		if (pdpte === null || !(pdpte & 1))
			return { phys: null, steps };
		const pdeA = (pdpte & ~0xfff) + ((lin >>> 21) & 0x1ff) * 8;
		const pde = phys32(cpu, pdeA);
		steps.push({ level: "PDE", addr: pdeA, value: pde, flags: pde === null ? "" : flags(pde, true) });
		if (pde === null || !(pde & 1))
			return { phys: null, steps };
		if (pde & 0x80)
			return { phys: ((pde & 0xffe00000) | (lin & 0x1fffff)) >>> 0, steps, page: "2 MiB" };
		const pteA = (pde & ~0xfff) + ((lin >>> 12) & 0x1ff) * 8;
		const pte = phys32(cpu, pteA);
		steps.push({ level: "PTE", addr: pteA, value: pte, flags: pte === null ? "" : flags(pte, false) });
		if (pte === null || !(pte & 1))
			return { phys: null, steps };
		return { phys: ((pte & ~0xfff) | (lin & 0xfff)) >>> 0, steps, page: "4 KiB" };
	}
	const pdeA = (cr3 & ~0xfff) + (lin >>> 22) * 4;
	const pde = phys32(cpu, pdeA);
	steps.push({ level: "PDE", addr: pdeA, value: pde, flags: pde === null ? "" : flags(pde, true) });
	if (pde === null || !(pde & 1))
		return { phys: null, steps };
	if ((pde & 0x80) && (cr4 & 0x10))
		return { phys: ((pde & 0xffc00000) | (lin & 0x3fffff)) >>> 0, steps, page: "4 MiB" };
	const pteA = (pde & ~0xfff) + ((lin >>> 12) & 0x3ff) * 4;
	const pte = phys32(cpu, pteA);
	steps.push({ level: "PTE", addr: pteA, value: pte, flags: pte === null ? "" : flags(pte, false) });
	if (pte === null || !(pte & 1))
		return { phys: null, steps };
	return { phys: ((pte & ~0xfff) | (lin & 0xfff)) >>> 0, steps, page: "4 KiB" };
}

// Symbol at a linear (virtual) address, through the page tables.
function symAtLinear(st, lin) {
	const w = pageWalk(st.cpu, lin);
	if (w.phys === null)
		return "";
	const i = st.symbolOf(w.phys);
	if (i < 0)
		return "";
	const off = w.phys - st.symStart[i];
	return st.symName[i] + (off ? "+" + x2(off).slice(2) : "");
}

// 8-byte descriptor at linear address a (GDT/LDT/IDT entry).
function readDesc(cpu, a) {
	const w = pageWalk(cpu, a), w2 = pageWalk(cpu, a + 4);
	if (w.phys === null || w2.phys === null)
		return null;
	const lo = phys32(cpu, w.phys), hi = phys32(cpu, w2.phys);
	return lo === null || hi === null ? null : [lo, hi];
}

function decodeSegDesc(lo, hi) {
	const access = (hi >>> 8) & 0xff, p = access >> 7, dpl = (access >> 5) & 3, s = (access >> 4) & 1, type = access & 15;
	const base = ((lo >>> 16) | ((hi & 0xff) << 16) | (hi & 0xff000000)) >>> 0;
	let limit = (lo & 0xffff) | (hi & 0xf0000);
	const g = (hi >>> 23) & 1, db = (hi >>> 22) & 1;
	if (g)
		limit = (limit * 4096 + 4095) >>> 0;
	const kind = !s ? SYS_TYPES[type] || "system type " + type :
		(type & 8 ? "code" + (type & 2 ? " RX" : " X") + (type & 4 ? " conforming" : "") :
			"data" + (type & 2 ? " RW" : " R") + (type & 4 ? " expand-down" : "")) + (type & 1 ? " A" : "");
	return { p, dpl, s, type, base, limit, g, db, kind, access };
}

// ---- sections -----------------------------------------------------------

// Each section: [id, title, rows(st, ctx)] with rows [name, value, title?].
const CPU_SECTIONS = [
	["segments", "Segments (selector and hidden descriptor cache)", (st) => {
		const c = st.cpu, rows = [];
		for (let i = 0; i < 8; i++) {
			const sel = c.sreg[i], acc = c.segment_access_bytes[i];
			const dpl = (acc >> 5) & 3, type = acc & 15, s = (acc >> 4) & 1;
			if (i >= 6) { // v86 keeps no access byte for TR and LDTR
				rows.push([SEG_NAMES[i], x4(sel) + " base " + x8(c.segment_offsets[i]) + " limit " + x8(c.segment_limits[i]) +
					(i === 6 ? " (" + (c.tss_size_32[0] ? "32-bit" : "16-bit") + " TSS)" : "") + ", type/DPL not kept"]);
				continue;
			}
			const kind = !s ? "system type " + type : type & 8 ? "code" + (type & 2 ? " RX" : " X") : "data" + (type & 2 ? " RW" : " R");
			const db = i === 1 ? (c.is_32[0] ? "32-bit" : "16-bit") : i === 2 ? (c.stack_size_32[0] ? "32-bit stack" : "16-bit stack") : "D/B not kept";
			rows.push([SEG_NAMES[i], x4(sel) + (c.segment_is_null[i] ? " null" : "") + " base " + x8(c.segment_offsets[i]) +
				" limit " + x8(c.segment_limits[i]) + " access " + x2(acc) + " (" + kind + ", DPL " + dpl + ", P " + (acc >> 7) + ", " + db + ")",
				"v86 keeps base, limit (in bytes: granularity already applied, G not kept), access byte; D/B only for CS and SS"]);
		}
		return rows;
	}],
	["tables", "Descriptor tables (GDTR, IDTR, LDTR, TR)", (st) => {
		const c = st.cpu;
		return [
			["GDTR", "base " + x8(c.gdtr_offset[0]) + " limit " + x4(c.gdtr_size[0]) + " (" + Math.floor(((c.gdtr_size[0] & 0xffff) + 1) / 8) + " entries)"],
			["IDTR", "base " + x8(c.idtr_offset[0]) + " limit " + x4(c.idtr_size[0]) + " (" + Math.floor(((c.idtr_size[0] & 0xffff) + 1) / 8) + " gates)"],
			["LDTR", x4(c.sreg[7]) + " base " + x8(c.segment_offsets[7]) + " limit " + x8(c.segment_limits[7])],
			["TR", x4(c.sreg[6]) + " base " + x8(c.segment_offsets[6]) + " limit " + x8(c.segment_limits[6]) + " (" + (c.tss_size_32[0] ? "32-bit" : "16-bit") + " TSS)"],
		];
	}, (st, box) => tableButtons(st, box)],
	["control", "Control registers", (st) => {
		const c = st.cpu, cr0 = c.cr[0] >>> 0, cr4 = c.cr[4] >>> 0;
		const efer = c.efer[0] >>> 0;
		return [
			["CR0", x8(cr0) + " " + bitsOf(cr0, CR0_BITS)],
			["CR2", x8(c.cr[2]), "Last page-fault linear address"],
			["CR3", x8(c.cr[3]) + " (page directory at " + x8(c.cr[3] & ~0xfff) + ")"],
			["CR4", x8(cr4) + " " + bitsOf(cr4, CR4_BITS)],
			["EFER", x8(efer) + (efer & 0x800 ? " NXE" : "") + (efer & 1 ? " SCE" : ""), "IA32_EFER (MSR 0xc0000080), low 32 bits"],
			["CPL", String(c.cpl[0])],
		];
	}],
	["debug", "Debug registers", (st) => {
		const d = st.cpu.dreg, dr7 = d[7] >>> 0, rows = [];
		for (let i = 0; i < 4; i++) {
			const en = (dr7 >> (i * 2)) & 3, rw = (dr7 >>> (16 + i * 4)) & 3, len = (dr7 >>> (18 + i * 4)) & 3;
			rows.push(["DR" + i, x8(d[i]) + (en ? " enabled (" + ["exec", "write", "I/O", "read/write"][rw] + ", " + [1, 2, 8, 4][len] + " bytes)" : " off")]);
		}
		rows.push(["DR6", x8(d[6]) + " B0-3 " + (d[6] & 15).toString(2).padStart(4, "0") + (d[6] & 0x4000 ? " BS" : "")]);
		rows.push(["DR7", x8(dr7) + (dr7 & 0x2000 ? " GD" : "")]);
		rows.push(["note", "v86 stores DR0-DR7 (mov to/from DRn) but does not emulate breakpoints: no #DB from them"]);
		return rows;
	}],
	["msr", "MSRs", (st) => {
		const c = st.cpu, tsc = c.current_tsc[0] + c.current_tsc[1] * 4294967296;
		const pat = [c.pat[0] >>> 0, c.pat[1] >>> 0];
		return [
			["TSC", tsc ? tsc.toLocaleString("en") + " (stored at the last state save)" :
				"computed by v86 on each rdtsc from its clock; reading it here would change v86's rdtsc state, so it is not read"],
			["SYSENTER_CS", x4(c.sysenter_cs[0])], ["SYSENTER_ESP", x8(c.sysenter_esp[0])],
			["SYSENTER_EIP", x8(c.sysenter_eip[0]) + " " + symAtLinear(st, c.sysenter_eip[0])],
			["APIC_BASE", c.acpi_enabled[0] ? "0xfee00000" + (c.apic_enabled[0] ? " EN" : " (disabled)") : "0 (no APIC: v86 started without ACPI)"],
			["PAT", x8(pat[1]) + x8(pat[0]).slice(2)],
			["EFER", x8(c.efer[0])],
			["MISC_ENABLE", "0x1 (fixed: fast strings; writes ignored)"],
			["MTRRs", "not emulated (rdmsr/wrmsr of the MTRR MSRs are not implemented)"],
		];
	}],
	["fpu", "x87 FPU, MMX, SSE", (st) => {
		const c = st.cpu, rows = [];
		const sw = c.fpu_load_status_word() & 0xffff, cw = c.fpu_control_word[0], tw = c.fpu_load_tag_word() & 0xffff;
		const top = (sw >> 11) & 7;
		const used = c.fpu_stack_empty[0] !== 0xff;
		rows.push(["FCW", x4(cw) + " PC " + ["24-bit", "res", "53-bit", "64-bit"][(cw >> 8) & 3] + " RC " +
			["nearest", "down", "up", "zero"][(cw >> 10) & 3] + " masks " + (cw & 0x3f).toString(2).padStart(6, "0")]);
		rows.push(["FSW", x4(sw) + " TOP " + top + (sw & 0x80 ? " ES" : "") + (sw & 0x40 ? " SF" : "") + " C3-0 " +
			[14, 10, 9, 8].map((b) => (sw >> b) & 1).join("") + " exc " + (sw & 0x3f).toString(2).padStart(6, "0")]);
		rows.push(["FTW", x4(tw) + " (empty mask " + x2(c.fpu_stack_empty[0]) + ")"]);
		for (let i = 0; i < 8; i++) {
			const empty = (c.fpu_stack_empty[0] >> ((top + i) & 7)) & 1;
			rows.push(["ST" + i, empty ? "empty" : String(c.fpu_get_sti_f64(i))]);
		}
		rows.push(["FIP", x4(c.fpu_ip_selector[0]) + ":" + x8(c.fpu_ip[0]) + " opcode " + x4(c.fpu_opcode[0])]);
		rows.push(["FDP", x4(c.fpu_dp_selector[0]) + ":" + x8(c.fpu_dp[0])]);
		let mmxUsed = false;
		const mm = [];
		for (let i = 0; i < 8; i++) {
			const lo = c.fpu_st[i * 4] >>> 0, hi = c.fpu_st[i * 4 + 1] >>> 0;
			if (lo || hi)
				mmxUsed = true;
			mm.push(["MM" + i, x8(hi) + x8(lo).slice(2)]);
		}
		if (!used && !mmxUsed)
			rows.push(["ST0-7, MM0-7", "not used yet (all registers empty / zero)"]);
		else
			rows.push(...mm);
		let xmmUsed = false;
		const xmm = [];
		for (let i = 0; i < 8; i++) {
			const w = [3, 2, 1, 0].map((j) => c.reg_xmm32s[i * 4 + j] >>> 0);
			if (w.some((v) => v))
				xmmUsed = true;
			xmm.push(["XMM" + i, w.map((v) => x8(v).slice(2)).join(" ")]);
		}
		if (xmmUsed)
			rows.push(...xmm);
		else
			rows.push(["XMM0-7", "not used yet (all zero)"]);
		rows.push(["MXCSR", x8(c.mxcsr[0])]);
		if (!used)
			return rows.filter(([n]) => !/^ST\d$/.test(n));
		return rows;
	}],
	["irq", "Interrupts and timers", (st) => {
		const c = st.cpu, rows = [], mem = new Uint8Array(c.wasm_memory.buffer);
		rows.push(["IF", String((c.get_eflags() >> 9) & 1)]);
		for (const [name, addr] of [["PIC master", c.get_pic_addr_master()], ["PIC slave", c.get_pic_addr_slave()]]) {
			// pic.rs Pic0 (bytes): 0 irq_mask (1 = enabled: the inverse of the
			// IMR), 1 irq_map (vector base), 2 isr, 3 irr, 4 master, 5 dummy,
			// 6 expect_icw4, 7 state (ICW being written), 8 read_isr,
			// 9 auto_eoi, 10 elcr, 11 irq_value, 12 special_mask_mode.
			const p = mem.subarray(addr, addr + 13);
			rows.push([name, "IMR " + x2(~p[0]) + " IRR " + x2(p[3]) + " ISR " + x2(p[2]) + " base vector " + x2(p[1]) +
				" pending " + x2(p[3] & p[0]) + (p[9] ? " auto-EOI" : "") + (p[12] ? " special mask" : "") + " ELCR " + x2(p[10]) +
				(p[7] ? " (initialising, ICW " + (p[7] + 1) + ")" : "")]);
		}
		const pit = c.devices.pit;
		if (pit)
			for (let i = 0; i < 3; i++)
				rows.push(["PIT ch" + i, "mode " + pit.counter_mode[i] + " reload " + (pit.counter_reload[i] || 65536) +
					(pit.counter_reload[i] ? " (" + (1193182 / pit.counter_reload[i]).toFixed(1) + " Hz)" : "") +
					" count at its last load/latch " + pit.counter_start_value[i] + (pit.counter_enabled[i] ? "" : " (disabled)"),
					"v86 computes the current count from the elapsed time when read; shown: the value it started from"]);
		if (c.acpi_enabled[0]) {
			// apic.rs Apic (u32 index): 0 id, 1 divider, 3 initial count, 4 current count,
			// 6-7 timer_last_tick (f64), 8 LVT timer, 9 perf, 10 LINT0, 11 LINT1,
			// 12 error, 13 TPR, 16-23 IRR, 24-31 ISR, 40 spurious vector.
			const a = new Uint32Array(c.wasm_memory.buffer, c.get_apic_addr(), 46);
			const reg8 = (o) => Array.from(a.subarray(o, o + 8)).reverse().map((v) => (v >>> 0).toString(16).padStart(8, "0")).join("");
			rows.push(["LAPIC", (c.apic_enabled[0] ? "enabled" : "disabled") + " id " + a[0] + " TPR " + x2(a[13]) + " SVR " + x4(a[40]) +
				" LVT timer " + x8(a[8]) + " LINT0 " + x8(a[10]) + " LINT1 " + x8(a[11]) + " error " + x8(a[12])]);
			rows.push(["LAPIC timer", "initial " + a[3] + " current " + a[4] + " divider " + a[1]]);
			rows.push(["LAPIC IRR/ISR", "IRR " + reg8(16) + " ISR " + reg8(24)]);
			const io = new Uint32Array(c.wasm_memory.buffer, c.get_ioapic_addr(), 52);
			const unmasked = [];
			for (let i = 0; i < 24; i++)
				if (!(io[i] & 0x10000))
					unmasked.push(i + "→" + x2(io[i]) + (io[i] & 0x8000 ? " level" : ""));
			rows.push(["IOAPIC", "id " + io[49] + " IRR " + x8(io[50]) + " unmasked pins " + (unmasked.join(", ") || "none")]);
		} else
			rows.push(["APIC, IOAPIC", "not enabled (v86 runs this machine without ACPI)"]);
		const rtc = c.devices.rtc;
		if (rtc)
			rows.push(["RTC", new Date(rtc.rtc_time).toISOString().replace("T", " ").slice(0, 19) + " UTC, reg A " + x2(rtc.cmos_a) +
				" B " + x2(rtc.cmos_b) + " C " + x2(rtc.cmos_c) + (rtc.periodic_interrupt ? " periodic interrupt on" : "")]);
		return rows;
	}],
	["paging", "Paging", (st) => {
		const c = st.cpu, cr0 = c.cr[0] >>> 0, cr4 = c.cr[4] >>> 0;
		return [["mode", !(cr0 & 0x80000000) ? "off" : cr4 & 0x20 ? "PAE (3-level)" : "2-level" + (cr4 & 0x10 ? " with 4 MiB pages (PSE)" : "")],
			["CR3", x8(c.cr[3])]];
	}, (st, box) => pagingTools(st, box)],
];

// ---- tools inside sections ---------------------------------------------

function toolRow(box, html) {
	const d = document.createElement("div");
	d.className = "cpu_tool";
	d.innerHTML = html;
	box.append(d);
	return d;
}

function tableButtons(st, box) {
	const d = toolRow(box, '<button type="button" id="view_gdt">view GDT</button> <button type="button" id="view_idt">view IDT</button><pre id="cpu_table" class="mono"></pre>');
	d.querySelector("#view_gdt").addEventListener("click", () => { $("cpu_table").textContent = gdtText(G_STEPPER()); });
	d.querySelector("#view_idt").addEventListener("click", () => { $("cpu_table").textContent = idtText(G_STEPPER()); });
}

const G_STEPPER = () => stepper;

function gdtText(st) {
	const c = st.cpu, base = c.gdtr_offset[0] >>> 0, n = Math.floor(((c.gdtr_size[0] & 0xffff) + 1) / 8);
	const out = [];
	for (let i = 0; i < n; i++) {
		const e = readDesc(c, base + i * 8);
		if (!e) {
			out.push(x4(i * 8) + " not mapped");
			continue;
		}
		if (!e[0] && !e[1]) {
			out.push(x4(i * 8) + " null");
			continue;
		}
		const d = decodeSegDesc(e[0], e[1]);
		out.push(x4(i * 8) + " " + d.kind + " base " + x8(d.base) + " limit " + x8(d.limit) + " DPL " + d.dpl + " P " + d.p +
			(d.s && d.type & 8 ? (d.db ? " 32-bit" : " 16-bit") : "") + (d.g ? " G" : ""));
	}
	return out.join("\n") || "empty";
}

function idtText(st) {
	const c = st.cpu, base = c.idtr_offset[0] >>> 0, n = Math.floor(((c.idtr_size[0] & 0xffff) + 1) / 8);
	const out = [];
	for (let i = 0; i < n; i++) {
		const e = readDesc(c, base + i * 8);
		if (!e) {
			out.push(x2(i) + " not mapped");
			continue;
		}
		const access = (e[1] >>> 8) & 0xff;
		if (!(access & 0x80)) {
			if (e[0] || e[1])
				out.push(x2(i) + " not present");
			continue;
		}
		const off = ((e[0] & 0xffff) | (e[1] & 0xffff0000)) >>> 0, sel = e[0] >>> 16;
		out.push(x2(i) + " " + (SYS_TYPES[access & 15] || "type " + (access & 15)) + " " + x4(sel) + ":" + x8(off) +
			" DPL " + ((access >> 5) & 3) + " " + symAtLinear(st, off));
	}
	return out.join("\n") || "empty";
}

function pagingTools(st, box) {
	const d = toolRow(box, '<label>translate <input id="xlate_in" class="mono" size="12" placeholder="0xc1000000" aria-label="linear address to translate"></label> ' +
		'<button type="button" id="xlate_go">translate</button> <button type="button" id="pd_view">page directory</button><pre id="xlate_out" class="mono"></pre>');
	const go = () => { $("xlate_out").textContent = translateText(G_STEPPER(), $("xlate_in").value); };
	d.querySelector("#xlate_go").addEventListener("click", go);
	d.querySelector("#xlate_in").addEventListener("keydown", (e) => { if (e.key === "Enter") go(); });
	d.querySelector("#pd_view").addEventListener("click", () => { $("xlate_out").textContent = pageDirText(G_STEPPER()); });
}

// Accepts a number or a symbol name (its virtual address).
function parseLinear(st, text) {
	text = text.trim();
	if (/^(0x)?[0-9a-f]+$/i.test(text))
		return parseInt(text, 16) >>> 0;
	const i = st.symName.indexOf(text);
	if (i >= 0 && typeof symbols === "object" && symbols && symbols.symbols) {
		const s = symbols.symbols.find((x) => x.name === text);
		if (s)
			return s.vaddr >>> 0;
	}
	return null;
}

function translateText(st, text) {
	const lin = parseLinear(st, text);
	if (lin === null)
		return "Give a hex address or a symbol name.";
	const w = pageWalk(st.cpu, lin);
	const lines = ["linear " + x8(lin)];
	if (w.note)
		lines.push(w.note);
	for (const s of w.steps)
		lines.push(s.level + " at " + x8(s.addr) + " = " + (s.value === null ? "beyond RAM" : x8(s.value) + " " + s.flags));
	lines.push(w.phys === null ? "not mapped (page fault)" : "physical " + x8(w.phys) + (w.page ? " (" + w.page + " page)" : ""));
	if (w.phys !== null) {
		const i = st.symbolOf(w.phys);
		if (i >= 0)
			lines.push("in " + st.symName[i]);
	}
	return lines.join("\n");
}

function pageDirText(st) {
	const c = st.cpu, cr0 = c.cr[0] >>> 0, cr3 = c.cr[3] >>> 0, cr4 = c.cr[4] >>> 0;
	if (!(cr0 & 0x80000000))
		return "Paging is off.";
	const out = [];
	if (cr4 & 0x20) {
		for (let i = 0; i < 4; i++) {
			const e = phys32(c, (cr3 & ~0x1f) + i * 8);
			out.push("PDPTE " + i + " (" + x8(i << 30) + ") " + (e === null ? "beyond RAM" : x8(e) + (e & 1 ? " → PD " + x8(e & ~0xfff) : " not present")));
		}
		return out.join("\n");
	}
	let runStart = -1, runPhys = 0, runFlags = 0, runLen = 0;
	const flush = () => {
		if (runStart >= 0)
			out.push(x8(runStart << 22) + "-" + x8(((runStart + runLen) << 22) - 1) + " " + (runLen > 1 ? runLen + " PDEs, " : "") +
				(runFlags & 0x80 && cr4 & 0x10 ? "4 MiB pages from " + x8(runPhys) : "page tables from " + x8(runPhys)) +
				" flags " + x2(runFlags & 0xff));
		runStart = -1;
	};
	for (let i = 0; i < 1024; i++) {
		const e = phys32(c, (cr3 & ~0xfff) + i * 4);
		if (e === null || !(e & 1)) {
			flush();
			continue;
		}
		const big = e & 0x80 && cr4 & 0x10;
		const expect = runPhys + runLen * (big ? 0x400000 : 0x1000);
		if (runStart >= 0 && (e & 0xfff) === (runFlags & 0xfff) && ((e & ~0xfff) >>> 0) === expect >>> 0) {
			runLen++;
			continue;
		}
		flush();
		runStart = i;
		runPhys = (e & ~0xfff) >>> 0;
		runFlags = e & 0xfff;
		runLen = 1;
	}
	flush();
	return (out.length ? out : ["no present entries"]).join("\n");
}

// ---- rendering ------------------------------------------------------------

let cpuPrev = {};
function renderCpuState() {
	const host = $("cpu_state");
	if (!host)
		return;
	if (!stepper) {
		host.textContent = "";
		return;
	}
	if (!host.dataset.built) {
		host.dataset.built = "1";
		for (const [id, title, , tools] of CPU_SECTIONS) {
			const d = document.createElement("details");
			d.id = "cpu_" + id;
			const s = document.createElement("summary");
			s.textContent = title;
			const rows = document.createElement("div");
			rows.className = "regs mono cpu_rows";
			d.append(s, rows);
			if (tools)
				tools(stepper, d);
			d.addEventListener("toggle", () => { if (d.open) renderCpuState(); });
			host.append(d);
		}
	}
	for (const [id, , rows] of CPU_SECTIONS) {
		const d = $("cpu_" + id);
		if (!d.open)
			continue;
		const box = d.querySelector(".cpu_rows");
		box.textContent = "";
		let list;
		try {
			list = rows(stepper);
		} catch (e) {
			list = [["error", String(e)]];
		}
		for (const [name, value, title] of list) {
			const key = id + ":" + name;
			const el = document.createElement("div");
			el.dataset.reg = name;
			el.className = "wide";
			if (cpuPrev[key] !== undefined && cpuPrev[key] !== value)
				el.classList.add("chg");
			cpuPrev[key] = value;
			const b = document.createElement("b");
			b.textContent = name;
			el.append(b, " " + value);
			if (title)
				el.title = title;
			box.append(el);
		}
	}
}

// v86 0.5.471's CPU.set_state (src/cpu.js) assigns state[86..88] to
// this.last_result, this.fpu_status_word and this.mxcsr instead of writing
// them into wasm memory: after restore_state() those fields are plain
// arrays and wasm keeps the values from before the restore. last_result
// feeds the lazy arithmetic flags, so the restored CPU computed ZF, SF and
// PF from the wrong result (found by S1: EFLAGS 0x2 instead of 0x57 right
// after restoring a snapshot). Called after every restore_state(): copy the
// restored values into wasm memory and put views like v86's back.
const V86_STATE_VIEWS = [["last_result", Int32Array, 112], ["fpu_status_word", Uint16Array, 1040], ["mxcsr", Int32Array, 824]];

function v86View(T, memory, offset) {
	return new Proxy({}, {
		get(target, prop) {
			const b = new T(memory.buffer, offset, 1);
			const x = b[prop];
			return typeof x === "function" ? x.bind(b) : x;
		},
		set(target, prop, value) {
			new T(memory.buffer, offset, 1)[prop] = value;
			return true;
		},
	});
}

function v86RestoreFix(cpu) {
	for (const [name, T, offset] of V86_STATE_VIEWS) {
		const v = cpu[name];
		if (!(v instanceof T))
			continue; // still v86's view (the state had no such field)
		new T(cpu.wasm_memory.buffer, offset, 1)[0] = v[0];
		cpu[name] = v86View(T, cpu.wasm_memory, offset);
	}
}

// v86 0.5.471's PCI.set_state (src/pci.js) moves the I/O ports of every BAR
// slot whose saved value has bit 0 set, through device.pci_bars[slot]. A
// slot without a BAR (the IDE controller's primary/secondary channel with
// no disk on it) still holds its initial config-space bytes, bit 0 included,
// until SeaBIOS writes the BAR (pci_write32 then zeroes it). So a state saved
// at power-on, before the BIOS ran, threw "Cannot read properties of
// undefined (reading 'original_bar')" and did not restore (B1). While
// set_state runs, an empty BAR (size 0: no port to move) stands in for each
// missing one; the device's config space is restored as saved.
function v86PciFix(pci) {
	if (!pci || pci.mkbSetStateFix)
		return;
	const setState = pci.set_state;
	pci.set_state = function (state) {
		const holes = [];
		for (const dev of this.devices) {
			if (!dev || !dev.pci_bars)
				continue;
			for (let i = 0; i < dev.pci_bars.length; i++) {
				if (!dev.pci_bars[i]) {
					dev.pci_bars[i] = { size: 0, original_bar: 0, entries: [] };
					holes.push([dev, i]);
				}
			}
		}
		try {
			return setState.call(this, state);
		} finally {
			for (const [dev, i] of holes)
				dev.pci_bars[i] = undefined;
		}
	};
	pci.mkbSetStateFix = true;
}

// The page's v86.wasm is v86 0.5.471's source with v86-det.patch
// (gen-v86.sh, R4). Its set_tsc sets the TSC from the clock itself (v86's
// took the old offset off twice: a restored TSC came out shifted by the
// offset the machine had before the restore, R2/R3), so a restore with the
// virtual clock put back first gives the TSC as saved. Its
// jit_reset_hidden() puts what save_state() does not keep (compiled code,
// hotness, the JIT's maps, the free table indices, the TLB, the TSC's
// extrapolation) into one canonical state. Called after every restore and
// after every snapshot saved (snapshots.js), and logged with the run (J4):
// from a snapshot the JIT then runs as it did from there the first time
// (R3: as shipped, a restored machine ended its JIT slices elsewhere).
// Compiled blocks still queued (jitFlush) go live first, as they would
// before the next slice, so none lands in the canonical state later.
function v86ResetHidden(em, where) {
	const f = em.v86.cpu.wm.exports.jit_reset_hidden;
	if (typeof f !== "function")
		throw new Error("this v86.wasm has no jit_reset_hidden (not the gen-v86.sh build)");
	f(where === "restore" ? 1 : 0);
	// R5: the JIT checkpoints' spacing counts from the last reset.
	if (typeof REV === "object")
		REV.jitMs = 0;
}

// emulator.restore_state() with the v86 0.5.471 restore bugs worked
// around, then the hidden state reset (v86ResetHidden).
async function v86Restore(em, state) {
	v86PciFix(em.v86.cpu.devices.pci);
	if (stepper && jitPending(stepper))
		await jitFlush(stepper);
	await em.restore_state(state);
	v86RestoreFix(em.v86.cpu);
	v86ResetHidden(em, "restore");
}
