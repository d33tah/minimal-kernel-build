// The Machine tab (R3): the VM's screen (v86 draws into #screen_container:
// its text rows and canvas), the run controls, the counters and the end
// banner (debugui.js, run.js), and the kernel and boot controls (boot.js,
// kernel.js, header.js, symimport.js). The component is the tab's markup
// and renders once; the modules and v86 draw into it by id, per frame
// while the machine runs (never through React).

function MachinePanel() {
	return (
		<>
			<div className="mstatus">
				<span id="status" role="status">Loading kernel list...</span>
			</div>
			{" "}
			<div id="problems" className="problems" />
			{" "}
			<div id="pwa_notes" className="problems" role="status" />
			{" "}
			<div className="machine">
				<div className="screenwrap">
					<div id="screen_container" title="The VGA text screen (click it and type: the keys go to the VM)">
						<div id="screen_text" />
						{" "}
						<canvas id="screen_canvas" style={{ display: "none" }} />
					</div>
					{" "}
					<div id="vm_overlay" hidden>
						<b>VM paused</b>
						<span id="vm_reason" />
						<button id="vm_continue" type="button">{"▶ Continue"}</button>
					</div>
				</div>
				{" "}
				<div className="run">
					<div className="runbar">
						<button id="dbg_pause" type="button" disabled title="Stop the CPU">Pause</button>
						{" "}
						<button id="dbg_continue" type="button" disabled title="Run again at the selected speed">Continue</button>
						{" "}
						<button id="dbg_step" type="button" disabled title="Execute one instruction">Step</button>
						{" "}
						<button id="dbg_back" type="button" disabled title="Undo the last instruction">Step back</button>
						{" "}
						<label>
							<span>
								speed
								<span className="tip" tabIndex="0" role="button" title="Instructions per second from the kernel entry on. By default BIOS and setup are stepped as fast as possible up to the kernel's 32-bit entry at 0x100000, then the kernel runs at 1k/s so you can watch it. Full speed is v86's JIT: on the virtual clock the page runs it one slice (about 100,000 instructions) at a time, counted, and a run repeats exactly; with the real-time clock v86 runs it on its own, not counted. 10M/s is more than JavaScript stepping reaches on most machines (about 5M/s on a fast desktop); the speed actually reached is shown next to the clock. The choice is remembered in this browser; ?speed=full (or 1000000, 100000, ..., 1) in the URL overrides it.">
									?
								</span>
							</span>
							{"\n          "}
							<select id="dbg_speed">
								<option value="full">full (JIT)</option>
								<option value="10000000">10M/s</option>
								<option value="1000000">1M/s</option>
								<option value="100000">100k/s</option>
								<option value="10000">10k/s</option>
								<option value="1000">1k/s</option>
								<option value="100">100/s</option>
								<option value="10">10/s</option>
								<option value="1">1/s</option>
							</select>
							{"\n        "}
						</label>
						{" "}
						<label>
							<span>
								clock
								<span className="tip" tabIndex="0" role="button" title="Guest time while the page steps the CPU. Virtual (default 10 instructions per microsecond, like a 10 MHz CPU; qemu -icount does the same): time advances with the instruction count, so the timer interrupt (IRQ0, 250 Hz in the minified Linux: one every 40,000 instructions at 10/us) arrives at the same instruction whatever the stepping speed, and the counts are the same every run. The minified Linux's count from its entry does not depend on this rate (132,006 instructions to Still alive); the from-reset count does, through SeaBIOS's waits. When the CPU halts the clock jumps to the next timer event. Real: v86's wall clock; at 1k/s the guest then sees a timer interrupt every few instructions. At full JIT speed the virtual clock follows v86's instruction counter, with compiled code going live only between two slices, so full-speed runs repeat exactly; real time there is v86's own loop, faster or slower with the machine and different every run. ?mips=10, 100, 1000 or real in the URL; the choice is remembered.">
									?
								</span>
							</span>
							{"\n          "}
							<select id="clock_mips" defaultValue="10">
								<option value="10">{"10/µs"}</option>
								<option value="100">{"100/µs"}</option>
								<option value="1000">{"1000/µs"}</option>
								<option value="real">real time</option>
							</select>
							{"\n        "}
						</label>
						{" "}
						<label>
							<input type="checkbox" id="count" defaultChecked />
							{" count"}
							<span className="tip" tabIndex="0" role="button" title="At full speed: first step the whole boot from the reset vector in the background until both lines are on the screen, counting, then run the JIT. At the other speeds the boot is always counted live. Remembered in this browser (Settings tab); ?count=0 in the URL turns it off for one visit.">
								?
							</span>
						</label>
						{" "}
						<span id="dbg_state" className="note" />
					</div>
					{" "}
					<div className="table-wrap">
						<table id="counters" className="mono">
							<thead>
								<tr>
									<th>
										until
										<span className="tip" tabIndex="0" role="button" title="Instructions executed until each line first appears in VGA text memory at 0xb8000, until the first exit or exit_group system call (int 0x80, EAX 1 or 252; the code is EBX), and until the end: the CPU enters panic() (symbols.json) or halts with interrupts off. Counted from the kernel entry at 1 MiB and from the reset vector. Stepping and slow motion stop at the end.">
											?
										</span>
									</th>
									<th>
										from 1 MiB
										<span className="tip" tabIndex="0" role="button" title="Instructions executed from the first one at physical 0x100000, where v86 puts the 32-bit kernel.">
											?
										</span>
									</th>
									<th>
										unique EIPs k/o
										<span className="tip" tabIndex="0" role="button" title="Distinct physical addresses executed from reset: in the kernel range (1 MiB to 1 MiB + init_size) / elsewhere (BIOS, option ROM, real-mode setup).">
											?
										</span>
									</th>
									<th>
										from reset
										<span className="tip" tabIndex="0" role="button" title="Instructions executed from the reset vector (SeaBIOS, the kernel's real-mode setup, the kernel).">
											?
										</span>
									</th>
								</tr>
							</thead>
							<tbody />
						</table>
					</div>
					{" "}
					<p className="note mono" id="vclock" />
					{" "}
					<p className="note mono" id="guest_in">
						<span id="guest_in_text" />
						<span id="guest_keys" />
						{" "}
						<span className="tip" tabIndex="0" role="button" title="What the guest reads from outside the CPU. On the virtual clock the RTC (CMOS clock: the date the kernel and SeaBIOS read) starts at a fixed date when the machine is powered on and runs with the virtual time, and RDRAND and the network card's MAC address come from a random number generator started from a seed, so every run is the same. ?rtc=2026-01-01T00:00:00Z (ISO 8601, the default) and ?seed=1 (the default) in the URL change them. A snapshot keeps the start date and the generator's state, and Restore puts them back. Keys typed on the virtual clock go into the guest only between two slices of the JIT or of stepping and are recorded with the instruction count; Replay (Snapshots tab) puts them in at the same counts. With the real-time clock the guest gets the browser's date and random numbers, and keys when they are typed.">
							?
						</span>
					</p>
					{" "}
					<p className="note mono" id="entry_info" />
					{" "}
					<p id="end_banner" className="banner" role="status" hidden />
					{" "}
					<p className="note" id="count_info">
						<span id="count_text" />
						{" "}
						<span className="tip" tabIndex="0" role="button" title="Exact for the run shown: each instruction is one call of v86's interpreter. A rep-prefixed string instruction counts once per v86 entry, and v86 runs at most a page of iterations per entry, so a long rep movs counts several times (qemu counts it once). On the virtual clock (the default) every count is the same in every run, from reset and from 1 MiB, at any stepping speed and at full JIT speed: the timer follows the instruction count, the RTC starts at a fixed date, RDRAND comes from a seeded generator, and Debian's panic and prompt stops are exact. The clock rate changes the from-reset count, since SeaBIOS's waits take more instructions at a faster clock. A run at full JIT speed gets its timer interrupts at the ends of its slices, so its counts differ from a stepped run's; each of the two repeats exactly. With the real-time clock the counts change from run to run: SeaBIOS waits on wall time, a kernel that calibrates a delay loop against the timer counts differently at each speed, and typed keys go in when they arrive. See the About tab for the qemu cross-check.">
							?
						</span>
					</p>
				</div>
			</div>
			{" "}
			<section className="controls" aria-label="Kernel and boot">
				<label>
					{"Kernel\n      "}
					<select id="kernel" />
					{"\n    "}
				</label>
				{" "}
				<label>
					{"RAM\n      "}
					<select id="memory">
						<option value="2097152">2 MB</option>
						<option value="4194304">4 MB</option>
						<option value="18874368">18 MB</option>
						<option value="33554432">32 MB</option>
						<option value="67108864">64 MB</option>
						<option value="100663296">96 MB</option>
						<option value="134217728">128 MB</option>
						<option value="268435456">256 MB</option>
						<option value="536870912">512 MB</option>
					</select>
					{"\n    "}
				</label>
				{" "}
				<label>
					<span>
						From disk
						<span className="tip" tabIndex="0" role="button" title="Load any bzImage from your disk instead of the list">?</span>
					</span>
					{"\n      "}
					<input type="file" id="file" />
					{"\n    "}
				</label>
				{" "}
				<label>
					<span>
						Symbols
						<span className="tip" tabIndex="0" role="button" title="Import symbols for the current kernel: a System.map (the text llvm-nm -n or nm -n prints: hex address, type letter, name) or an unstripped vmlinux (ELF32: .symtab and section headers are read in the browser). They replace the kernel's symbols.json everywhere (memory grid, Page symbol index, Debugger, Calls, panic detection) and the kernel reboots. A System.map has no sizes: each symbol runs to the next one, capped at the end of its section (_etext, _edata, __bss_stop, ...). Physical address = virtual - offset: auto takes PT_LOAD from an ELF, and for a System.map puts _text at 0x100000 where v86 loads the 32-bit kernel (else vaddr - 0xC0000000, the i386 PAGE_OFFSET, or identity); or type a hex offset. An unstripped vmlinux with DWARF gives file:line; for the text of the lines pick its source folder or .tar under C source. A vmlinux (offset auto) then gets the data files CI makes for the kernels in the list, made in this browser by the same generators with llvm-nm and llvm-objdump of the page's clang.wasm (fetched once, 77 MB): symbols.json, disasm.json (code up to 8 MiB), the DWARF shards with the coverage tree's file list, variables, locals, function facts and call frame information, and the C source index once its tree is picked. Until they are made, code is disassembled live and each compile unit's DWARF is read when first needed. The map and the files made are remembered in this browser for this bzImage (by SHA-256). You can also drop a bzImage and a System.map on the page together.">
							?
						</span>
					</span>
					{"\n      "}
					<input type="file" id="symfile" />
					{"\n    "}
				</label>
				{" "}
				<span id="src_pick" className="srcpick" hidden>
					<span>
						C source
						<span className="tip" tabIndex="0" role="button" title="The text of the C lines for an imported vmlinux (or a kernel without shipped source): pick the source tree it was built from, as a folder or an uncompressed .tar (ustar or GNU; a .tar.gz or .tar.xz would have to be decompressed whole in the browser, so unpack it first). The DWARF paths are relative to the tree; a folder or .tar holding it one level down (linux-6.1.176/...) works too. Only the files of the lines shown are read, when they are shown, until the files made for an imported vmlinux are there: then the files its DWARF names are read once and kept in this browser for this bzImage (gzipped, as the kernels in the list ship them). Nothing is uploaded.">
							?
						</span>
					</span>
					{"\n      "}
					<span className="row">
						<input type="file" id="src_tar" accept=".tar,application/x-tar" aria-label="kernel source .tar" />
						{"\n      "}
						<button id="src_dir_btn" type="button">folder…</button>
					</span>
					{"\n      "}
					<input type="file" id="src_dir" webkitdirectory="" hidden />
					{"\n    "}
				</span>
				{" "}
				<label>
					<span>
						initrd
						<span className="tip" tabIndex="0" role="button" title="An initrd/initramfs for the kernel, passed to v86 (initrd: {buffer}). v86 0.5.471 always puts it at 64 MiB (src/kernel.js), so RAM must be larger than 64 MiB plus its size: picking one selects enough RAM. The kernel finds it through ramdisk_image/ramdisk_size in the boot header. The minified Linux does not use an external initrd yet.">
							?
						</span>
					</span>
					{"\n      "}
					<input type="file" id="initrd_file" />
					{"\n    "}
				</label>
				{" "}
				<label>
					<span>
						cmdline
						<span className="tip" tabIndex="0" role="button" title="Kernel command line, passed to v86 (cmdline). Default: nokaslr cryptomgr.notests (imported System.maps only line up without KASLR; the crypto self-tests take long when stepped), or a kernel's own default; kernels that do not know an option ignore it, and the minified Linux ignores the command line. Add e.g. console=tty0 for a distro kernel. Remembered in this browser for this bzImage (by SHA-256); ?cmdline= in the URL sets it.">
							?
						</span>
					</span>
					{"\n      "}
					<input id="cmdline" size="18" defaultValue="nokaslr cryptomgr.notests" aria-label="kernel command line" />
					{"\n    "}
				</label>
				{" "}
				<label>
					<span>
						offset
						<span className="tip" tabIndex="0" role="button" title="Physical address = virtual address - offset, for imported symbols. auto: from the ELF program headers (PT_LOAD) of a vmlinux; for a System.map _text is put at 0x100000, where v86 loads the 32-bit kernel. Or type the offset in hex (e.g. 0xc0000000, the i386 PAGE_OFFSET). Read when a symbol file is imported.">
							?
						</span>
					</span>
					{"\n      "}
					<input id="sym_offset" defaultValue="auto" size="10" aria-label="symbol offset, auto or hex" />
					{"\n    "}
				</label>
				{" "}
				<button id="boot" type="button">Boot</button>
				{" "}
				<span id="kinfo" className="note mono" />
				{" "}
				<span id="build_info" className="note mono" />
				{" "}
				<span id="sym_status" className="note" role="status" />
				{" "}
				<span id="gen_status" className="note" role="status" />
				{" "}
				<span id="src_status" className="note" role="status" />
				{" "}
				<span id="initrd_status" className="note" />
				{" "}
				<button id="initrd_clear" type="button" hidden>no initrd</button>
				{" "}
				<button id="sym_forget" type="button" hidden title="Forget the imported symbols and use the kernel's own">forget symbols</button>
			</section>
		</>
	);
}

export { MachinePanel };
