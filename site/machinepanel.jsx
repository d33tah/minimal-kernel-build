// The Machine tab (A1): the run controls, the counters and the clock, the
// end banner, the status line and the kernel and boot controls, as React
// components on machtab.js's controllers. The tab is shown at load, so
// shell.jsx mounts it at once, into the `.run` box (#machine_run), with
// portals into the status box above the screen (#machine_top), the screen's
// box for the "VM paused" overlay (#screen_wrap) and the kernel and boot
// section (#machine_ctl). The screen itself (#screen_container, its text
// rows and canvas) is index.html's: v86 needs it before init and draws into
// it, never React. Each part is a memo component on its own field of a
// controller (useField), so a drawn frame of a running machine renders only
// the parts whose text changed. Element ids and data attributes are the
// ones the tests and other modules read.

import { memo, useLayoutEffect, useRef } from "react";
import { createPortal } from "react-dom";
import { useField, Tip, SrcLink, DomNodes } from "./ui.jsx";
import { MSET, SPEEDS, CLOCKS, DEFAULT_CMDLINE, MHEAD, MIMP, MV } from "./machtab.js";
import { machPause, machContinue, doStep, chooseSpeed, chooseClock, chooseCount } from "./runto.js";
import { revStepBack } from "./reverse.js";
import { chooseKernel, chooseRam, bootFromDisk } from "./kernel.js";
import { symfilePicked, forgetImported, initrdPicked, initrdClear, cmdlineInput, saveCmdline, cmdlineKey } from "./symimport.js";
import { srcPickTar, srcPickFolder } from "./srctext.js";
import { pwaReload } from "./pwa.js";
import { boot } from "./boot.js";

