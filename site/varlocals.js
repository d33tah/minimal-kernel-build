"use strict";

// The Debugger's parameters and locals of the call frames (vars.js).

import { emuRam, stepper } from "./page.js";
import { DWT } from "./dwarfread.js";
import { dwStrip, dwTypeName, dwTypeSize, dwLocAt } from "./dwarflocals.js";
import { dwSetFrameBase, dwLocation } from "./dwexpr.js";
import { srcLinkEl } from "./srclinks.js";
import { pageWalk } from "./cpustate.js";
import { varsCell, varsRead, le, varsRow } from "./varseval.js";

// ---- parameters and locals of the call frames -------------------------------
//
// At a pause, every frame the call stack unwound (symview.js unwindHere: EIP
// for the innermost, the call instruction for the others) gets the
// parameters and locals the DWARF has there (dbginfo.js dbgLocals: the
// scopes around that address, inlined calls as frames of their own, like
// gdb's backtrace), each read through its location (dwexpr.js dwLocation:
// a register of that frame, memory at an offset of the frame base, the
// CFA from the call frame information, a computed value, pieces) and
// shown with the Variables panel's rows. What the DWARF or the frame does
// not have says so: optimized out, a register a callee may have changed,
// a value only described at the function's entry.

// "frame index:function" -> whether the user left that frame open (the
// innermost one is open unless closed).
const localsOpen = new Map();

// Bytes at virtual va through the page tables, or null.
function localsReadBytes(va, n) {
	const cpu = stepper && stepper.cpu;
	if (!cpu || n > 4096)
		return null;
	const out = new Uint8Array(n);
	for (let k = 0; k < n;) {
		const w = pageWalk(cpu, (va + k) >>> 0);
		if (w.phys === null)
			return null;
		const run = Math.min(n - k, 4096 - ((va + k) & 0xfff));
		if (w.phys + run > cpu.mem8.length)
			return null;
		out.set(cpu.mem8.subarray(w.phys, w.phys + run), k);
		k += run;
	}
	return out;
}

// The physical address of n bytes at virtual va when they are one run of
// RAM (what varsRow reads), else -1.
function localsPhys(va, n) {
	const cpu = stepper && stepper.cpu;
	const a = cpu ? pageWalk(cpu, va >>> 0).phys : null, b = cpu ? pageWalk(cpu, (va + Math.max(n, 1) - 1) >>> 0).phys : null;
	return a !== null && b !== null && b - a === Math.max(n, 1) - 1 && a + n <= emuRam ? a : -1;
}

// The DWARF expression context of an unwound frame f.
function localsCtx(f, fb) {
	const ctx = {
		reg: (r) => f.regs && f.regs[r] !== undefined && f.regs[r] !== null ? f.regs[r] >>> 0 : undefined,
		readBytes: localsReadBytes,
		read: (a, n) => {
			const b = localsReadBytes(a, n);
			return b ? b.reduceRight((v, x) => v * 256 + x, 0) : undefined;
		},
		cfa: f.cfa === undefined || f.cfa === null ? undefined : f.cfa >>> 0,
	};
	dwSetFrameBase(ctx, fb ? dwLocAt(fb, f.lookup) : null);
	return ctx;
}

