// The Page tab (A1): one page of guest memory as a listing (listing.js),
// its symbol index and the jump paths (jumps.js). A component on
// listing.js's PAGETAB and vars.js's VARINFO (ui.jsx's controller()),
// mounted when the tab is first shown (shell.jsx). The listing's <pre> is
// drawn by listing.js through its ref, as a canvas would be (PAGETAB.el);
// the rest is drawn here. Element ids and data attributes are the ones the
// tests and the modules read.

import { memo, useLayoutEffect, useMemo, useRef } from "react";
import { PAGE, hex } from "./util.js";
import { selectedPage, symbols, api, setShowSrc, setLiveDecode } from "./page.js";
import { useStore, useController, DomNodes, Tip } from "./ui.jsx";
import { PAGETAB, inspect, selectPage, setLive, listingMount, symbolIndexGo } from "./listing.js";
import { symbolsInPage, symRange, gridRam, openLegend } from "./memview.js";
import { setPageSnapView } from "./snapdiff.js";
import { bmAsk, bmDraftHere } from "./bookmarks.js";
import { dbgHas } from "./dbginfo.js";
import { loadIced } from "./livedis.js";
import { srcLineClick } from "./source.js";
import { jumpArcsObserve } from "./jumpview.js";
import { varDeclEl } from "./varseval.js";
import { VARINFO, varWatch, varsListingMove, varsListingLeave, varsListingClick } from "./vars.js";

const BM_TITLE = "Bookmark the outlined instruction (where a jump or link took you), else the line a third down the listing; " +
	"or right-click (long-press) any line, or point at it and press B";
const SRC_TIP = "C lines above the instructions they compiled to, like objdump -S, from the kernel's DWARF line table: for the kernels " +
	"built here a -g build of the same tree whose code is byte-identical to the kernel that boots (checked in CI), for Debian its -dbg " +
	"package, for an imported unstripped vmlinux its own DWARF. A line is printed again when the code goes back to it; (no line " +
	"information) marks code the line table has no line for. Lines of inlined code name the function they were inlined into; hover for " +
	"the whole inlined-at chain. The text of the lines is shipped for every sample kernel: for the minified Linux and hello the tree they " +
	"were built from, for Debian its source package (the upstream tarball with Debian's patches applied, and the two generated headers " +
	"the DWARF names regenerated). Tap a C line for the whole function with the lines that have executed so far. For an imported vmlinux " +
	"pick its source folder or .tar; without it the lines show as file:line.";
const DECODE_TIP = "Disassemble guest memory in the browser (iced-x86) instead of showing the build's disassembly (disasm.json): shows " +
	"what actually runs, including code that differs from the build. Kernels without disasm.json (imported symbols, disks, the Debian " +
	"sample, BIOS and setup code) always use it. Decoding starts at the containing symbol's first byte so instruction boundaries are " +
	"right; code outside any symbol starts at the page start and its first lines are marked as maybe misaligned. Code below the kernel " +
	"entry, or while the CPU is in real mode, decodes as 16-bit.";
const LISTING_TIP = "One listing of the page in address order, like objdump -d mixed with hexdump -C: instructions from the kernel's " +
	"disassembly (llvm-objdump at build time) as address, bytes, instruction and symbol+offset; everything else as hex rows, with " +
	"repeated rows collapsed to *. Symbol starts are header lines. Yellow bytes changed since the last scan; yellow instructions differ " +
	"from the build (their bytes say why: vmlinux not moved into place yet, a Run to breakpoint, zero memory, a change at run time); " +
	"tinted instructions have not executed in this boot: the page records the address of every instruction it steps, so code that ran " +
	"at the same address before the kernel was in place counts as executed, and code v86's JIT ran (the fast-forward to the kernel " +
	"entry, full speed) is not recorded; the boxed line is the next instruction when the CPU is paused.";
