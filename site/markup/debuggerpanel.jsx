/** @jsxRuntime classic */
/** @jsx h */
/** @jsxFrag Fragment */
// The Debugger tab (R3): stepping, Run to and reverse controls, the
// registers and CPU state (debugui.js, cpustate.js), Code at EIP and its
// jump paths, parameters and locals, watched variables (vars.js). The
// component is the tab's markup and renders once; the modules draw into
// it by id, per frame while the machine runs (never through React), and
// debugui.js draws Code at EIP again each time the tab is shown (the
// store's tabShows).

import { h, Fragment } from "./html.js";

function DebuggerPanel() {
	return (
		<>
			<div className="scan">
				<span className="group">
					<button id="dbg_stepn" type="button" disabled title="Execute this many instructions">Step</button>
					{"\n      "}
					<input id="dbg_n" defaultValue="100" size="7" inputMode="numeric" aria-label="instructions to step" />
					<button id="dbg_backn" type="button" disabled>Step back</button>
				</span>
				{" "}
				<span className="group">
					<button id="dbg_runto" type="button" disabled>Run to</button>
					{"\n      "}
					<input id="dbg_addr" defaultValue="0x100000" size="11" aria-label="function, address or file:line to run to" />
					<button id="dbg_backto" type="button" disabled>Back to</button>
				</span>
				<span className="tip" tabIndex="0" role="button" title="Step until the next instruction is at this address. A function name (hello_main, start_kernel: every code symbol of that name), hex, physical, or a kernel virtual address (minus the kernel's virtual offset from symbols.json, 0xC0000000 for Linux). An address the CPU cannot stop at (kernel data such as .rodata, or the middle of an instruction) and a name that is no symbol are refused, with similar names, and the CPU stays paused; a target the kernel does not reach before it ends says so. Run to from a target goes on to its next hit. Or a source line, file:line (init/main.c:1000, or main.c:1000 when one file has that name): stops at the first of the line's breakpoint addresses the kernel reaches (the line table's is_stmt rows of that line, in every inlined copy, and the start of each call inlined on it). Clicking a line in the Source panel lists them.">
					?
				</span>
			</div>
			{" "}
			<p className="note revline">
				<span id="rev_info" />
				<span className="tip" tabIndex="0" role="button" title="Reverse execution on the virtual clock, stepped and at JIT speed. While the page steps the CPU it keeps a checkpoint about every half second of stepping, and while the JIT runs one where it starts and about every 3 s of JIT running, estimated from what the JIT did so that they come at the same counts every run (a v86 snapshot stored as the pages changed since the one before, in memory, the last 32; at JIT speed v86's compiled code is reset there too, as a restore resets it, so the JIT runs on from a checkpoint as it ran the first time). Step back restores the last checkpoint before the instruction it goes back to and runs forward to it the way the run went: stepping, or the JIT stopped at that very instruction by its instruction budget. Back to (reverse continue) goes back to the last time before now the CPU was at the Run to field's address (a function, an address or file:line); it looks only in the stretches that were stepped, since the JIT runs compiled code the page cannot watch instruction by instruction. A run on the virtual clock repeats exactly, keys typed included (they go in again at their counts), so the registers, RAM and screen are as they were at that instruction. Stepping forward again runs the same instructions; the paths, calls and coverage recorded the first time are not counted twice, and while stepped back the recorded call stack, the path and the Calls timeline are those of the furthest instruction (the timeline marks now). Where the JIT ran any of the way ahead, Step and Continue go forward on the recorded run the same way, from a checkpoint. The history starts anew where the run cannot be repeated: a snapshot restored, the clock changed or real time, a fast-forward or Run to with breakpoints, a replay running the JIT. Typing, a Run to, or the JIT started after stepping back with only stepped stretches ahead take another way from there: what was ahead is dropped.">
					?
				</span>
			</p>
			{" "}
			<p id="dbg_status" className="note mono" role="status" />
			{" "}
			<div id="regs" className="regs mono" />
			{" "}
			<div id="cpu_state" className="cpu_state" />
			{" "}
			<div id="sym_info" className="syminfo" />
			{" "}
			<h3>
				{"Code at EIP "}
				<span className="tip" tabIndex="0" role="button" title="About 6 instructions before and 10 after the next one (boxed), from the build's disassembly, with symbol+offset. Yellow lines: guest memory differs from the build there (their bytes say why). Tap a line or a symbol to open it in the Page tab. After each instruction its jump paths: ←n into it, →n out of it, ·k× transfers observed while stepping (tap for the list), ⊘ no path known to lead here; above each instruction its sources by name, after it the targets out of view or known only at run time; jumps between the lines shown are arcs left of the addresses; under the listing, the paths into and out of EIP itself (the Page tab's ? has the details). The task row names the running task: comm and pid of the task_struct current_task points at, read from guest RAM with the member offsets from the DWARF (Watch shows the whole task_struct). The previous symbol and the path are recorded while stepping or in slow motion, not at full JIT speed. With DWARF the call stack is unwound from the registers and the stack at the pause: each function's call frame information (.debug_frame), entry code's saved registers (pt_regs) at interrupts, exceptions and system calls, else the EBP chain; where that stops, the frames below are the ones recorded while stepping. Each frame is labelled with the method that found it (EIP, CFI, pt_regs, EBP chain, recorded). The source rows give the C line at EIP from the kernel's DWARF and, in inlined code, each function it was inlined into with the line of the call; every frame of the call stack and of the path has its file:line and inlined-at chain in its tooltip.">
					?
				</span>
				{" "}
				<button id="bm_add_eip" type="button" className="bmadd" aria-label="Bookmark the instruction at EIP" title="Bookmark the instruction at EIP; or right-click (long-press) any line here, or point at it and press B">
					{"☆"}
				</button>
				{" "}
				<button type="button" className="legendlink" aria-label="Legend: what the colours and marks mean">Legend</button>
			</h3>
			{" "}
			<pre id="context" className="listing-small asm" data-jbox="" />
			{" "}
			<p id="eip_jumps" className="note mono" data-jbox="" />
			{" "}
			<h3>
				{"Parameters and locals "}
				<span className="tip" tabIndex="0" role="button" title="The parameters and local variables of every call frame the call stack unwound, from the kernel's DWARF (shipped kernels: precomputed in CI per 64 KiB of code and fetched for the frames shown; an imported vmlinux: read per compile unit by the Worker that read its variables). Each frame lists, innermost first, the inlined calls at its address and then the function, like gdb's backtrace; a frame's variables are those whose lexical block holds its EIP (a caller's: its call instruction). Values come from that frame's registers and stack through the DWARF location: a register, memory at an offset of the frame base (the CFA from the call frame information), a value computed from registers, a constant, or pieces of these; the location is given after each value. Without a location the variable is optimized out. A caller's EAX, ECX and EDX are not known (a callee may have changed them), and a value only described at the function's entry (DW_OP_entry_value: with -mregparm=3 the first three arguments arrive in EAX, EDX and ECX) is not kept. Frames below where unwinding stopped have no registers. Tap a frame to open it; structs, arrays and pointers work as in Variables.">
					?
				</span>
			</h3>
			{" "}
			<p id="locals_status" className="note" role="status" />
			{" "}
			<div id="locals" className="watches mono" />
			{" "}
			<h3>
				{"Variables "}
				<span className="tip" tabIndex="0" role="button" title="Global and static variables from the kernel's DWARF (for the kernels built here a -g build of the same tree whose code is byte-identical to the kernel that boots, for Debian its -dbg package, for an imported unstripped vmlinux its own DWARF, read once in a Worker), pretty-printed from guest RAM: structs and arrays open on a tap, enums by name, char arrays and char pointers as strings, pointers name what they point at and open the Page tab there. Type a name, Function::name for a static local, then .member, ->member or [index]; a leading * dereferences. A per-CPU variable shows CPU 0's copy once the per-CPU areas exist. Values are read when drawn: while the kernel runs at full speed tap Refresh. In the Page tab, point at or tap a byte of a hex row to see which variable and member it belongs to. The parameters and locals of the call frames are in the section above.">
					?
				</span>
			</h3>
			{" "}
			<div className="scan">
				<span className="group">
					<input id="watch_expr" list="watch_names" size="24" placeholder="init_task.comm" aria-label="variable to watch" autoComplete="off" spellCheck="false" />
					{"\n      "}
					<button id="watch_add" type="button">Watch</button>
				</span>
				{" "}
				<button id="watch_refresh" type="button">Refresh</button>
				<span className="tip" tabIndex="0" role="button" title="Watch adds a variable: a global or static name (Function::name for a static local), then .member, ->member or [index]; a leading * dereferences. Names complete from the DWARF as you type. The + buttons add common ones. Values are read from guest RAM when drawn; Refresh reads them again (needed while the kernel runs at full speed). A watch list is remembered per kernel in this browser.">
					?
				</span>
				{" "}
				<span id="watch_quick" className="group" />
			</div>
			{" "}
			<datalist id="watch_names" />
			{" "}
			<p id="watch_status" className="note" role="status" />
			{" "}
			<div id="watches" className="watches mono" />
		</>
	);
}

export { DebuggerPanel };
