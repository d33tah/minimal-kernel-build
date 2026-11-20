/** @jsxRuntime classic */
/** @jsx h */
/** @jsxFrag Fragment */
// The About tab's legend (about.jsx shows it): every colour and mark of
// the page with a sample (check-legend.py reads the samples in the
// #about_legend section: each class style.css colours has one). Static:
// memview.js writes the generated lists (#legend_mem, #legend_graph).

import { h, Fragment } from "./html.js";

function AboutLegend() {
	return (
		<section id="about_legend" aria-labelledby="about_legend_h">
			<h3 id="about_legend_h">Legend</h3>
			{" "}
			<p>
				Every colour and mark of the page, each drawn here with the same styles as where it appears (light and dark follow). Most marks also explain themselves: hover, tap or focus them.
			</p>
			{" "}
			<h4>Instruction listings: Code at EIP (Debugger tab) and the Page listing</h4>
			{" "}
			<ul className="lgnd">
				<li>
					<span className="lgs asm" inert>
						<span className="ins bad">
							{"00100000 "}
							<span className="difb tipmark">8d 4d 03</span>
							{"     lea 0x3(%ebp),%ecx"}
						</span>
					</span>
					{" "}
					<span>
						<b>Yellow line</b>
						{": guest memory holds other bytes than the build's disassembly at this instruction. Its bytes say why (hover, tap or focus them): vmlinux is not in place yet (the minified Linux's bzImage loads it 121 bytes higher, behind a relocation stub at 0x100000 that moves it down right after the kernel entry), Run to has put HLT breakpoint bytes there while the JIT runs to it, the memory is zero, or the code changed at run time. Where: Code at EIP, Page listing."}
					</span>
				</li>
				<li>
					<span className="lgs asm" inert>
						<span className="hex">
							{"00121ec0 00 00 "}
							<mark>3c</mark>
							{" 12 10 00 "}
							<mark>01</mark>
							{" 00 |..<.....|"}
						</span>
					</span>
					{" "}
					<span>
						<b>Yellow bytes</b>
						{" in a hex row or an instruction: changed since the last memory scan (the page scans at every stop, every few frames while the CPU runs and on Scan), or between the last two scans right after a Scan. In a snapshot diff: the byte differs between the two sides (the hexdump, and the Page tab opened from the diff). Where: Page listing, Snapshots diff."}
					</span>
				</li>
				<li>
					<span className="lgs asm" inert>
						<span className="ins nx">{"001001a8 89 c3     mov %eax,%ebx"}</span>
						{"\n"}
						<span className="ins">{"001001aa 85 db     test %ebx,%ebx"}</span>
						{"  "}
						<span className="nxkey tipmark">tinted</span>
					</span>
					{" "}
					<span>
						<b>Tinted line</b>
						{": the instruction has not executed in this boot; untinted lines have. Recorded while stepping; code v86's JIT ran (fast-forward, full speed) is not recorded, and the key above the listing says when the tint may be incomplete. Where: Page listing."}
					</span>
				</li>
				<li>
					<span className="lgs asm" inert>
						<span className="ins cur">
							<span className="eipmark tipmark">▶</span>
							{"00126280 55        push %ebp"}
						</span>
					</span>
					{" "}
					<span>
						<b>Boxed line</b>
						: EIP, the next instruction the CPU runs (▶ in Code at EIP). Where: Code at EIP, Page listing, while the CPU is paused or stepping.
					</span>
				</li>
				<li>
					<span className="lgs asm" inert>
						<span className="ins cur mid tipmark">{"00100000 8d 4d 03   lea 0x3(%ebp),%ecx"}</span>
					</span>
					{" "}
					<span>
						<b>Dashed box</b>
						{": EIP is inside this instruction of the build's disassembly, so the CPU runs other bytes here (the relocation stub at 0x100000 right after the entry). Where: Page listing."}
					</span>
				</li>
				<li>
					<span className="lgs asm" inert>
						<span className="ins jhit">{"00104a10 e8 3b 02 00 00  call 0x104c50"}</span>
					</span>
					{" "}
					<span>
						<b>Dashed blue outline</b>
						: the instruction a jump, link or Back took you to. Where: Page listing.
					</span>
				</li>
				<li>
					<span className="lgs asm" inert>
						<span className="lbl">
							{"start_kernel: "}
							<span className="dim">1290 bytes, .init.text</span>
							{" "}
							<span className="srctext">void start_kernel(void)</span>
						</span>
					</span>
					{" "}
					<span>
						<b>Symbol header</b>
						{" (blue, bold): a symbol starts here, with its size and section or its type, and a function's prototype. Where: Page listing."}
					</span>
				</li>
				<li>
					<span className="lgs asm" inert>
						<span className="hex">000b8000 48 07 65 07 6c 07 6c 07 |H.e.l.l.|</span>
					</span>
					{" "}
					<span>
						<b>Hex row</b>
						: bytes outside the disassembly, like hexdump -C; a * stands for repeated rows. Where: Page listing.
					</span>
				</li>
				<li>
					<span className="lgs asm" inert>
						<span className="ins maybe">{"0009f000 00 00     add %al,(%eax)"}</span>
					</span>
					{" "}
					<span>
						<b>Faded italics</b>
						: a line decoded live before the first symbol of the page; it may start in the middle of an instruction. Where: Page listing with live decode.
					</span>
				</li>
			</ul>
			{" "}
			<h4>Jump paths</h4>
			{" "}
			<ul className="lgnd">
				<li>
					<span className="lgs asm" inert>
						<span className="ins">{"00101091 c3        ret"}</span>
						{"\n"}
						<span className="ins">{"00101092 90        nop"}</span>
						{" "}
						<span className="jnone tipmark">⊘</span>
					</span>
					{" "}
					<span>
						<b>⊘</b>
						: no path leads here. Nothing jumps or calls to this instruction, its address is stored nowhere, the one before it does not fall through and it has not run while stepping: usually alignment padding, or a function nothing calls. Where: Code at EIP, Page listing.
					</span>
				</li>
				<li>
					<span className="lgs asm" inert>
						<span className="ins">{"00104a10 e8 3b 02 00 00  call 0x104c50"}</span>
						{" "}
						<span className="jc">←2·5×</span>
						{" "}
						<span className="jc">→1·5×</span>
					</span>
					{" "}
					<span>
						<b>←n·k× and →n·k×</b>
						: n paths into and out of the instruction (static ones from the disassembly and ones seen while stepping), taken k times while stepping. Tap for the list. Where: Code at EIP, Page listing.
					</span>
				</li>
				<li>
					<span className="lgs asm" inert>
						<span className="jsrc">
							<span className="je">
								<button type="button" className="jto">↑ do_one_initcall</button>
							</span>
							{" "}
							<span className="je">
								<button type="button" className="jto">↓ L1</button>
							</span>
							{" "}
							<span className="je">
								<button type="button" className="jto">↗ kernel_init</button>
							</span>
							{" "}
							<span className="jmore">+3 more</span>
						</span>
						<span className="ins">{"00104c50 55        push %ebp"}</span>
						{" "}
						<span className="jdst">
							{"→ "}
							<button type="button" className="jto">↗ memcpy</button>
						</span>
					</span>
					{" "}
					<span>
						<b>Sources and targets</b>
						: the line above an instruction names where paths into it come from, the text after it where it goes when that is out of view or known only at run time: ↑ earlier in the function, ↓ later in it, ↗ another function; names are links. Where: Code at EIP, Page listing.
					</span>
				</li>
				<li>
					<span className="lgs" inert>
						<svg width="150" height="44" viewBox="0 0 150 44" aria-hidden="true">
							{"\n      "}
							<path className="jarc" d="M30 4 H12 V16 H30" />
							<path className="jarrow" d="M30 16 l-5 -3 v6 z" />
							{"\n      "}
							<path className="jarc taken" d="M80 4 H62 V28 H80" />
							<path className="jarrow taken" d="M80 28 l-5 -3 v6 z" />
							{"\n      "}
							<path className="jarc dyn" d="M130 4 H112 V16 H130" />
							<path className="jarrow" d="M130 16 l-5 -3 v6 z" />
							{"\n      "}
							<path className="jstub" d="M30 40 H16 V34" />
							<path className="jstubend" d="M13 34 h6" />
						</svg>
					</span>
					{" "}
					<span>
						<b>Arcs</b>
						{" left of the addresses: a jump between two lines in view, grey for a path from the disassembly, blue for one taken while stepping, dashed for one seen only at run time (indirect). "}
						<b>Dotted stubs</b>
						: a path in from a line out of view (the line above the instruction names it). Where: Code at EIP, Page listing.
					</span>
				</li>
				<li>
					<span className="lgs asm" inert>
						<span className="ins">
							{"00104a10 e8 3b 02 00 00  call "}
							<span className="brt">memcpy</span>
							{" mov "}
							<span className="opn">jiffies</span>
							,%eax
						</span>
					</span>
					{" "}
					<span>
						<b>Blue name</b>
						{" in an instruction: a branch target (tap to go there). "}
						<b>Teal name</b>
						: a memory operand named from the debug info (a variable or member). Where: Code at EIP, Page listing.
					</span>
				</li>
			</ul>
			{" "}
			<h4>C source</h4>
			{" "}
			<ul className="lgnd">
				<li>
					<span className="lgs asm" inert>
						<span className="src">
							<span className="srcloc">init/main.c:960</span>
							{" "}
							<span className="srctext">{"set_task_stack_end_magic(&init_task);"}</span>
						</span>
						{"\n"}
						<span className="src nol">
							<span className="srcloc">include/linux/sched.h:12</span>
							{" "}
							<span className="srctext">return p;</span>
						</span>
					</span>
					{" "}
					<span>
						<b>C line</b>
						{" (italics) above the instructions it compiled to: location in brown, text in purple; tap it for the function in the Source panel. Faded when there is no panel to open. Where: Page listing, Code at EIP (C source on)."}
					</span>
				</li>
				<li>
					<span className="lgs plain" inert>
						<button type="button" className="srcloc srclink">init/main.c:960</button>
						{" "}
						<span className="srctext">{"set_task_stack_end_magic(&init_task);"}</span>
					</span>
					{" "}
					<span>
						<b>file:line</b>
						{" with a dotted underline: opens the Source panel at that line. Brown file:line and purple C text are the same everywhere: listings, Debugger, call stack, Source panel, Calls. Where: everywhere C appears."}
					</span>
				</li>
			</ul>
			{" "}
			<h4>Debugger</h4>
			{" "}
			<ul className="lgnd">
				<li>
					<span className="lgs plain" inert>
						<span className="regs mono">
							<div className="chg">
								<b>EAX</b>
								{" 00000003"}
							</div>
							<div>
								<b>EBX</b>
								{" 00000000"}
							</div>
						</span>
					</span>
					{" "}
					<span>
						<b>Yellow register</b>
						: changed since the registers were last shown. Where: Debugger, registers.
					</span>
				</li>
				<li>
					<span className="lgs plain" inert>
						<span className="watches">
							<span className="wname">nr_cpu_ids</span>
							{" "}
							<span className="wtype">unsigned int</span>
							{" = 1; "}
							<span className="wname">err</span>
							{" = "}
							<span className="lout">{"<optimized out>"}</span>
							{"; "}
							<span className="wname">p</span>
							{" = "}
							<span className="lout">{"<not mapped>"}</span>
							{"; "}
							<span className="werr">cannot read 0xc1000000</span>
						</span>
					</span>
					{" "}
					<span>
						<b>Variables</b>
						: names in blue, types muted; in italics a value that is not there: optimized out (the compiler kept no location for it here) or not mapped (the address is not in RAM or not paged in); red, an error reading it. Where: Parameters and locals, Watch.
					</span>
				</li>
				<li>
					<span className="lgs plain" inert>
						<span className="banner">Kernel panic: Attempted to kill init!</span>
						<span className="banner" data-kind="halt">Halted: hlt with interrupts off</span>
					</span>
					{" "}
					<span>
						<b>End of the run</b>
						: red when the kernel panicked or ran out of its panic budget, amber when the CPU halted for good. Where: under the screen.
					</span>
				</li>
			</ul>
			{" "}
			<h4>Source panel and Source tab</h4>
			{" "}
			<ul className="lgnd">
				<li>
					<span className="lgs srcview" inert>
						<span className="exec">
							{"  958 ● "}
							<span className="srctext">local_irq_disable();</span>
						</span>
						{"\n"}
						<span className="exec cont">
							{"  959 ┆ "}
							<span className="srctext">{"  early_boot_irqs_disabled = true;"}</span>
						</span>
						{"\n"}
						<span className="code">
							{"  961 ○ "}
							<span className="srctext">boot_cpu_init();</span>
						</span>
						{"\n"}
						<span className="exec here">
							{"  960 ● "}
							<span className="srctext">{"set_task_stack_end_magic(&init_task);"}</span>
						</span>
						{"\n"}
						<span className="code pick">
							{"  962 ○ "}
							<span className="srctext">page_address_init();</span>
						</span>
						{"\n"}
						<span className="code was">
							{"  963 ◌ "}
							<span className="srctext">feature_init();</span>
						</span>
					</span>
					{" "}
					<span>
						<b>Green</b>
						{" (●): a line whose code has executed; ○ not yet; ┆ and a lighter green: more lines of the statement above or below (not counted). "}
						<b>Blue box</b>
						{": the line at EIP or the line opened. "}
						<b>Brown bar</b>
						{" on the left: the picked line, whose addresses are listed above the text. "}
						<b>Orange</b>
						{" (◌): a line that ran in the earlier build the Source tab's coverage tree compares with, not (yet) in this one. Recorded while stepping. Where: Source panel."}
					</span>
				</li>
				<li>
					<span className="lgs plain" inert>
						<span className="covbar" style={{ width: "8rem" }}>
							<span style={{ width: "62%" }} />
						</span>
					</span>
					{" "}
					<span>
						<b>Coverage bar</b>
						: the share of lines (or instructions) that ran while stepping. Where: Source tab.
					</span>
				</li>
				<li>
					<span className="lgs plain" inert>
						<pre className="eddiff">
							<span className="edf">{"diff --git a/boot/hello.c b/boot/hello.c"}</span>
							{"\n"}
							<span className="edh">{"@@ -30,2 +30,3 @@"}</span>
							{"\n \tput_line(row, \"Hello, World!\");\n"}
							<span className="eda">{"+\t/* your line */"}</span>
							{"\n"}
							<span className="edd">{"-\tput_line(row + 1, \"Still alive\");"}</span>
						</pre>
					</span>
					{" "}
					<span>
						<b>A diff of your edits</b>
						{": green lines added, red lines removed, blue the place of each change (old and new line numbers), bold the file's header; the other lines are unchanged context. Where: Source panel (Diff while editing), Source tab (Your edits)."}
					</span>
				</li>
			</ul>
			{" "}
			<h4>Memory tab</h4>
			{" "}
			<p className="note">{"One square per 4 KiB page, coloured by region of the boot memory map or by the kernel's ELF section:"}</p>
			{" "}
			<ul className="legend" id="legend_mem" />
			{" "}
			<ul className="lgnd">
				<li>
					<span className="lgs plain memsample" inert>
						<span className="memtag tipmark">EIP</span>
						<span className="memcell">
							<span className="memeip" />
						</span>
					</span>
					{" "}
					<span>
						<b>Blue box</b>
						{" around a page's square, with the EIP label left of its row: the page of the next instruction, at its physical address (through the page tables once paging is on), the one the Debugger shows. The line above the grid says where EIP is and opens it in the Page tab. It follows EIP while stepping and when paused; at full speed (JIT) it is drawn when the CPU stops. Where: Memory tab."}
					</span>
				</li>
				<li>
					<span className="lgs plain memsample" inert>
						<span className="memtag esp tipmark">ESP</span>
						<span className="memcell">
							<span className="memesp" />
						</span>
					</span>
					{" "}
					<span>
						<b>Dashed box</b>
						, with the ESP label: the page ESP points into (the stack). Where: Memory tab.
					</span>
				</li>
				<li>
					<span className="lgs plain" inert>
						<span className="bar memlgbar">
							<div style={{ flex: "1 0 0", background: "var(--r-bios)" }}>first MiB</div>
							<div className="eiphere" style={{ flex: "2 0 0", background: "var(--r-kernel)" }}>kernel</div>
							<span className="eiptick" style={{ left: "55%" }} />
						</span>
					</span>
					{" "}
					<span>
						<b>Blue frame</b>
						{" in a region bar: the region that holds EIP; the "}
						<b>tick</b>
						{" is where EIP is in it. Where: Memory tab."}
					</span>
				</li>
			</ul>
			{" "}
			<h4>Calls tab</h4>
			{" "}
			<p className="note">Symbols are coloured by the top directory of their source file:</p>
			{" "}
			<ul className="legend" id="legend_graph" />
			{" "}
			<h4>The menu toggle (bottom right, on every tab)</h4>
			{" "}
			<ul className="lgnd">
				<li>
					<span className="lgs plain rbsamples" inert>
						<span className="rbtog" data-state="run">
							<span className="rbtop">
								<span className="rbbars" />
								<span className="rbdot" />
								<span className="rbnews" hidden />
							</span>
							<span className="rbspeed">1k/s</span>
						</span>
						<span className="rbtog" data-state="ff">
							<span className="rbtop">
								<span className="rbbars" />
								<span className="rbdot" />
								<span className="rbnews" hidden />
							</span>
							<span className="rbspeed">JIT</span>
						</span>
						<span className="rbtog" data-state="paused">
							<span className="rbtop">
								<span className="rbbars" />
								<span className="rbdot" />
								<span className="rbnews" hidden />
							</span>
							<span className="rbspeed">paused</span>
						</span>
						<span className="rbtog" data-state="halted">
							<span className="rbtop">
								<span className="rbbars" />
								<span className="rbdot" />
								<span className="rbnews" hidden />
							</span>
							<span className="rbspeed">halted</span>
						</span>
						<span className="rbtog" data-state="panic">
							<span className="rbtop">
								<span className="rbbars" />
								<span className="rbdot" />
								<span className="rbnews" hidden />
							</span>
							<span className="rbspeed">panic</span>
						</span>
						<span className="rbtog" data-state="off">
							<span className="rbtop">
								<span className="rbbars" />
								<span className="rbdot" />
								<span className="rbnews" hidden />
							</span>
							<span className="rbspeed">off</span>
						</span>
					</span>
					{" "}
					<span>
						{"Three bars: it opens a menu with Back and Forward through the jump history, the run state in words with the instruction count, and Pause or Continue. The "}
						<b>dot</b>
						{" and the word under it say what the VM does: "}
						<b>green, pulsing</b>
						{" while the CPU runs, with the speed (1/s up to full); "}
						<b>blue, pulsing</b>
						{" while it fast-forwards with the JIT; "}
						<b>hollow</b>
						{" when paused; a "}
						<b>grey square</b>
						{" at the end of the run (halted); a "}
						<b>red square</b>
						{" at a kernel panic; "}
						<b>dotted</b>
						{" before a kernel boots. With reduced motion the dot does not pulse. Where: bottom right of every tab."}
					</span>
				</li>
				<li>
					<span className="lgs plain rbsamples" inert>
						<span className="rbtog" data-state="paused">
							<span className="rbtop">
								<span className="rbbars" />
								<span className="rbdot" />
								<span className="rbnews" />
							</span>
							<span className="rbspeed">paused</span>
						</span>
					</span>
					{" "}
					<span>
						<b>Pink dot</b>
						{" on its corner: a new version of this page is deployed; the menu says which and offers Reload."}
					</span>
				</li>
			</ul>
			{" "}
			<h4>Live memory map (Memory tab; Quests tab beside the cards)</h4>
			{" "}
			<ul className="lgnd">
				<li>
					<span className="lgs plain" inert>
						<span className="mm-sw mm-l0" />
						{" "}
						<span className="mm-sw mm-l1" />
						{" "}
						<span className="mm-sw mm-l2" />
						{" "}
						<span className="mm-sw mm-l3" />
					</span>
					{" "}
					<span>
						{"Who touched the cells, bright when new, fading after: "}
						<b>blue</b>
						{" the CPU outside RAM (memory-mapped I/O, with a pulse from the ring on the cell of the code that did it), "}
						<b>orange</b>
						{" a device in RAM (DMA), "}
						<b>green</b>
						{" the emulator (the loader's drop, a snapshot restore, the page's own writes), "}
						<b>light blue</b>
						{" the CPU writing a RAM page (sampled per frame). Where: Live memory map, its story and its key; the Events tab (a row's left edge and its Who, the Who chips)."}
					</span>
				</li>
				<li>
					<span className="lgs plain" inert>
						<span className="mm-sw mm-k-ram" />
						{" "}
						<span className="mm-sw mm-k-reserved" />
						{" "}
						<span className="mm-sw mm-k-mmio" />
						{" "}
						<span className="mm-sw mm-k-rom" />
						{" "}
						<span className="mm-sw mm-k-none" />
					</span>
					{" "}
					<span>
						{"The regions under them, pale until touched: "}
						<b>grey</b>
						{" RAM, "}
						<b>dark grey</b>
						{" reserved (E820), "}
						<b>purple</b>
						{" memory-mapped I/O (video memory, PCI BARs, APICs, HPET), "}
						<b>yellow</b>
						{" ROM, "}
						<b>hatched</b>
						{" addresses no standard the guest used names. Where: Live memory map and its region list; the Events tab (before each region, the Region chips)."}
					</span>
				</li>
			</ul>
			{" "}
			<h4>Events tab</h4>
			{" "}
			<ul className="lgnd">
				<li>
					<span className="lgs plain" inert>
						<span className="mev-rw" data-rw="write">
							<span className="mev-rwb">{"→ W"}</span>
							write
						</span>
						{" "}
						<span className="mev-rw" data-rw="read">
							<span className="mev-rwb">{"← R"}</span>
							read
						</span>
					</span>
					{" "}
					<span>
						<b>Filled</b>
						{" badge with an arrow into memory and W: a write; "}
						<b>outlined</b>
						{" (dashed) badge with an arrow out of memory and R: a read. Writes are also in bold. The row's left edge and the swatch under Who have the initiator's colour (see Live memory map). Where: Events tab, column Access."}
					</span>
				</li>
			</ul>
			{" "}
			<h4>Quests tab</h4>
			{" "}
			<ul className="lgnd">
				<li>
					<span className="lgs plain" inert>
						<span className="qs-card qs-sample" data-state="solved">
							<span className="qs-mark">{"★"}</span>
							{" "}
							<span className="qs-wontext">solved</span>
						</span>
						{" "}
						<span className="qs-meter" style={{ width: "6rem" }}>
							<span style={{ width: "50%" }} />
						</span>
						{" "}
						<span className="qs-card qs-sample" data-state="open">
							<span className="qs-chip">To do</span>
						</span>
						{" "}
						<span className="qs-card qs-sample" data-state="never">
							<span className="qs-chip">Needs Debian</span>
						</span>
						{" "}
						<code className="qs-key">0xC0000</code>
					</span>
					{" "}
					<span>
						<b>Green</b>
						{" card with a "}
						<b>star</b>
						{": a quest solved on this kernel in this browser; the "}
						<b>bar</b>
						{" fills with the quests solved out of those this kernel can give. The state in words at the top right: "}
						<b>To do</b>
						{" (blue), "}
						<b>Solved</b>
						{" (green), and on a "}
						<b>dashed</b>
						{" card one this kernel cannot give (it says which can). "}
						<b>Shaded</b>
						{" values: the addresses, numbers and names a quest is about. Where: Quests tab."}
					</span>
				</li>
				<li>
					<span className="lgs plain" inert>
						<span className="qs-hint">a hint</span>
						{" "}
						<span className="mm-sw mm-hintsw" />
						{" "}
						<span className="qs-fb bad">Too small</span>
					</span>
					{" "}
					<span>
						<b>Pink</b>
						{": a quest's hint and the cells it makes blink on the live memory map. "}
						<b>Red text</b>
						: an answer that is not right yet, with which way it is off. Where: Quests, Live memory map.
					</span>
				</li>
			</ul>
			{" "}
			<h4>Bookmarks</h4>
			{" "}
			<ul className="lgnd">
				<li>
					<span className="lgs asm" inert>
						<span className="ins">
							{"00126280 55        push %ebp "}
							<span className="bmstar tipmark">{"★"}</span>
						</span>
					</span>
					{" "}
					<span>
						<b>Gold star</b>
						{" (★): a bookmark points here; hover, tap or focus it for its name. ☆ buttons add one (so do a right click or a long press on a line, page, box or frame, and the B key), and the menu toggle's menu lists them. Where: Code at EIP, Page listing, Memory grid (on the page's square), Source panel, Source tab, call stack, Calls (before the box's name)."}
					</span>
				</li>
			</ul>
			{" "}
			<h4>Warnings and links</h4>
			{" "}
			<ul className="lgnd">
				<li>
					<span className="lgs plain problems" inert>
						<div className="warn">Boot protocol 2.15: newer than v86 knows</div>
						<div className="error">{"No \"HdrS\" signature at 0x202: this is not a bzImage."}</div>
					</span>
					{" "}
					<span>
						<b>Amber</b>
						{": a warning about the loaded bzImage; "}
						<b>red</b>
						: an error that stops it from booting. Where: under the kernel picker.
					</span>
				</li>
				<li>
					<span className="lgs plain" inert>
						<a href="#about_legend">a link</a>
						{" "}
						<button type="button" className="symlink">start_kernel</button>
					</span>
					{" "}
					<span>
						<b>Blue text</b>
						{" goes somewhere: a link, a symbol, a jump target."}
					</span>
				</li>
			</ul>
		</section>
	);
}

export { AboutLegend };