const JUMP_TIP = "Jump paths: every control transfer that does not fall through to the next instruction (jmp, the taken side of a jcc, " +
	"call, ret, loop and jecxz, int, iret, far and indirect jumps and calls, sysenter and sysexit, and the exceptions and interrupts " +
	"that enter a handler). Static paths come from the disassembly: a direct jump or call has its target there; ret, iret, indirect " +
	"jumps and calls and interrupts get theirs only at run time. Observed paths are recorded while the page steps the CPU, with how " +
	"often each was taken; code v86's JIT runs (the fast-forward to the kernel entry, full speed, Run to) records none. After each " +
	"instruction: ←n paths into it, →n paths out of it, ·k× the transfers observed along them; tap one for the list, each with its " +
	"file:line and a button to go there. Above each instruction a path reaches, \"← n paths in:\" names the first sources without a " +
	"tap (two on a narrow screen): the source's mnemonic (or the kind: interrupt, exception), its symbol+offset as llvm-objdump writes " +
	"a target, its file:line and how often it was taken, then \"+k more\" for the whole list. ↑ and ↓ mark a source earlier or later " +
	"in the same function outside the listing, ↗ one in another function; those also get a stub at the edge of the arc gutter, and " +
	"sources without a mark are in the listing (their arcs). After a jump or call whose target is outside the listing, ↑, ↓ or ↗ goes " +
	"there; targets known only at run time (returns, indirect jumps and calls, exceptions) are named after the instruction, \"→ retl " +
	"to ...\". Tap a name to go there. ⊘ marks an instruction no path is known to reach: no jump or call goes there, its address is " +
	"nowhere in the kernel (no function pointer, jump table or exception fixup), the instruction before does not fall through into " +
	"it, and it has not run while stepping (alignment padding after a ret or jmp, or a function nothing calls). An instruction reached " +
	"only through a pointer, a jump table or the exception table has no ⊘, and its list of paths in says how it is reached. Without " +
	"disasm.json the static paths come only from the instructions decoded live (this page, or the lines around EIP), so ⊘ and the " +
	"texts then say that jumps and calls from other code are not known. Jumps inside the listing are drawn as arcs left of the " +
	"addresses, like objdump --visualize-jumps: the arrow points at the target, thin grey arcs have not been taken while stepping, " +
	"solid ones have, dashed ones were observed only (an indirect jump). Point at or tap ⊘, a gutter stub or the tint key for what it " +
	"means; a long press on a button (↑ ↓ ↗, a name, ←n, file:line) shows what it does without doing it, and so does pointing at it or " +
	"reaching it with Tab.";
const TINT_TIP = (partial) => "Tinted lines: instructions that have not executed in this boot. The page records the address of every " +
	"instruction it steps; lines without the tint have executed. Code v86's JIT runs (the fast-forward to the kernel entry, full speed) " +
	"is not recorded" + (partial ? ", and the JIT has run since the kernel entry, so tinted code may have run." : ".");

// The snapshot shown instead of live memory, with the way back.
function SnapBanner({ snap }) {
	return (
		<p id="page_snap" className="note snapview" hidden={!snap}>
			<span id="page_snap_text">{snap ? "Showing snapshot " + snap.name + "; yellow bytes differ from " + snap.against + ". " : ""}</span>
			<button id="page_snap_live" type="button" onClick={() => api.show.navJump(() => {
				setPageSnapView(null);
				inspect();
			})}>Show live memory</button>
		</p>
	);
}

// The listing's coverage key: how many of its instructions are tinted.
function CovInfo({ c }) {
	let body = null;
	if (c && c.nIns && c.tint)
		body = <><span className="nxkey tipmark" tabIndex={0} role="button" data-tip={TINT_TIP(c.partial)}>tinted</span>
			{": " + c.nNx + " of " + c.nIns + " instructions have not executed in this boot (recorded while stepping)" +
			(c.partial ? "; v86's JIT has run since the kernel entry and records nothing, so tinted code may have run" : "")}</>;
	else if (c && c.nIns)
		body = "No execution coverage: " + c.why + ".";
	return <span id="cov_info" className="note">{body}</span>;
}

// The symbols of page p, each a button to its line in the listing: drawn
// again for another page or other symbols, not each time the tab is.
const SymbolIndex = memo(function SymbolIndex({ p }) {
	useStore("symbols");
	useStore("image");
	const list = p < 0 ? null : symbolsInPage(p);
	return (
		<div id="symbol_index" className="symindex" data-count={list ? list.length : undefined}>
			{!list ? null : !symbols ? "No symbols.json for this kernel." : !list.length ? "No symbols in this page." : list.map((s, i) =>
				<button key={i} type="button" title={symRange(s) + (s.size ? ", " + s.size + " bytes" : "") + ", " + s.section +
					" (" + s.type + "), virtual " + hex(s.vaddr) + (s.paddr < p * PAGE ? ", starts in an earlier page" : "")}
				onClick={() => symbolIndexGo(s, p)}>{s.name}</button>)}
		</div>
	);
});

