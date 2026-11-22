"use strict";

// Memory operands in the listings named by variable (vars.js,
// asmnames.js).

import { hex, escHtml } from "./util.js";
import { stepper, symbols, api } from "./page.js";
import { dwarfScopesAt } from "./dwarflocals.js";
import { asmOperandNames, asmBranchTarget } from "./asmnames.js";
import { dbgTypes, dbgHas, dbgCfiRow, dbgLocalGroups } from "./dbginfo.js";
import { dbgTitle } from "./dbgfacts.js";
import { kernelSymLookup } from "./livedis.js";
import { varsOffset } from "./varseval.js";

// ---- memory operands in the listings (asmnames.js) -------------------------------


// The names asmOperandNames needs, made once per listing render: the
// types (starting their load), symbols, locals and CFI shards (fetched
// as asked for), and at a pause the registers and CFA at EIP. null when
// there is nothing to name with.
function opEnv() {
	const ty = dbgHas() ? dbgTypes() : null;
	const T = ty && ty.state === "ok" ? ty.T : null;
	if (!symbols && !T)
		return null;
	const row = (pa) => {
		const r = dbgHas() ? dbgCfiRow(pa) : null;
		return r && r !== "loading" && !r.why ? r : null;
	};
	const scopes = (pa) => {
		const r = T ? dbgLocalGroups(pa) : null;
		return r && r.state === "ok" ? dwarfScopesAt(r.G, pa) : null;
	};
	const env = { off: varsOffset(), T, sym: kernelSymLookup(), row, scopes, cur: null, sameFrame: null,
		loading: ty && ty.state === "loading" ? ty.why : "" };
	const eip = api.machine.currentPhys();
	if (eip >= 0 && stepper) {
		const r = stepper.regs();
		const v = [r.eax, r.ecx, r.edx, r.ebx, r.esp, r.ebp, r.esi, r.edi];
		const reg = (n) => (v[n] === undefined ? undefined : v[n] >>> 0);
		const rw = row(eip);
		const cfa = rw && rw.cfa && rw.cfa.reg !== undefined && reg(rw.cfa.reg) !== undefined ? (reg(rw.cfa.reg) + rw.cfa.off) >>> 0 : undefined;
		env.cur = { pa: eip, reg, cfa };
		// The same function (one invocation: EIP's frame) as EIP.
		const fn = (pa) => { const c = scopes(pa); return c && c.length ? c[c.length - 1][0][0][2][0] : -1; };
		const mine = fn(eip);
		env.sameFrame = (pa) => mine >= 0 && fn(pa) === mine;
	}
	return env;
}

// The names of the memory operands of the instruction `text` at physical
// pa as HTML for a listing line (after the instruction), "" if none;
// counts them in stat.named.
function opNamesHtml(pa, text, env, stat) {
	if (!env)
		return "";
	const names = asmOperandNames(text, pa, env);
	if (!names.length)
		return "";
	if (stat)
		stat.named = (stat.named || 0) + names.length;
	return " " + names.map((n) => '<span class="opn" data-op="' + escHtml(n.op) + '" data-name="' + escHtml(n.name) + '" data-kind="' + n.kind + '"' +
		(n.va !== undefined ? ' data-va="' + n.va + '"' : "") + (n.rel !== undefined ? ' data-rel="' + n.rel + '"' : "") +
		' title="' + escHtml(n.op + " = " + n.detail) + '">&lt;' + escHtml(n.name) + "&gt;</span>").join(" ");
}

// An instruction's text as HTML for a listing line (the Page tab's and the
// Debugger's, whether the text is disasm.json's or decoded live): a
// direct branch's `<name+0xoff>` (llvm-objdump's, or decodeLive's) is a
// .brt span that opens the target when clicked and whose tooltip, made
// when hovered (brtTitle), is the target's source and function facts;
// then the names of the memory operands (opNamesHtml).
function insnHtml(pa, text, env, stat) {
	const t = asmBranchTarget(text);
	const m = t >= 0 ? /^(.*\S)\s+<([^<>]+)>$/.exec(text) : null;
	const head = m ? escHtml(m[1]) + ' <span class="brt" data-name="' + escHtml(m[2]) + '" data-va="' + t + '" data-pa="' +
		((t - varsOffset()) >>> 0) + '">&lt;' + escHtml(m[2]) + "&gt;</span>" : escHtml(text);
	return head + opNamesHtml(pa, text, env, stat);
}

// The tooltip of a branch target's name (.brt): its address and what the
// debug info says of the code there (dbgTitle: the inline chain and each
// function's facts), or why there is none.
function brtTitle(b) {
	const pa = Number(b.dataset.pa);
	return b.dataset.name + " at " + hex(Number(b.dataset.va)) + " (click: open it)\n" + dbgTitle(pa, b.dataset.name.replace(/\+0x[0-9a-f]+$/, ""));
}

export { opEnv, opNamesHtml, insnHtml, brtTitle };
