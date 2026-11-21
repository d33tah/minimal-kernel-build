// The Debugger tab (A1): stepping, Run to and reverse controls, the
// registers and CPU state, Code at EIP and its jump paths, the call
// frames' parameters and locals, watched variables. A component on the
// store's `machine` (the registers, the CPU state: drawn when the machine
// is published, at most once per drawn frame) and on the controllers of
// the modules that set what it shows (dbgtab.js DBGSTATUS, DBGCODE;
// reverse.js REVV; vars.js WATCHV), mounted when the tab is first shown
// (shell.jsx): until then nothing of it is drawn. Code at EIP's <pre> and
// its paths line are written by debugui.js and jumpview.js through their
// refs (HTML lines the Page listing's helpers make, with arcs over them);
// the symbol rows, the frames and the watches are the nodes their helpers
// build (DomNodes). Element ids and data attributes are the ones the tests
// and the modules read.

import { memo, useLayoutEffect, useRef, useState } from "react";
import { stepper, runState, api, setPrevRegs } from "./page.js";
import { useStore, useController, DomNodes, Tip } from "./ui.jsx";
import { DBGSTATUS, DBGIN, DBGCODE, nMount, addrMount, stepCount, codeMount } from "./dbgtab.js";
import { regsView } from "./debugui.js";
import { CPU_SECTIONS, cpuRows, gdtText, idtText, translateText, pageDirText } from "./cpustate.js";
import { REV, REVV, revView, revStepBack, revBackToField } from "./reverse.js";
import { doStep, doRunTo } from "./runto.js";
import { WATCHV, watchAdd, watchSuggest, renderWatches } from "./vars.js";
import { bmAsk, bmDraftHere } from "./bookmarks.js";
import { openLegend } from "./memview.js";
import { srcLineClick } from "./source.js";
import { jumpArcsObserve } from "./jumpview.js";