const SPEED_TIP = "Instructions per second from the kernel entry on. By default BIOS and setup are stepped as fast as possible up to the kernel's 32-bit entry at 0x100000, then the kernel runs at 1k/s so you can watch it. Full speed is v86's JIT: on the virtual clock the page runs it one slice (about 100,000 instructions) at a time, counted, and a run repeats exactly; with the real-time clock v86 runs it on its own, not counted. 10M/s is more than JavaScript stepping reaches on most machines (about 5M/s on a fast desktop); the speed actually reached is shown next to the clock. The choice is remembered in this browser; ?speed=full (or 1000000, 100000, ..., 1) in the URL overrides it.";
const CLOCK_TIP = "Guest time while the page steps the CPU. Virtual (default 10 instructions per microsecond, like a 10 MHz CPU; qemu -icount does the same): time advances with the instruction count, so the timer interrupt (IRQ0, 250 Hz in the minified Linux: one every 40,000 instructions at 10/us) arrives at the same instruction whatever the stepping speed, and the counts are the same every run. The minified Linux's count from its entry does not depend on this rate (132,006 instructions to Still alive); the from-reset count does, through SeaBIOS's waits. When the CPU halts the clock jumps to the next timer event. Real: v86's wall clock; at 1k/s the guest then sees a timer interrupt every few instructions. At full JIT speed the virtual clock follows v86's instruction counter, with compiled code going live only between two slices, so full-speed runs repeat exactly; real time there is v86's own loop, faster or slower with the machine and different every run. ?mips=10, 100, 1000 or real in the URL; the choice is remembered.";
const COUNT_TIP = "At full speed: first step the whole boot from the reset vector in the background until both lines are on the screen, counting, then run the JIT. At the other speeds the boot is always counted live. Remembered in this browser (Settings tab); ?count=0 in the URL turns it off for one visit.";
const UNTIL_TIP = "Instructions executed until each line first appears in VGA text memory at 0xb8000, until the first exit or exit_group system call (int 0x80, EAX 1 or 252; the code is EBX), and until the end: the CPU enters panic() (symbols.json) or halts with interrupts off. Counted from the kernel entry at 1 MiB and from the reset vector. Stepping and slow motion stop at the end.";
const ENTRY_TIP = "Instructions executed from the first one at physical 0x100000, where v86 puts the 32-bit kernel.";
const UNIQUE_TIP = "Distinct physical addresses executed from reset: in the kernel range (1 MiB to 1 MiB + init_size) / elsewhere (BIOS, option ROM, real-mode setup).";
const RESET_TIP = "Instructions executed from the reset vector (SeaBIOS, the kernel's real-mode setup, the kernel).";
const GUEST_TIP = "What the guest reads from outside the CPU. On the virtual clock the RTC (CMOS clock: the date the kernel and SeaBIOS read) starts at a fixed date when the machine is powered on and runs with the virtual time, and RDRAND and the network card's MAC address come from a random number generator started from a seed, so every run is the same. ?rtc=2026-01-01T00:00:00Z (ISO 8601, the default) and ?seed=1 (the default) in the URL change them. A snapshot keeps the start date and the generator's state, and Restore puts them back. Keys typed on the virtual clock go into the guest only between two slices of the JIT or of stepping and are recorded with the instruction count; Replay (Snapshots tab) puts them in at the same counts. With the real-time clock the guest gets the browser's date and random numbers, and keys when they are typed.";
const COUNTED_TIP = "Exact for the run shown: each instruction is one call of v86's interpreter. A rep-prefixed string instruction counts once per v86 entry, and v86 runs at most a page of iterations per entry, so a long rep movs counts several times (qemu counts it once). On the virtual clock (the default) every count is the same in every run, from reset and from 1 MiB, at any stepping speed and at full JIT speed: the timer follows the instruction count, the RTC starts at a fixed date, RDRAND comes from a seeded generator, and Debian's panic and prompt stops are exact. The clock rate changes the from-reset count, since SeaBIOS's waits take more instructions at a faster clock. A run at full JIT speed gets its timer interrupts at the ends of its slices, so its counts differ from a stepped run's; each of the two repeats exactly. With the real-time clock the counts change from run to run: SeaBIOS waits on wall time, a kernel that calibrates a delay loop against the timer counts differently at each speed, and typed keys go in when they arrive. See the About tab for the qemu cross-check.";
const DISK_TIP = "Load any bzImage from your disk instead of the list";
const SYMBOLS_TIP = "Import symbols for the current kernel: a System.map (the text llvm-nm -n or nm -n prints: hex address, type letter, name) or an unstripped vmlinux (ELF32: .symtab and section headers are read in the browser). They replace the kernel's symbols.json everywhere (memory grid, Page symbol index, Debugger, Calls, panic detection) and the kernel reboots. A System.map has no sizes: each symbol runs to the next one, capped at the end of its section (_etext, _edata, __bss_stop, ...). Physical address = virtual - offset: auto takes PT_LOAD from an ELF, and for a System.map puts _text at 0x100000 where v86 loads the 32-bit kernel (else vaddr - 0xC0000000, the i386 PAGE_OFFSET, or identity); or type a hex offset. An unstripped vmlinux with DWARF gives file:line; for the text of the lines pick its source folder or .tar under C source. A vmlinux (offset auto) then gets the data files CI makes for the kernels in the list, made in this browser by the same generators with llvm-nm and llvm-objdump of the page's clang.wasm (fetched once, 77 MB): symbols.json, disasm.json (code up to 8 MiB), the DWARF shards with the coverage tree's file list, variables, locals, function facts and call frame information, and the C source index once its tree is picked. Until they are made, code is disassembled live and each compile unit's DWARF is read when first needed. The map and the files made are remembered in this browser for this bzImage (by SHA-256). You can also drop a bzImage and a System.map on the page together.";
const SOURCE_TIP = "The text of the C lines for an imported vmlinux (or a kernel without shipped source): pick the source tree it was built from, as a folder or an uncompressed .tar (ustar or GNU; a .tar.gz or .tar.xz would have to be decompressed whole in the browser, so unpack it first). The DWARF paths are relative to the tree; a folder or .tar holding it one level down (linux-6.1.176/...) works too. Only the files of the lines shown are read, when they are shown, until the files made for an imported vmlinux are there: then the files its DWARF names are read once and kept in this browser for this bzImage (gzipped, as the kernels in the list ship them). Nothing is uploaded.";
const INITRD_TIP = "An initrd/initramfs for the kernel, passed to v86 (initrd: {buffer}). v86 0.5.471 always puts it at 64 MiB (src/kernel.js), so RAM must be larger than 64 MiB plus its size: picking one selects enough RAM. The kernel finds it through ramdisk_image/ramdisk_size in the boot header. The minified Linux does not use an external initrd yet.";
const CMDLINE_TIP = "Kernel command line, passed to v86 (cmdline). Default: nokaslr cryptomgr.notests (imported System.maps only line up without KASLR; the crypto self-tests take long when stepped), or a kernel's own default; kernels that do not know an option ignore it, and the minified Linux ignores the command line. Add e.g. console=tty0 for a distro kernel. Remembered in this browser for this bzImage (by SHA-256); ?cmdline= in the URL sets it.";
const OFFSET_TIP = "Physical address = virtual address - offset, for imported symbols. auto: from the ELF program headers (PT_LOAD) of a vmlinux; for a System.map _text is put at 0x100000, where v86 loads the 32-bit kernel. Or type the offset in hex (e.g. 0xc0000000, the i386 PAGE_OFFSET). Read when a symbol file is imported.";