// The variable the byte under the pointer (or tapped) belongs to.
const VarInfo = memo(function VarInfo() {
	useController(VARINFO);
	const { pa, pinned, r, addr, path } = VARINFO;
	const decl = useMemo(() => (r && r.v ? [varDeclEl(r.T, r.v)] : null), [r]);
	return (
		<p id="var_info" className="note mono varinfo" role="status" data-state={r ? r.state : ""} data-path={addr === "" ? undefined : path}
			data-addr={addr === "" ? undefined : addr}>
			{pa >= 0 && r && <>{r.text}{decl && <><DomNodes nodes={decl} />{r.after}</>}
				{pinned && r.state === "ok" && <>{" "}<button id="var_watch" type="button" onClick={() => varWatch(r.path)}>
					{"Watch " + r.path}</button></>}</>}
		</p>
	);
});

// The listing: listing.js draws into it (listingMount); its width sets the
// bytes per row, and the arcs follow it (jumpview.js).
const Listing = memo(function Listing() {
	const ref = useRef(null);
	useLayoutEffect(() => {
		const el = ref.current;
		listingMount(el);
		jumpArcsObserve(el);
		let width = 0;
		const ro = new ResizeObserver(() => {
			if (el.clientWidth !== width) {
				width = el.clientWidth;
				requestAnimationFrame(inspect);
			}
		});
		ro.observe(el);
		return () => {
			ro.disconnect();
			listingMount(null);
		};
	}, []);
	return <pre id="listing" ref={ref} className="asm" data-jbox="" onClickCapture={srcLineClick} onMouseMove={varsListingMove}
		onMouseLeave={varsListingLeave} onClick={varsListingClick} />;
});

function PagePanel() {
	useController(PAGETAB);
	const showSrc = useStore("showSrc");
	const liveDecode = useStore("liveDecode");
	const t = PAGETAB;
	return (
		<>
			<h2 id="page_title">{t.title}</h2>
			{" "}
			<SnapBanner snap={t.snap} />
			{" "}
			<div className="scan">
				<button id="page_prev" type="button" title="Previous page"
					onClick={() => selectedPage > 0 && selectPage(selectedPage - 1)}>{"‹"}</button>
				{" "}
				<button id="page_next" type="button" title="Next page"
					onClick={() => selectedPage >= 0 && selectedPage + 1 < gridRam() / PAGE && selectPage(selectedPage + 1)}>{"›"}</button>
				{" "}
				<button id="reread" type="button" onClick={inspect}>Re-read</button>
				{" "}
				<button id="bm_add_page" type="button" className="bmadd" title={BM_TITLE} onClick={() => bmAsk(bmDraftHere("page"))}>
					{"☆ Bookmark"}
				</button>
				<Tip text="Read the page again; bytes that changed since the last memory scan are marked" />
				{" "}
				<label id="src_toggle" hidden={!dbgHas()}>
					<input type="checkbox" id="show_src" checked={showSrc} onChange={(ev) => {
						setShowSrc(ev.target.checked);
						inspect();
						api.show.renderContext();
					}} />
					{" C source"}
					<Tip text={SRC_TIP} />
				</label>
				{" "}
				<label>
					<input type="checkbox" id="live_decode" checked={liveDecode} onChange={(ev) => {
						setLiveDecode(ev.target.checked);
						if (ev.target.checked)
							loadIced();
						inspect();
						api.show.renderContext();
					}} />
					{" live decode"}
					<Tip text={DECODE_TIP} />
				</label>
				{" "}
				<label>
					<input type="checkbox" id="live" checked={t.live} onChange={(ev) => {
						t.live = ev.target.checked;
						t.changed();
						setLive();
					}} />
					{" live"}
					<Tip text="Re-read the page every 500 ms while the VM runs" />
				</label>
				{" "}
				<span id="dump_info" className="note">{t.dump}</span>
				{" "}
				<Tip text={LISTING_TIP} />
				{" "}
				<CovInfo c={t.cov} />
				{" "}
				<span id="jump_info" className="note">{t.jump}</span>
				{" "}
				<Tip text={JUMP_TIP} />
				{" "}
				<button type="button" className="legendlink" aria-label="Legend: what the colours and marks mean" onClick={openLegend}>Legend</button>
			</div>
			{" "}
			<SymbolIndex p={t.page} />
			{" "}
			<VarInfo />
			{" "}
			<Listing />
		</>
	);
}

export { PagePanel };