const RUNTO_TIP = "Step until the next instruction is at this address. A function name (hello_main, start_kernel: every code symbol of that name), hex, physical, or a kernel virtual address (minus the kernel's virtual offset from symbols.json, 0xC0000000 for Linux). An address the CPU cannot stop at (kernel data such as .rodata, or the middle of an instruction) and a name that is no symbol are refused, with similar names, and the CPU stays paused; a target the kernel does not reach before it ends says so. Run to from a target goes on to its next hit. Or a source line, file:line (init/main.c:1000, or main.c:1000 when one file has that name): stops at the first of the line's breakpoint addresses the kernel reaches (the line table's is_stmt rows of that line, in every inlined copy, and the start of each call inlined on it). Clicking a line in the Source panel lists them.";
const REV_TIP = "Reverse execution on the virtual clock, stepped and at JIT speed. While the page steps the CPU it keeps a checkpoint about every half second of stepping, and while the JIT runs one where it starts and about every 3 s of JIT running, estimated from what the JIT did so that they come at the same counts every run (a v86 snapshot stored as the pages changed since the one before, in memory, the last 32; at JIT speed v86's compiled code is reset there too, as a restore resets it, so the JIT runs on from a checkpoint as it ran the first time). Step back restores the last checkpoint before the instruction it goes back to and runs forward to it the way the run went: stepping, or the JIT stopped at that very instruction by its instruction budget. Back to (reverse continue) goes back to the last time before now the CPU was at the Run to field's address (a function, an address or file:line); it looks only in the stretches that were stepped, since the JIT runs compiled code the page cannot watch instruction by instruction. A run on the virtual clock repeats exactly, keys typed included (they go in again at their counts), so the registers, RAM and screen are as they were at that instruction. Stepping forward again runs the same instructions; the paths, calls and coverage recorded the first time are not counted twice, and while stepped back the recorded call stack, the path and the Calls timeline are those of the furthest instruction (the timeline marks now). Where the JIT ran any of the way ahead, Step and Continue go forward on the recorded run the same way, from a checkpoint. The history starts anew where the run cannot be repeated: a snapshot restored, the clock changed or real time, a fast-forward or Run to with breakpoints, a replay running the JIT. Typing, a Run to, or the JIT started after stepping back with only stepped stretches ahead take another way from there: what was ahead is dropped.";
const CODE_TIP = "About 6 instructions before and 10 after the next one (boxed), from the build's disassembly, with symbol+offset. Yellow lines: guest memory differs from the build there (their bytes say why). Tap a line or a symbol to open it in the Page tab. After each instruction its jump paths: ←n into it, →n out of it, ·k× transfers observed while stepping (tap for the list), ⊘ no path known to lead here; above each instruction its sources by name, after it the targets out of view or known only at run time; jumps between the lines shown are arcs left of the addresses; under the listing, the paths into and out of EIP itself (the Page tab's ? has the details). The task row names the running task: comm and pid of the task_struct current_task points at, read from guest RAM with the member offsets from the DWARF (Watch shows the whole task_struct). The previous symbol and the path are recorded while stepping or in slow motion, not at full JIT speed. With DWARF the call stack is unwound from the registers and the stack at the pause: each function's call frame information (.debug_frame), entry code's saved registers (pt_regs) at interrupts, exceptions and system calls, else the EBP chain; where that stops, the frames below are the ones recorded while stepping. Each frame is labelled with the method that found it (EIP, CFI, pt_regs, EBP chain, recorded). The source rows give the C line at EIP from the kernel's DWARF and, in inlined code, each function it was inlined into with the line of the call; every frame of the call stack and of the path has its file:line and inlined-at chain in its tooltip.";
const LOCALS_TIP = "The parameters and local variables of every call frame the call stack unwound, from the kernel's DWARF (shipped kernels: precomputed in CI per 64 KiB of code and fetched for the frames shown; an imported vmlinux: read per compile unit by the Worker that read its variables). Each frame lists, innermost first, the inlined calls at its address and then the function, like gdb's backtrace; a frame's variables are those whose lexical block holds its EIP (a caller's: its call instruction). Values come from that frame's registers and stack through the DWARF location: a register, memory at an offset of the frame base (the CFA from the call frame information), a value computed from registers, a constant, or pieces of these; the location is given after each value. Without a location the variable is optimized out. A caller's EAX, ECX and EDX are not known (a callee may have changed them), and a value only described at the function's entry (DW_OP_entry_value: with -mregparm=3 the first three arguments arrive in EAX, EDX and ECX) is not kept. Frames below where unwinding stopped have no registers. Tap a frame to open it; structs, arrays and pointers work as in Variables.";
const VARS_TIP = "Global and static variables from the kernel's DWARF (for the kernels built here a -g build of the same tree whose code is byte-identical to the kernel that boots, for Debian its -dbg package, for an imported unstripped vmlinux its own DWARF, read once in a Worker), pretty-printed from guest RAM: structs and arrays open on a tap, enums by name, char arrays and char pointers as strings, pointers name what they point at and open the Page tab there. Type a name, Function::name for a static local, then .member, ->member or [index]; a leading * dereferences. A per-CPU variable shows CPU 0's copy once the per-CPU areas exist. Values are read when drawn: while the kernel runs at full speed tap Refresh. In the Page tab, point at or tap a byte of a hex row to see which variable and member it belongs to. The parameters and locals of the call frames are in the section above.";
const WATCH_TIP = "Watch adds a variable: a global or static name (Function::name for a static local), then .member, ->member or [index]; a leading * dereferences. Names complete from the DWARF as you type. The + buttons add common ones. Values are read from guest RAM when drawn; Refresh reads them again (needed while the kernel runs at full speed). A watch list is remembered per kernel in this browser.";

// Step, Step back, Run to and Back to (enabled as the machine and the
// reverse history allow: drawn with REVV, which drawDebugUi changes).
const Controls = memo(function Controls() {
	useController(REVV);
	const can = !!stepper && runState !== "off" && !REV.busy;
	const rv = revView();
	const step = () => {
		if (can)
			doStep(stepCount());
	};
	return (
		<div className="scan">
			<span className="group">
				<button id="dbg_stepn" type="button" disabled={!can} title="Execute this many instructions" onClick={step}>Step</button>
				{"\n      "}
				<input id="dbg_n" ref={nMount} defaultValue="100" size="7" inputMode="numeric"
					aria-label="instructions to step" onKeyDown={(e) => { if (e.key === "Enter") step(); }} />
				<button id="dbg_backn" type="button" disabled={!rv.back} title={"Step back this many instructions" + rv.tip}
					onClick={() => revStepBack(stepCount())}>Step back</button>
			</span>
			{" "}
			<span className="group">
				<button id="dbg_runto" type="button" disabled={!can} onClick={doRunTo}>Run to</button>
				{"\n      "}
				<input id="dbg_addr" ref={addrMount} defaultValue={DBGIN.addr} size="11"
					aria-label="function, address or file:line to run to" onKeyDown={(e) => { if (e.key === "Enter") doRunTo(); }} />
				<button id="dbg_backto" type="button" disabled={!rv.back}
					title={"Reverse continue: back to the last time the CPU was at this address before now" + rv.tip}
					onClick={revBackToField}>Back to</button>
			</span>
			<Tip text={RUNTO_TIP} />
		</div>
	);
});