// ---- above the screen -------------------------------------------------------

const StatusLine = memo(function StatusLine() {
	return <span id="status" role="status">{useField(MHEAD, "status")}</span>;
});

const Problems = memo(function Problems() {
	const problems = useField(MHEAD, "problems");
	return (
		<div id="problems" className="problems">
			{problems.map(([level, text], i) => <div key={i} className={level}>{text}</div>)}
		</div>
	);
});

const PwaNotes = memo(function PwaNotes() {
	const newer = useField(MHEAD, "pwaNewer"), misses = useField(MHEAD, "pwaMisses");
	return (
		<div id="pwa_notes" className="problems" role="status">
			{newer && (
				<div id="pwa_update" className="warn">
					{"A new version of this page is deployed (" + newer + "). "}
					<button type="button" onClick={pwaReload}>Reload</button>
				</div>
			)}
			{misses.length > 0 && (
				<div className="warn">
					{"Offline: " + misses.join(", ") + (misses.length > 1 ? " are" : " is") +
						" not available offline yet (a file is kept for offline use once the page has fetched it online)."}
				</div>
			)}
		</div>
	);
});

function Status() {
	return (
		<>
			<div className="mstatus">
				<StatusLine />
			</div>
			{" "}
			<Problems />
			{" "}
			<PwaNotes />
		</>
	);
}

// "VM paused" over the screen, with why (the debugger's last note, its
// file:line links copied, or the end of the run) and Continue unless the
// run has ended.
const Overlay = memo(function Overlay() {
	const o = useField(MV, "overlay");
	const cont = useField(MV, "btns").cont;
	return (
		<div id="vm_overlay" hidden={!o.paused}>
			<b>VM paused</b>
			<span id="vm_reason">{o.nodes ? <DomNodes nodes={o.nodes} /> : o.reason}</span>
			<button id="vm_continue" type="button" hidden={!!o.ended} onClick={() => { if (!cont) machContinue(); }}>{"▶ Continue"}</button>
		</div>
	);
});

// ---- the run box ------------------------------------------------------------

