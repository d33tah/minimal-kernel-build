/** @jsxRuntime classic */
/** @jsx h */
/** @jsxFrag Fragment */
// The About tab (R3): what the page is, its tabs, how counting works and
// the legend of every colour and mark (legend.jsx, which check-legend.py
// reads). Static: it renders once; debugui.js writes the deployed commit
// into #build_subject.

import { h, Fragment } from "./html.js";
import { AboutLegend } from "./legend.jsx";

function AboutPanel() {
	return (
		<>
			<p>
				{"This page boots a Linux-style bzImage in "}
				<a href="https://github.com/copy/v86">v86</a>
				{", an x86 emulator compiled to WebAssembly, and lets you look inside the machine while it runs. Four kernels come with it: "}
				<b>hello</b>
				{", a toy kernel that prints two lines and halts; "}
				<b>minified-linux</b>
				{", the reduced Linux kernel of "}
				<a href="https://github.com/d33tah/minimal-kernel-build">minimal-kernel-build</a>
				{"; and Debian 6.1 686 (bookworm's linux-image-6.1.0-50-686), once without an initrd (it ends in a root-fs panic) and once with its initramfs (it stops at the BusyBox shell). All four come with their debug info (DWARF): file:line, C source, variables, backtraces, parameters and locals, task names and a coverage tree. Any bzImage can be loaded from disk."}
			</p>
			{" "}
			<h3>Tabs</h3>
			{" "}
			<ul>
				<li>
					<b>Machine</b>
					{": the VM's screen, Pause, Continue, Step, speed, clock, count, the state line and the counters, then what runs and how it boots: the kernel, RAM, a bzImage from disk, symbols, C source, initrd, command line, Boot, the loaded bzImage's header and build. It opens first. On every tab the menu toggle at the bottom right shows whether the VM runs and how fast; its menu has Back and Forward, the run state with the instructions stepped (a click opens the Machine tab) and Pause or Continue. The VM keeps running and its screen keeps changing while another tab is shown; keys go to the VM only while the Machine tab is shown."}
				</li>
				<li>
					<b>Memory</b>
					: the physical memory map from the boot header and one square per 4 KiB page, coloured by region and ELF section, with the pages of EIP and ESP marked.
				</li>
				<li>
					<b>Page</b>
					: one page as a single listing, instructions where the kernel has code and hex rows elsewhere, with symbol headers; after each instruction its jump paths in and out (static and observed while stepping, with counts), and the jumps inside the page as arcs.
				</li>
				<li>
					<b>Debugger</b>
					{": registers, stepping and run-to-address; at a pause the C line and inlined-at chain of EIP, the running task (comm, pid), the call stack unwound with the DWARF call frame information, each frame's parameters and locals, and the kernel's global variables, pretty-printed from guest RAM."}
				</li>
				<li>
					<b>C source</b>
					: the Page listing and the Debugger interleave the C lines (from the DWARF of a -g build with byte-identical code) with the instructions; tap one for the function with its executed lines. The text is shipped for every sample kernel (Debian: its source package); for an imported vmlinux pick its source folder or .tar.
				</li>
				<li>
					<b>Symbols</b>
					{": import a System.map or an unstripped vmlinux for the current kernel (Machine tab, or drop it on the page with a bzImage from any tab); it replaces the kernel's symbols everywhere."}
				</li>
				<li>
					<b>initrd, cmdline</b>
					{" (Machine tab): pass an initrd and a command line to v86 for third-party 32-bit kernels (v86 puts the initrd at 64 MiB, so RAM is raised to fit). Our kernels ignore a bootloader initrd."}
				</li>
				<li>
					<b>Calls</b>
					: the symbols the CPU went through while stepping and the transitions between them, live, with task switches named by task.
				</li>
				<li>
					<b>Source</b>
					{": all the kernel's code as a tree of directories, files and functions with the lines and instructions that ran while stepping (coverage); a file opens in the Source panel with its executed lines, lcov export."}
				</li>
				<li>
					<b>Snapshots</b>
					: save, name, restore, download and diff the whole machine state; kept in this browser across reloads.
				</li>
				<li>
					<b>Settings</b>
					: what the page remembers in this browser (compact tabs, speed, clock, count, and per kernel imported symbols, command lines, watch expressions and measured boot lengths), each with an explanation and Forget, and Reset all settings.
				</li>
			</ul>
			{" "}
			<h3>Counting and stepping</h3>
			{" "}
			<p>
				{"v86 normally runs guest code through a JIT, at least 100,003 instructions per call, and has no single-step API. The page adds two exports to v86's own "}
				<code>v86.wasm</code>
				{" at load time (the functions are already in it and named in its name section, the code is unchanged): "}
				<code>read_imm8</code>
				{", which fetches the byte at EIP, and the interpreter's "}
				<code>run</code>
				{", which executes one instruction. Stepping calls them from JavaScript the way v86's interpreter loop does. By default every boot steps BIOS and setup as fast as possible to the kernel entry at 0x100000, then the kernel runs at 1k instructions per second, counted live, until \"Hello, World!\" and \"Still alive\" are both in VGA text memory at 0xb8000 (hello: 154 instructions, a fraction of a second; the minified Linux: about 132,000 instructions, 2 minutes 12 s, with an ETA). With speed set to full (remembered, or "}
				<code>?speed=full</code>
				{") and "}
				<i>count</i>
				{" on, the whole boot is stepped in the background first and then runs on v86's JIT."}
			</p>
			{" "}
			<p>
				{"The counts are exact for the run shown. A "}
				<code>rep</code>
				{" string instruction counts once per v86 entry, and v86 runs at most a page of iterations per entry. On the virtual clock (on by default) every count is the same in every run, from reset and from the kernel entry, at any stepping speed and at full JIT speed: the timer follows the instruction count, the RTC starts at a fixed date, RDRAND and the network card's MAC address come from a seeded generator, and Debian's panic and prompt stops land on an exact instruction. Keys typed there are recorded with their counts, and Replay puts them in again at the same counts. The clock rate changes the from-reset count, since SeaBIOS's waits take more instructions at a faster clock. A run at full JIT speed gets its timer interrupts at the ends of its slices, so its counts differ from a stepped run's; each of the two repeats exactly. With the real-time clock the counts change from run to run: SeaBIOS waits on wall time, a kernel that calibrates delay loops against the timer counts differently at each speed, and typed keys go in when they arrive. hello's kernel count matches qemu instruction for instruction. site/README.md has the cross-check against qemu."}
			</p>
			{" "}
			<AboutLegend />
			{" "}
			<p id="build_subject" className="note" />
			{" "}
			<p>
				{"Source: "}
				<a href="https://github.com/d33tah/minimal-kernel-build/tree/hello-kernel/site">site/</a>
				{" on the hello-kernel branch. Third-party files: site/THIRD_PARTY.md."}
			</p>
		</>
	);
}

export { AboutPanel };