const RevLine = memo(function RevLine() {
	useController(REVV);
	const rv = revView();
	return (
		<p className="note revline">
			<span id="rev_info" data-cps={rv.cps} data-start={rv.start} data-ahead={rv.ahead}>{rv.text}</span>
			<Tip text={REV_TIP} />
		</p>
	);
});

// The status line: drawn anew for each line written (its key), with the
// reverse step's state (data-rev, data-rev-ms).
const Status = memo(function Status() {
	useController(DBGSTATUS);
	const s = DBGSTATUS;
	return (
		<p key={s.said} id="dbg_status" className="note mono" role="status" data-rev={s.step} data-rev-ms={s.ms}>
			{Array.isArray(s.note) ? <DomNodes nodes={s.note} /> : s.note}
		</p>
	);
});

// The registers, yellow where they changed since last drawn.
const Regs = memo(function Regs() {
	useStore("machine");
	const v = regsView();
	useLayoutEffect(() => {
		if (v)
			setPrevRegs(v.shown);
	});
	return (
		<div id="regs" className="regs mono" data-eip={v ? v.eip : undefined} data-phys={v ? v.phys : undefined}>
			{v && v.cells.map((c) => (
				<div key={c.name} data-reg={c.name} className={[c.chg && "chg", c.wide && "wide"].filter(Boolean).join(" ") || undefined}
					title={c.title}><b>{c.name}</b>{" " + c.value}</div>
			))}
		</div>
	);
});

// A section's buttons: the GDT and the IDT, or translating an address and
// the page directory.
function CpuTools({ kind }) {
	const [out, setOut] = useState("");
	const ref = useRef(null);
	if (kind === "tables")
		return (
			<div className="cpu_tool">
				<button type="button" id="view_gdt" onClick={() => setOut(gdtText(stepper))}>view GDT</button>
				{" "}
				<button type="button" id="view_idt" onClick={() => setOut(idtText(stepper))}>view IDT</button>
				<pre id="cpu_table" className="mono">{out}</pre>
			</div>
		);
	const go = () => setOut(translateText(stepper, ref.current.value));
	return (
		<div className="cpu_tool">
			<label>{"translate "}<input id="xlate_in" ref={ref} className="mono" size="12" placeholder="0xc1000000"
				aria-label="linear address to translate" onKeyDown={(e) => { if (e.key === "Enter") go(); }} /></label>
			{" "}
			<button type="button" id="xlate_go" onClick={go}>translate</button>
			{" "}
			<button type="button" id="pd_view" onClick={() => setOut(pageDirText(stepper))}>page directory</button>
			<pre id="xlate_out" className="mono">{out}</pre>
		</div>
	);
}

// "section:name" -> the value last drawn (a changed one is yellow).
const cpuPrev = new Map();

// The sections of the machine state beyond the registers, read when open.
// Made anew (closed) for each machine with a debugger: without one there
// are none (CpuState).
function CpuSections() {
	const [open, setOpen] = useState(() => new Set());
	const prev = Object.fromEntries(cpuPrev);
	const shown = CPU_SECTIONS.map(([id, , rows]) => (open.has(id) ? cpuRows(stepper, id, rows, prev) : []));
	useLayoutEffect(() => {
		for (const list of shown)
			for (const r of list)
				cpuPrev.set(r.key, r.value);
	});
	return CPU_SECTIONS.map(([id, title, , tools], k) => (
		<details key={id} id={"cpu_" + id} open={open.has(id)} onToggle={(e) => {
			const on = e.currentTarget.open;
			if (on !== open.has(id))
				setOpen((o) => {
					const n = new Set(o);
					n[on ? "add" : "delete"](id);
					return n;
				});
		}}>
			<summary>{title}</summary>
			<div className="regs mono cpu_rows">
				{shown[k].map((r) => <div key={r.key} data-reg={r.name} className={r.chg ? "wide chg" : "wide"} title={r.title}>
					<b>{r.name}</b>{" " + r.value}</div>)}
			</div>
			{tools && <CpuTools kind={tools} />}
		</details>
	));
}

const CpuState = memo(function CpuState() {
	useStore("machine");
	const st = stepper;
	useLayoutEffect(() => {
		if (!st)
			cpuPrev.clear();
	});
	return <div id="cpu_state" className="cpu_state">{st && <CpuSections />}</div>;
});