const RunButtons = memo(function RunButtons() {
	const b = useField(MV, "btns");
	const rv = useField(MV, "back");
	return (
		<>
			<button id="dbg_pause" type="button" disabled={b.pause} title="Stop the CPU" onClick={machPause}>Pause</button>
			{" "}
			<button id="dbg_continue" type="button" disabled={b.cont} title="Run again at the selected speed" onClick={machContinue}>Continue</button>
			{" "}
			<button id="dbg_step" type="button" disabled={b.step} title="Execute one instruction" onClick={() => doStep(1)}>Step</button>
			{" "}
			<button id="dbg_back" type="button" disabled={!rv.back}
				title={"Undo the last instruction: restore the checkpoint before it and step forward to it" + rv.tip}
				onClick={() => revStepBack(1)}>Step back</button>
		</>
	);
});

const SpeedSel = memo(function SpeedSel() {
	return (
		<select id="dbg_speed" value={useField(MSET, "speed")} onChange={(e) => chooseSpeed(e.target.value)}>
			{SPEEDS.map(([v, label]) => <option key={v} value={v}>{label}</option>)}
		</select>
	);
});

const ClockSel = memo(function ClockSel() {
	return (
		<select id="clock_mips" value={useField(MSET, "clock")} onChange={(e) => chooseClock(e.target.value)}>
			{CLOCKS.map(([v, label]) => <option key={v} value={v}>{label}</option>)}
		</select>
	);
});

const CountBox = memo(function CountBox() {
	return <input type="checkbox" id="count" checked={useField(MSET, "count")} onChange={(e) => chooseCount(e.target.checked)} />;
});

function RunSettings() {
	return (
		<>
			<label>
				<span>speed<Tip text={SPEED_TIP} /></span>
				<SpeedSel />
			</label>
			{" "}
			<label>
				<span>clock<Tip text={CLOCK_TIP} /></span>
				<ClockSel />
			</label>
			{" "}
			<label>
				<CountBox />
				{" count"}
				<Tip text={COUNT_TIP} />
			</label>
		</>
	);
}

// What the CPU does: the text is cut with an ellipsis, the file:line link
// after it stays whole (style.css #dbg_state).
const RunState = memo(function RunState() {
	const s = useField(MV, "state");
	return (
		<span id="dbg_state" className="note" data-state={s.kind} title={s.title}>
			<span className="sttext">{s.text}</span>
			{s.loc && (
				<span className="stloc">{" ("}<SrcLink file={s.loc.file} line={s.loc.line} pa={s.loc.phys} />{")"}</span>
			)}
		</span>
	);
});

const Counters = memo(function Counters() {
	const c = useField(MV, "counters");
	return (
		<tbody>
			{c.rows.map((r) => (
				<tr key={r.key} data-milestone={r.key} data-steps={r.steps} data-entry={r.entry} data-unique={r.unique} data-kernel={r.kernel}>
					{r.cells.map((v, i) => <td key={i} title={i === 0 ? r.title : undefined}>{v}</td>)}
				</tr>
			))}
		</tbody>
	);
});

const Clock = memo(function Clock() {
	const c = useField(MV, "clock");
	return (
		<p className="note mono" id="vclock" title={c.title} data-speed={c.speed} data-irq0={c.irq0} data-ms={c.ms} data-virtual={c.virtual}>
			{c.text}
		</p>
	);
});

const GuestIn = memo(function GuestIn() {
	const text = useField(MV, "guestIn"), keys = useField(MV, "guestKeys");
	return (
		<p className="note mono" id="guest_in">
			<span id="guest_in_text">{text}</span>
			<span id="guest_keys">{keys}</span>
			{" "}
			<Tip text={GUEST_TIP} />
		</p>
	);
});

const Entry = memo(function Entry() {
	const e = useField(MV, "entry");
	return <p className="note mono" id="entry_info" data-ff-ms={e.ffMs} data-ff-target={e.ffTarget} data-steps={e.steps}>{e.text}</p>;
});

const EndBanner = memo(function EndBanner() {
	const e = useField(MV, "end");
	return <p id="end_banner" className="banner" role="status" hidden={!e.kind} data-kind={e.kind} data-code={e.code}>{e.text}</p>;
});

