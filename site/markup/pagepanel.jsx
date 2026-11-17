/** @jsxRuntime classic */
/** @jsx h */
/** @jsxFrag Fragment */
// The Page tab (R3): one page of guest memory as a listing (listing.js),
// its symbol index and the jump paths (jumps.js). The component is the
// tab's markup and renders once; listing.js draws the listing, the title
// and the notes into it through their ids, and reads the page again each
// time the tab is shown (the store's tabShows).

import { h, Fragment } from "./html.js";

function PagePanel() {
	return (
		<>
			<h2 id="page_title">No page selected</h2>
			{" "}
			<p id="page_snap" className="note snapview" hidden>
				<span id="page_snap_text" />
				<button id="page_snap_live" type="button">Show live memory</button>
			</p>
			{" "}
			<div className="scan">
				<button id="page_prev" type="button" title="Previous page">{"‹"}</button>
				{" "}
				<button id="page_next" type="button" title="Next page">{"›"}</button>
				{" "}
				<button id="reread" type="button">Re-read</button>
				{" "}
				<button id="bm_add_page" type="button" className="bmadd" title="Bookmark the outlined instruction (where a jump or link took you), else the line a third down the listing; or right-click (long-press) any line, or point at it and press B">
					{"☆ Bookmark"}
				</button>
				<span className="tip" tabIndex="0" role="button" title="Read the page again; bytes that changed since the last memory scan are marked">?</span>
				{" "}
				<label id="src_toggle" hidden>
					<input type="checkbox" id="show_src" defaultChecked />
					{" C source"}
					<span className="tip" tabIndex="0" role="button" title="C lines above the instructions they compiled to, like objdump -S, from the kernel's DWARF line table: for the kernels built here a -g build of the same tree whose code is byte-identical to the kernel that boots (checked in CI), for Debian its -dbg package, for an imported unstripped vmlinux its own DWARF. A line is printed again when the code goes back to it; (no line information) marks code the line table has no line for. Lines of inlined code name the function they were inlined into; hover for the whole inlined-at chain. The text of the lines is shipped for every sample kernel: for the minified Linux and hello the tree they were built from, for Debian its source package (the upstream tarball with Debian's patches applied, and the two generated headers the DWARF names regenerated). Tap a C line for the whole function with the lines that have executed so far. For an imported vmlinux pick its source folder or .tar; without it the lines show as file:line.">
						?
					</span>
				</label>
				{" "}
				<label>
					<input type="checkbox" id="live_decode" />
					{" live decode"}
					<span className="tip" tabIndex="0" role="button" title="Disassemble guest memory in the browser (iced-x86) instead of showing the build's disassembly (disasm.json): shows what actually runs, including code that differs from the build. Kernels without disasm.json (imported symbols, disks, the Debian sample, BIOS and setup code) always use it. Decoding starts at the containing symbol's first byte so instruction boundaries are right; code outside any symbol starts at the page start and its first lines are marked as maybe misaligned. Code below the kernel entry, or while the CPU is in real mode, decodes as 16-bit.">
						?
					</span>
				</label>
				{" "}
				<label>
					<input type="checkbox" id="live" />
					{" live"}
					<span className="tip" tabIndex="0" role="button" title="Re-read the page every 500 ms while the VM runs">?</span>
				</label>
				{" "}
				<span id="dump_info" className="note" />
				{" "}
				<span className="tip" tabIndex="0" role="button" title="One listing of the page in address order, like objdump -d mixed with hexdump -C: instructions from the kernel's disassembly (llvm-objdump at build time) as address, bytes, instruction and symbol+offset; everything else as hex rows, with repeated rows collapsed to *. Symbol starts are header lines. Yellow bytes changed since the last scan; yellow instructions differ from the build (their bytes say why: vmlinux not moved into place yet, a Run to breakpoint, zero memory, a change at run time); tinted instructions have not executed in this boot: the page records the address of every instruction it steps, so code that ran at the same address before the kernel was in place counts as executed, and code v86's JIT ran (the fast-forward to the kernel entry, full speed) is not recorded; the boxed line is the next instruction when the CPU is paused.">
					?
				</span>
				{" "}
				<span id="cov_info" className="note" />
				{" "}
				<span id="jump_info" className="note" />
				{" "}
				<span className="tip" tabIndex="0" role="button" title={"Jump paths: every control transfer that does not fall through to the next instruction (jmp, the taken side of a jcc, call, ret, loop and jecxz, int, iret, far and indirect jumps and calls, sysenter and sysexit, and the exceptions and interrupts that enter a handler). Static paths come from the disassembly: a direct jump or call has its target there; ret, iret, indirect jumps and calls and interrupts get theirs only at run time. Observed paths are recorded while the page steps the CPU, with how often each was taken; code v86's JIT runs (the fast-forward to the kernel entry, full speed, Run to) records none. After each instruction: ←n paths into it, →n paths out of it, ·k× the transfers observed along them; tap one for the list, each with its file:line and a button to go there. Above each instruction a path reaches, \"← n paths in:\" names the first sources without a tap (two on a narrow screen): the source's mnemonic (or the kind: interrupt, exception), its symbol+offset as llvm-objdump writes a target, its file:line and how often it was taken, then \"+k more\" for the whole list. ↑ and ↓ mark a source earlier or later in the same function outside the listing, ↗ one in another function; those also get a stub at the edge of the arc gutter, and sources without a mark are in the listing (their arcs). After a jump or call whose target is outside the listing, ↑, ↓ or ↗ goes there; targets known only at run time (returns, indirect jumps and calls, exceptions) are named after the instruction, \"→ retl to ...\". Tap a name to go there. ⊘ marks an instruction no path is known to reach: no jump or call goes there, its address is nowhere in the kernel (no function pointer, jump table or exception fixup), the instruction before does not fall through into it, and it has not run while stepping (alignment padding after a ret or jmp, or a function nothing calls). An instruction reached only through a pointer, a jump table or the exception table has no ⊘, and its list of paths in says how it is reached. Without disasm.json the static paths come only from the instructions decoded live (this page, or the lines around EIP), so ⊘ and the texts then say that jumps and calls from other code are not known. Jumps inside the listing are drawn as arcs left of the addresses, like objdump --visualize-jumps: the arrow points at the target, thin grey arcs have not been taken while stepping, solid ones have, dashed ones were observed only (an indirect jump). Point at or tap ⊘, a gutter stub or the tint key for what it means; a long press on a button (↑ ↓ ↗, a name, ←n, file:line) shows what it does without doing it, and so does pointing at it or reaching it with Tab."}>
					?
				</span>
				{" "}
				<button type="button" className="legendlink" aria-label="Legend: what the colours and marks mean">Legend</button>
			</div>
			{" "}
			<div id="symbol_index" className="symindex" />
			{" "}
			<p id="var_info" className="note mono varinfo" role="status" />
			{" "}
			<pre id="listing" className="asm" data-jbox="">{"Click a page in the Memory tab."}</pre>
		</>
	);
}

export { PagePanel };