const SymInfo = memo(function SymInfo() {
	useController(DBGCODE);
	return <div id="sym_info" className="syminfo"><DomNodes nodes={DBGCODE.sym} /></div>;
});

// A line of Code at EIP (or a symbol on it) opens in the Page tab; a C
// line opens the Source panel (srcLineClick, first).
function codeClick(ev) {
	const line = ev.target.closest("[data-addr]");
	if (line)
		api.show.jumpTo(Number(line.dataset.addr));
}

// Code at EIP and the paths at EIP, written through their refs.
const Code = memo(function Code() {
	const ctx = useRef(null), jumps = useRef(null);
	useLayoutEffect(() => {
		codeMount(ctx.current, jumps.current);
		jumpArcsObserve(ctx.current);
	}, []); // the tab stays mounted: nothing to undo
	return (
		<>
			<pre id="context" ref={ctx} className="listing-small asm" data-jbox="" onClickCapture={srcLineClick} onClick={codeClick} />
			{" "}
			<p id="eip_jumps" ref={jumps} className="note mono" data-jbox="" />
		</>
	);
});

const Locals = memo(function Locals() {
	useController(DBGCODE);
	return (
		<>
			<p id="locals_status" className="note" role="status">{DBGCODE.localsStatus}</p>
			{" "}
			<div id="locals" className="watches mono"><DomNodes nodes={DBGCODE.locals} /></div>
		</>
	);
});

// Variables: the input (its completions follow what is typed, also an
// input event a script sends: a listener of its own), the suggestions,
// the status line and the watches.
const Watches = memo(function Watches() {
	useController(WATCHV);
	const w = WATCHV;
	const ref = useRef(null);
	useLayoutEffect(() => {
		const el = ref.current;
		el.addEventListener("input", () => watchSuggest(el.value));
	}, []); // the tab stays mounted: nothing to undo
	const add = () => {
		watchAdd(ref.current.value);
		ref.current.value = "";
	};
	return (
		<>
			<div className="scan">
				<span className="group">
					<input id="watch_expr" ref={ref} list="watch_names" size="24" placeholder="init_task.comm" aria-label="variable to watch"
						autoComplete="off" spellCheck="false" onKeyDown={(e) => { if (e.key === "Enter") add(); }} />
					{"\n      "}
					<button id="watch_add" type="button" onClick={add}>Watch</button>
				</span>
				{" "}
				<button id="watch_refresh" type="button" onClick={renderWatches}>Refresh</button>
				<Tip text={WATCH_TIP} />
				{" "}
				<span id="watch_quick" className="group">
					{w.quick.map((n) => <button key={n} type="button" title={"Watch " + n} onClick={() => watchAdd(n)}>{"+ " + n}</button>)}
				</span>
			</div>
			{" "}
			<datalist id="watch_names">{w.names.map((n) => <option key={n} value={n} />)}</datalist>
			{" "}
			<p id="watch_status" className="note" role="status">{w.status}</p>
			{" "}
			<div id="watches" className="watches mono" data-state={w.state || undefined} data-started={w.started}>
				{w.empty || <DomNodes nodes={w.rows} />}
			</div>
		</>
	);
});

function DebuggerPanel() {
	return (
		<>
			<Controls />
			{" "}
			<RevLine />
			{" "}
			<Status />
			{" "}
			<Regs />
			{" "}
			<CpuState />
			{" "}
			<SymInfo />
			{" "}
			<h3>
				{"Code at EIP "}
				<Tip text={CODE_TIP} />
				{" "}
				<button id="bm_add_eip" type="button" className="bmadd" aria-label="Bookmark the instruction at EIP"
					title="Bookmark the instruction at EIP; or right-click (long-press) any line here, or point at it and press B"
					onClick={() => bmAsk(bmDraftHere("eip"))}>
					{"☆"}
				</button>
				{" "}
				<button type="button" className="legendlink" aria-label="Legend: what the colours and marks mean" onClick={openLegend}>Legend</button>
			</h3>
			{" "}
			<Code />
			{" "}
			<h3>
				{"Parameters and locals "}
				<Tip text={LOCALS_TIP} />
			</h3>
			{" "}
			<Locals />
			{" "}
			<h3>
				{"Variables "}
				<Tip text={VARS_TIP} />
			</h3>
			{" "}
			<Watches />
		</>
	);
}

export { DebuggerPanel };