const CountInfo = memo(function CountInfo() {
	const c = useField(MV, "counters");
	return (
		<p className="note" id="count_info" data-ms={c.ms} data-eta={c.eta}>
			<span id="count_text">{c.text}</span>
			{" "}
			<Tip text={COUNTED_TIP} />
		</p>
	);
});

const COUNTER_HEAD = [["until", UNTIL_TIP], ["from 1 MiB", ENTRY_TIP], ["unique EIPs k/o", UNIQUE_TIP], ["from reset", RESET_TIP]];

function RunBox() {
	return (
		<>
			<div className="runbar">
				<RunButtons />
				{" "}
				<RunSettings />
				{" "}
				<RunState />
			</div>
			{" "}
			<div className="table-wrap">
				<table id="counters" className="mono">
					<thead>
						<tr>{COUNTER_HEAD.map(([t, tip]) => <th key={t}>{t}<Tip text={tip} /></th>)}</tr>
					</thead>
					<Counters />
				</table>
			</div>
			{" "}
			<Clock />
			{" "}
			<GuestIn />
			{" "}
			<Entry />
			{" "}
			<EndBanner />
			{" "}
			<CountInfo />
		</>
	);
}

// ---- the kernel and boot controls ---------------------------------------------

// The deployed build (version.json), or "local build".
const BuildInfo = memo(function BuildInfo() {
	const b = useField(MIMP, "build");
	const deployed = !!b && !b.local;
	return (
		<span id="build_info" className="note mono" data-sha={deployed ? b.sha : undefined}>
			{b && b.local && "local build"}
			{deployed && <><a href={b.url}>{"build " + b.sha.slice(0, 12)}</a>{" · " + b.when + " · "}<a href={b.runUrl}>{"run #" + b.run}</a></>}
		</span>
	);
});

const KernelSel = memo(function KernelSel() {
	const kernels = useField(MSET, "kernels");
	return (
		<select id="kernel" value={useField(MSET, "kernel")} onChange={(e) => chooseKernel(e.target.value)}>
			{kernels.map(([name, label]) => <option key={name} value={name}>{label}</option>)}
		</select>
	);
});

const RamSel = memo(function RamSel() {
	const rams = useField(MSET, "rams");
	return (
		<select id="memory" value={useField(MSET, "ram")} onChange={(e) => chooseRam(e.target.value)}>
			{rams.map(([v, label]) => <option key={v} value={v}>{label}</option>)}
		</select>
	);
});

function Settings() {
	return (
		<>
			<label>
				Kernel
				<KernelSel />
			</label>
			{" "}
			<label>
				RAM
				<RamSel />
			</label>
		</>
	);
}

// A file input: its file goes to pick, then the input is emptied (the same
// file picked again is a change again).
function onFile(pick, clear = true) {
	return (e) => {
		const f = e.target.files[0];
		if (f)
			pick(f);
		if (clear)
			e.target.value = "";
	};
}

// The import statuses and buttons, each on its own field of MIMP.
const SymStatus = memo(function SymStatus() {
	const sym = useField(MIMP, "sym");
	return <span id="sym_status" className="note" role="status" data-state={sym.state} data-dwarf={sym.dwarf}>{sym.text}</span>;
});

const GenStatus = memo(function GenStatus() {
	const gen = useField(MIMP, "gen");
	return <span id="gen_status" className="note" role="status" data-state={gen.state}>{gen.text}</span>;
});

const SrcStatus = memo(function SrcStatus() {
	const src = useField(MIMP, "src");
	return <span id="src_status" className="note" role="status" data-state={src.state}>{src.text}</span>;
});

const InitrdStatus = memo(function InitrdStatus() {
	const initrd = useField(MIMP, "initrd");
	return (
		<>
			<span id="initrd_status" className="note" data-mismatch={initrd.mismatch}>{initrd.text}</span>
			{" "}
			<button id="initrd_clear" type="button" hidden={!initrd.clear} onClick={initrdClear}>no initrd</button>
		</>
	);
});