// One variable of a frame: { pa, how, why, state } for varsRow (pa < 0:
// nothing to show), state "ok", "partial" (some bytes known), "out" (the
// DWARF has no location here) or "unmapped" (it has one, a memory address
// the page tables do not map: e.g. EBP-based slots in a function's
// prologue, before it has set EBP).
function localsValue(T, v, f, ctx) {
	const size = dwTypeSize(T, v[1]) || 4;
	const l = dwLocAt(v[3], f.lookup);
	if (l.why)
		return { pa: -1, state: "out", why: v[2] & 2 ? "optimized out (nothing about it is left in this copy of the function)" : l.why };
	if (l.c !== undefined || l.cb) {
		const b = new Uint8Array(size);
		if (l.cb)
			b.set(l.cb.subarray(0, size));
		else
			for (let k = 0, x = BigInt(l.c); k < size; k++, x >>= 8n)
				b[k] = Number(BigInt.asUintN(8, x));
		return { pa: varsCell(b, new Uint8Array(size).fill(1), "a constant (DW_AT_const_value)", ""), state: "ok", how: "a constant" };
	}
	const r = dwLocation(l.expr, ctx, size);
	if (r.why)
		return { pa: -1, state: "out", why: r.why + (ctx.fbWhy && /frame base/.test(r.why) ? " (" + ctx.fbWhy + ")" : "") };
	if (r.mem !== undefined) {
		const pa = localsPhys(r.mem, size);
		if (pa >= 0)
			return { pa, state: "ok", how: r.how };
		const b = localsReadBytes(r.mem, size);
		if (!b)
			return { pa: -1, state: "unmapped", why: r.how + ": not mapped" };
		return { pa: varsCell(b, new Uint8Array(size).fill(1), r.how, ""), state: "ok", how: r.how };
	}
	const known = r.known.subarray(0, size).every(Boolean);
	return { pa: varsCell(r.bytes, r.known, r.how, r.whys.join("; ")), state: known ? "ok" : "partial", how: r.how,
		why: r.whys.join("; ") };
}

// The unsigned value of a scalar (integer, pointer, enum) at pa as a
// decimal string, null for anything else or bytes not known: what the
// tests compare with gdb.
function localsRaw(T, type, pa) {
	const s = dwStrip(T, type);
	const t = s >= 0 ? T.types[s] : null;
	if (!t || ![DWT.base, DWT.ptr, DWT.enum].includes(t[0]) || !(t[2] > 0 && t[2] <= 8))
		return null;
	const b = varsRead(pa, t[2]);
	return b ? le(b, t[2]).toString() : null;
}

// The variables of one local frame (an inlined call or the function).
function localsGroup(T, lf, k, f, ctx, key, fr) {
	const box = document.createElement("div");
	box.className = "lscope";
	box.dataset.func = lf.func || "";
	box.dataset.inlined = lf.inlined ? "1" : "";
	const head = document.createElement("div");
	head.className = "lhead";
	head.append((lf.func || "?") + "()" + (lf.inlined ? " inlined" : ""));
	if (fr)
		head.append(" ", srcLinkEl(fr.file, fr.line, "", f.lookup));
	box.append(head);
	const list = [...lf.params.map((v) => [v, "param"]), ...lf.locals.map((v) => [v, "local"])];
	for (const [v, kind] of list) {
		const d = document.createElement("div");
		d.className = "lvar";
		d.dataset.name = v[0];
		d.dataset.kind = kind;
		const x = localsValue(T, v, f, ctx);
		d.dataset.state = x.state;
		if (x.pa < 0) {
			const r = document.createElement("span");
			r.className = "wrow";
			const n = document.createElement("span");
			n.className = "wname";
			n.textContent = v[0];
			const ty = document.createElement("span");
			ty.className = "wtype";
			ty.textContent = dwTypeName(T, v[1]);
			const val = document.createElement("span");
			val.className = "wval lout";
			val.textContent = x.state === "unmapped" ? "<not mapped>" : "<optimized out>";
			r.append(n, " = ", val, " ", ty);
			const leaf = document.createElement("div");
			leaf.className = "wleaf";
			leaf.append(r);
			d.append(leaf);
			d.dataset.value = "";
		} else {
			d.append(varsRow(T, v[0], v[1], x.pa, null, key + "/" + k + "/" + v[0], 0));
			const val = d.querySelector(".wval");
			d.dataset.value = val ? val.textContent : "";
			const raw = localsRaw(T, v[1], x.pa);
			if (raw !== null)
				d.dataset.raw = raw;
		}
		const loc = document.createElement("span");
		loc.className = "lloc";
		loc.textContent = (kind === "param" ? "parameter, " : "") + (x.pa < 0 ? x.why : x.how + (x.why ? "; " + x.why : ""));
		(d.querySelector(".wrow") || d).append(" ", loc);
		box.append(d);
	}
	if (!list.length) {
		const n = document.createElement("div");
		n.className = "note";
		n.textContent = "no parameters or locals here";
		box.append(n);
	}
	return box;
}

export { localsOpen, localsCtx, localsGroup };