const Forget = memo(function Forget() {
	return (
		<button id="sym_forget" type="button" hidden={!useField(MIMP, "forget")} title="Forget the imported symbols and use the kernel's own"
			onClick={forgetImported}>forget symbols</button>
	);
});

// The C source picker, shown when there is DWARF but no shipped source.
const SrcPick = memo(function SrcPick() {
	const dir = useRef(null);
	return (
		<span id="src_pick" className="srcpick" hidden={!useField(MIMP, "src").show}>
			<span>C source<Tip text={SOURCE_TIP} /></span>
			<span className="row">
				<input type="file" id="src_tar" accept=".tar,application/x-tar" aria-label="kernel source .tar"
					onChange={(e) => { if (e.target.files[0]) srcPickTar(e.target.files[0]); }} />
				{" "}
				<button id="src_dir_btn" type="button" onClick={() => dir.current.click()}>folder…</button>
			</span>
			<input type="file" id="src_dir" ref={dir} webkitdirectory="" hidden
				onChange={(e) => { if (e.target.files.length) srcPickFolder(e.target.files); }} />
		</span>
	);
});

function Imports() {
	return (
		<>
			<label>
				<span>From disk<Tip text={DISK_TIP} /></span>
				<input type="file" id="file" onChange={onFile(bootFromDisk, false)} />
			</label>
			{" "}
			<label>
				<span>Symbols<Tip text={SYMBOLS_TIP} /></span>
				<input type="file" id="symfile" onChange={onFile(symfilePicked)} />
			</label>
			{" "}
			<SrcPick />
			{" "}
			<label>
				<span>initrd<Tip text={INITRD_TIP} /></span>
				<input type="file" id="initrd_file" onChange={onFile(initrdPicked)} />
			</label>
			{" "}
			<Cmdline />
			{" "}
			<label>
				<span>offset<Tip text={OFFSET_TIP} /></span>
				<input id="sym_offset" ref={(el) => { MSET.offsetEl = el; }} defaultValue="auto" size="10" aria-label="symbol offset, auto or hex" />
			</label>
			{" "}
			<button id="boot" type="button" onClick={boot}>Boot</button>
			{" "}
			<Kinfo />
			{" "}
			<BuildInfo />
			{" "}
			<SymStatus />
			{" "}
			<GenStatus />
			{" "}
			<SrcStatus />
			{" "}
			<InitrdStatus />
			{" "}
			<Forget />
		</>
	);
}

// The command line: an uncontrolled input (read by cmdline()); its native
// change (the field left after an edit) remembers it, which React's
// onChange (every key) is not.
function Cmdline() {
	const ref = useRef(null);
	useLayoutEffect(() => {
		const el = ref.current;
		MSET.cmdlineEl = el;
		el.addEventListener("change", saveCmdline);
		return () => el.removeEventListener("change", saveCmdline);
	}, []);
	return (
		<label>
			<span>cmdline<Tip text={CMDLINE_TIP} /></span>
			<input id="cmdline" ref={ref} size="18" defaultValue={DEFAULT_CMDLINE} aria-label="kernel command line"
				onInput={cmdlineInput} onKeyDown={cmdlineKey} />
		</label>
	);
}

const Kinfo = memo(function Kinfo() {
	return <span id="kinfo" className="note mono">{useField(MHEAD, "kinfo")}</span>;
});

// The tab: the run box here, the rest through portals into index.html's
// boxes around the screen.
function MachinePanel({ top, wrap, ctl }) {
	return (
		<>
			<RunBox />
			{createPortal(<Status />, top)}
			{createPortal(<Overlay />, wrap)}
			{createPortal(<><Settings />{" "}<Imports /></>, ctl)}
		</>
	);
}

export { MachinePanel };
