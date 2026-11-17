"use strict";

// Page tab: the listing of one page (disassembly or hex dump, symbols,
// jump arcs, coverage), the yellow lines where guest memory differs from
// the build, and the selected page.

import { realNow } from "./clock.js";
import { PAGE, $, escHtml, esc, asciiOf, hex, V86_KERNEL_ADDR } from "./util.js";
import { emulator, disasm, stepper, symbols, api, image, emuRam, selectedPage, emuReady, setSelectedPage,
	runState, provide, state, tab } from "./page.js";
import { jumpsFor, jumpHit, jumpPaths, jumpBadgesHtml, jumpsJitNote, jumpHitScroll,
	setJumpHitScroll } from "./jumps.js";
import { jumpInlineMax, jumpInlineHtml, jumpStubAttrs, jumpsDrawn, drawJumpArcs } from "./jumpview.js";
import { srcLineHtml } from "./source.js";
import { liveDecode, liveInsnsForPage, icedError, loadIced, iced } from "./livedis.js";
import { ffBreaks } from "./fastforward.js";
import { opEnv, insnHtml } from "./varops.js";
import { varTypeOf } from "./vars.js";
import { fnLabelOf } from "./fnfacts.js";
import { pageSnapView, pageSnapBanner } from "./snapdiff.js";
import { bmCodeStar, bmFnStar } from "./bookmarks.js";
import { scanBase } from "./scan.js";
import { symbolsInPage, symbolAt, symRange, gridRam, pageLabel, drawGrid } from "./memview.js";

const LIVE_MS = 500;
let liveTimer = null;

// ---- page listing ---------------------------------------------------------

function readPage(p) {
	return emulator.read_memory(p * PAGE, PAGE).slice();
}

// Characters of the listing's monospace font that fit in one line.
let charWidth = 0, charFont = "";
function lineChars(el) {
	const cs = getComputedStyle(el);
	const font = cs.fontSize + " " + cs.fontFamily;
	if (!charWidth || charFont !== font) {
		const ctx = document.createElement("canvas").getContext("2d");
		ctx.font = font;
		charFont = font;
		charWidth = ctx.measureText("0123456789abcdef").width / 16 || 7;
	}
	const w = el.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight);
	return Math.max(20, Math.floor(w / charWidth) - 1);
}

// Bytes per hex row: the most of 16, 8, 4 whose row fits.
function hexPer(chars) {
	for (const per of [16, 8, 4])
		if (8 + 1 + per * 3 + (per === 16 ? 1 : 0) + 2 + per + 1 <= chars)
			return per;
	return 4;
}

// Index of the first instruction at or after physical address a.
function insnIndex(a) {
	const ins = disasm.insns;
	let lo = 0, hi = ins.length;
	while (lo < hi) { const m = (lo + hi) >> 1; if (ins[m][0] < a) lo = m + 1; else hi = m; }
	return lo;
}

// Whether the listing of the page at lo can tint the instructions that
// have not executed: only what the stepper stepped is recorded (Stepper.seen,
// by physical address), not what v86's JIT ran.
function coverageFor(lo) {
	if (!stepper || !stepper.unique)
		return { tint: false, why: "nothing has been stepped yet" };
	if (stepper.jitBeforeEntry && (stepper.sinceEntry < 0 || lo < stepper.kernelLo))
		return { tint: false, why: "the code before the kernel entry ran on v86's JIT (fast-forward), which records nothing" };
	return { tint: true, partial: stepper.jitAfterEntry };
}

// One listing of page p in address order: instructions (disasm.json) as
// "addr bytes insn symbol+off", everything else as hexdump -C rows, symbol
// starts as "name:" headers. base: bytes from the last scan (changed bytes
// are marked; how: since when, for their popover); cur: physical address of
// the next instruction, or -1. Instructions that have not executed in this
// boot are tinted (coverageFor); instructions whose bytes differ from the
// build are yellow and say why (diffTip).
function listPage(p, bytes, base, cur, chars, how) {
	const lo = p * PAGE, hi = lo + PAGE;
	const cov = coverageFor(lo);
	const per = hexPer(chars);
	const wide = chars >= 100;
	const syms = symbolsInPage(p).filter((s) => s.paddr >= lo);
	const sized = symbolsInPage(p).filter((s) => s.size > 0);
	let insns = new Map();
	let source = "none", misaligned = new Set(), bits = 32;
	if (disasm && !liveDecode())
		for (let i = insnIndex(lo); i < disasm.insns.length && disasm.insns[i][0] < hi; i++)
			insns.set(disasm.insns[i][0], disasm.insns[i]);
	if (insns.size)
		source = "build";
	else {
		// No disasm.json (or "live decode"): decode guest memory with iced-x86.
		const live = liveInsnsForPage(p);
		if (live) {
			insns = live.insns;
			misaligned = live.misaligned;
			bits = live.bits;
			source = "live";
		} else if (!icedError)
			loadIced();
	}
	// Jump paths: the build's (whole kernel) or, decoded live, this page's.
	const jS = !insns.size ? null : source === "build" ? jumpsFor(disasm.insns) : jumpsFor([...insns.values()].sort((x, y) => x[0] - y[0]));
	const paths = new Map();
	const inView = (x) => insns.has(x), imax = jumpInlineMax($("listing"));
	const out = [];
	let si = 0, nIns = 0, nBad = 0, nNx = 0, nRows = 0, nonzero = 0, changed = 0, curLine = -1;
	let prevRow = null, starred = false;
	const srcState = { key: null };
	const ops = insns.size && bits !== 16 ? opEnv() : null, opStat = {}; // real-mode addresses are segment-relative
	const mark = (off, text) => (base && base[off] !== bytes[off] ? "<mark>" + text + "</mark>" : text);
	const scanTip = "Yellow: the marked bytes changed " + (how === "since the previous scan" ?
		"between the previous memory scan and the last one (Memory tab, Scan)." :
		"since the last memory scan. The page scans memory at every stop, every few frames while the CPU runs, and on Scan.");
	for (let off = 0; off < PAGE; off++)
		if (bytes[off]) nonzero++;
	if (base)
		for (let off = 0; off < PAGE; off++)
			if (base[off] !== bytes[off]) changed++;
	let a = lo;
	while (a < hi) {
		while (si < syms.length && syms[si].paddr <= a) {
			const s = syms[si++];
			if (s.paddr === a || !insns.has(s.paddr)) {
				const ty = varTypeOf(s);
				// A function's label line: its prototype, and its facts in the title.
				const fl = ty ? null : fnLabelOf(s);
				out.push('<span class="lbl" data-sym="' + escHtml(s.name) + '" data-addr="' + s.paddr + '"' + (ty ? ' data-type="' + escHtml(ty) + '"' : "") +
					(fl ? ' data-proto="' + escHtml(fl.proto) + '" title="' + escHtml(fl.title) + '"' : "") + ">" +
					escHtml(s.name) + ":" + (s.size || ty ? ' <span class="dim">' + (ty ? escHtml(ty) + (s.size ? ", " : "") : "") +
						(s.size ? s.size + " bytes, " + s.section : "") + "</span>" : "") + (fl ? ' <span class="srctext">' + escHtml(fl.proto) + "</span>" : "") + bmFnStar(s.paddr) + "</span>");
			}
			prevRow = null;
		}
		const nextSym = si < syms.length ? syms[si].paddr : hi;
		const ins = insns.get(a);
		if (ins) {
			const [pa, raw, text] = ins;
			const n = raw.length / 2;
			let ok = true, rawHtml = "", scanned = false;
			for (let k = 0; k < n && pa + k < hi; k++) {
				const want = parseInt(raw.substr(k * 2, 2), 16);
				if (bytes[pa - lo + k] !== want)
					ok = false;
				if (base && base[pa - lo + k] !== bytes[pa - lo + k])
					scanned = true;
				if (k < 7)
					rawHtml += mark(pa - lo + k, bytes[pa - lo + k].toString(16).padStart(2, "0"));
			}
			const m = Math.min(n, hi - pa);
			const tip = !ok ? diffTip(pa, raw.substr(0, m * 2), bytes.subarray(pa - lo, pa - lo + m)) : scanned ? scanTip : "";
			if (tip)
				rawHtml = diffBytesHtml(rawHtml, tip);
			rawHtml += n > 7 ? "…" : "  ".repeat(7 - n) + " ";
			const s = symbolAt(sized, pa);
			const where = s ? s.name + "+0x" + (pa - s.paddr).toString(16) : "";
			let cls = ok ? "ins" : "ins bad";
			if (misaligned.has(pa))
				cls += " maybe";
			if (cov.tint && !stepper.executed(pa)) {
				cls += " nx";
				nNx++;
			}
			// EIP inside this instruction: the bytes there are not the
			// build's (the relocation stub at 0x100000 runs over vmlinux's
			// startup_32); the line holding EIP gets the mark.
			const inside = cur > pa && cur < pa + n;
			if (pa === cur || inside) {
				cls += inside ? " cur mid tipmark" : " cur";
				curLine = out.length;
			}
			const srcLine = srcLineHtml(pa, srcState);
			if (srcLine)
				out.push(srcLine);
			if (pa === cur || inside)
				curLine = out.length;
			if (pa === jumpHit)
				cls += " jhit";
			const P = jumpPaths(jS, stepper, pa);
			paths.set(pa, P);
			const J = jumpInlineHtml(jS, P, pa, inView, imax);
			out.push(J.src + '<span class="' + cls + '" data-addr="' + pa + '"' + (inside ? ' data-eip="' + cur + '"' : "") +
				(inside || tip ? ' data-tip="' + escHtml((tip ? tip + (inside ? "\n\n" : "") : "") + (inside ? midTitle(cur) : "")) + '"' : "") +
				jumpStubAttrs(J) + ">" + pa.toString(16).padStart(8, "0") +
				" " + rawHtml + " " + insnHtml(pa, text, ops, opStat) +
				(wide && where ? ' <span class="dim">' + escHtml(where) + "</span>" : "") + bmCodeStar(pa) + "</span>" + J.dst + jumpBadgesHtml(P, pa));
			nIns++;
			if (!ok)
				nBad++;
			a += Math.max(1, n);
			prevRow = null;
			continue;
		}
		// Hex row up to the row boundary, the next instruction or symbol.
		let end = Math.min(hi, (Math.floor((a - lo) / per) + 1) * per + lo, nextSym);
		for (let b = a + 1; b < end; b++)
			if (insns.has(b)) { end = b; break; }
		const full = end - a === per && (a - lo) % per === 0;
		const row = bytes.subarray(a - lo, end - lo);
		const dirty = base && row.some((v, i) => base[a - lo + i] !== v);
		const hasCur = cur >= a && cur < end;
		if (full && !dirty && !hasCur && prevRow && row.every((v, i) => v === prevRow[i])) {
			if (!starred)
				out.push("*");
			starred = true;
			a = end;
			continue;
		}
		starred = false;
		prevRow = full ? row : null;
		const pad = (a - lo) % per;
		let hx = "", asc = "";
		for (let i = 0; i < per; i++) {
			const sep = i === 8 && per === 16 ? "  " : i ? " " : "";
			const b = a - pad + i;
			if (b < a || b >= end) {
				hx += sep + "  ";
				asc += " ";
				continue;
			}
			const v = bytes[b - lo];
			hx += sep + mark(b - lo, v.toString(16).padStart(2, "0"));
			asc += mark(b - lo, esc(asciiOf(v)));
		}
		let cls = "hex";
		if (hasCur) {
			cls += " cur";
			curLine = out.length;
		}
		out.push('<span class="' + cls + '" data-addr="' + a + '" data-row="' + (a - pad) + '" data-per="' + per + '" data-end="' + end + '"' +
			(dirty ? ' data-tip="' + escHtml(scanTip) + '"' : "") + ">" +
			(a - pad).toString(16).padStart(8, "0") +
			" " + hx + " |" + asc + "|" + bmCodeStar(a) + "</span>");
		nRows++;
		a = end;
	}
	return { html: out.join("\n"), nIns, nBad, nNx, cov, nRows, nonzero, changed, curLine, source, bits, misaligned: misaligned.size, jS, paths,
		opsNamed: opStat.named || 0, opsWhy: !ops ? "" : ops.loading ? "operand names: " + ops.loading : !ops.T ? "operands named from ELF symbols only (no DWARF variables)" : "" };
}

function renderSymbolIndex(p) {
	const el = $("symbol_index");
	el.textContent = "";
	const syms = symbolsInPage(p);
	el.dataset.count = syms.length;
	if (!symbols) {
		el.textContent = "No symbols.json for this kernel.";
		return;
	}
	if (!syms.length) {
		el.textContent = "No symbols in this page.";
		return;
	}
	for (const s of syms) {
		const b = document.createElement("button");
		b.type = "button";
		b.textContent = s.name;
		b.title = symRange(s) + (s.size ? ", " + s.size + " bytes" : "") + ", " + s.section +
			" (" + s.type + "), virtual " + hex(s.vaddr) + (s.paddr < p * PAGE ? ", starts in an earlier page" : "");
		b.addEventListener("click", () => {
			const target = $("listing").querySelector('[data-addr="' + Math.max(s.paddr, p * PAGE) + '"]');
			if (target)
				api.show.navJump(() => scrollListingTo(target));
		});
		el.append(b);
	}
}

function scrollListingTo(line) {
	const box = $("listing");
	// An instruction's sources line above it comes into view with it.
	const prev = line.previousElementSibling;
	if (prev && prev.matches(".jsrc"))
		line = prev;
	// Measured, not offsetTop: the listing is the lines' offsetParent (it
	// holds the jump arcs, position: relative). A third of the way down
	// the listing, or of the part of it on screen above the Source panel
	// where the panel is a sheet over the screen (a phone).
	const b = box.getBoundingClientRect(), band = sheetBand(b);
	const top = band ? band[0] : b.top, h = band ? band[1] - band[0] : box.clientHeight;
	box.scrollTop += line.getBoundingClientRect().top - top - h / 3;
}

// [top, bottom] of rect r on screen between the sticky top bar and the
// Source panel when the panel is open as a sheet (narrow screens), else null.
function sheetBand(r) {
	const sp = $("src_panel");
	if (sp.hidden)
		return null;
	const pr = sp.getBoundingClientRect();
	if (pr.left > 0 || pr.top <= 0)
		return null;
	const top = Math.max(r.top, document.querySelector(".top").getBoundingClientRect().bottom), bottom = Math.min(r.bottom, pr.top);
	return bottom - top > 40 ? [top, bottom] : null;
}

// The Page tab's line on the jump paths of the listing.
function renderJumpInfo(r, dropped) {
	const el = $("jump_info");
	if (!r.nIns) {
		el.textContent = "";
		return;
	}
	let arcs = 0, none = 0, into = 0, outs = 0, seen = 0;
	for (const P of r.paths.values()) {
		if (P.noDirect)
			none++;
		if (P.into.length)
			into++;
		if (P.out.length)
			outs++;
		for (const e of P.out)
			seen += e.count;
	}
	arcs = Number($("listing").dataset.arcs || 0);
	const jit = jumpsJitNote();
	el.textContent = "Jump paths: " + arcs + " jumps inside the page drawn as arcs" + (dropped ? " (" + dropped + " more not drawn: the gutter is full)" : "") +
		"; " + into + " instructions with paths in (\u2190), " + outs + " with paths out (\u2192), " + seen.toLocaleString("en") +
		" transfers from here observed while stepping, the sources named above each instruction and targets out of view after it; " + none + " without a direct path in (\u2298)" +
		(r.source === "live" ? " from this page's live decode (static paths only from the " + r.jS.local.toLocaleString("en") +
			" instructions decoded here: jumps and calls from other code are not known)" : "") + (jit ? "; " + jit : "") + ".";
}

// Draw the selected page; base: bytes of the last scan (changes are marked).
function renderPage(p, bytes, base, how, curAt) {
	const t0 = realNow();
	// A wanted scroll counts from the start of the drawing: a large page
	// on a slow host takes longer to draw than the want lasts.
	const back = navScrollWant && navScrollWant.page === p && t0 < navScrollWant.until ? navScrollWant : null;
	const box = $("listing");
	const cur = curAt === undefined ? api.machine.currentPhys() : curAt;
	const r = listPage(p, bytes, base, cur >= p * PAGE && cur < p * PAGE + PAGE ? cur : -1, lineChars(box), how);
	box.innerHTML = r.html;
	box.dataset.jlocal = r.source === "live" ? "page's live decode" : "";
	const dropped = jumpsDrawn(box, r.jS, r.paths);
	renderJumpInfo(r, dropped);
	Object.assign(box.dataset, { page: p, insns: r.nIns, hexrows: r.nRows, nonzero: r.nonzero,
		changed: r.changed, reads: Number(box.dataset.reads || 0) + 1, draws: Number(box.dataset.draws || 0) + 1 });
	const hit = jumpHitScroll ? box.querySelector(".jhit") : null;
	const backLine = back && back.addr !== undefined ? listingLineAt(back.addr) : null;
	if (backLine && back.off !== undefined)
		box.scrollTop += backLine.getBoundingClientRect().top - box.getBoundingClientRect().top - back.off;
	else if (backLine)
		scrollListingTo(backLine);
	else if (back && back.addr === undefined)
		box.scrollTop = back.top;
	else if (hit) {
		setJumpHitScroll(false);
		scrollListingTo(hit);
	} else if (r.curLine >= 0) {
		const line = box.querySelector(".cur");
		if (line)
			scrollListingTo(line);
	}
	box.dataset.source = r.source;
	box.dataset.opsNamed = r.opsNamed;
	box.dataset.coverage = !r.nIns ? "" : r.cov.tint ? (r.cov.partial ? "partial" : "stepped") : "none";
	renderCovInfo(r);
	$("dump_info").textContent = (r.source === "build" ? r.nIns + " instructions (disasm.json)" :
		r.source === "live" ? r.nIns + " instructions decoded live from guest memory (iced-x86, " + r.bits + "-bit" +
			(r.misaligned ? "; the first lines outside a symbol may be misaligned" : "") + ")" :
		icedError ? "hex only: no disassembler (" + icedError + ")" : !iced ? "loading the disassembler..." : "no code in this page") +
		(r.nBad ? " (" + r.nBad + " differ from the build)" : "") +
		(r.nIns ? ", " + r.opsNamed + " memory operands named" + (r.opsWhy ? " (" + r.opsWhy + ")" : "") : "") +
		", " + r.nonzero + " non-zero bytes" + (base ? ", " + r.changed + " changed " + how : "") +
		", read " + new Date().toLocaleTimeString("en-GB");
	box._drawn = { page: p, stepper, bytes: bytes.slice(), base: base ? base.slice() : null, r, lines: null,
		at: realNow(), cost: realNow() - t0 };
}

// The listing's coverage key: how many of its instructions are tinted.
function renderCovInfo(r) {
	$("listing").dataset.unexecuted = r.cov.tint ? r.nNx : "";
	const key = $("cov_info");
	key.textContent = "";
	if (r.nIns && r.cov.tint) {
		const sw = document.createElement("span");
		sw.className = "nxkey tipmark";
		sw.textContent = "tinted";
		sw.tabIndex = 0;
		sw.setAttribute("role", "button");
		sw.dataset.tip = "Tinted lines: instructions that have not executed in this boot. The page records the address of every " +
			"instruction it steps; lines without the tint have executed. Code v86's JIT runs (the fast-forward to the kernel entry, " +
			"full speed) is not recorded" + (r.cov.partial ? ", and the JIT has run since the kernel entry, so tinted code may have run." : ".");
		key.append(sw, ": " + r.nNx + " of " + r.nIns + " instructions have not executed in this boot (recorded while stepping)" +
			(r.cov.partial ? "; v86's JIT has run since the kernel entry and records nothing, so tinted code may have run" : ""));
	} else if (r.nIns)
		key.textContent = "No execution coverage: " + r.cov.why + ".";
}

// ---- yellow lines: guest memory that differs from the build ---------------

// How far above its link address the bzImage loads vmlinux's first bytes:
// the minified Linux's bzImage carries a relocation stub of this many bytes
// at 0x100000 with vmlinux after it, and the stub moves vmlinux down right
// after the kernel entry. 0 when the payload is vmlinux itself (hello) or
// not found. From the image file and disasm.json, once per pair.
let stubShiftOf = { image: null, disasm: null, shift: 0 };
function stubShift() {
	if (!image || !disasm || !disasm.insns.length)
		return 0;
	const c = stubShiftOf;
	if (c.image === image && c.disasm === disasm)
		return c.shift;
	const ins = disasm.insns, lo = ins[0][0], want = [];
	for (const [pa, raw] of ins) {
		if (pa !== lo + want.length || want.length >= 48)
			break;
		for (let k = 0; k < raw.length; k += 2)
			want.push(parseInt(raw.substr(k, 2), 16));
	}
	const pay = image.bytes.subarray(image.hdr.setup_size, image.hdr.setup_size + 0x10000 + want.length);
	let shift = 0;
	for (let i = 0; i + want.length <= pay.length && want.length; i++)
		if (want.every((b, k) => pay[i + k] === b)) {
			shift = i - (lo - V86_KERNEL_ADDR);
			break;
		}
	stubShiftOf = { image, disasm, shift };
	return shift;
}

// The popover text of a yellow line: the instruction at pa (build bytes
// raw, hex) holds got in guest memory. Says which case it is: a Run to
// breakpoint, vmlinux not moved into place yet (the relocation stub), zero
// memory, or a change at run time.
function diffTip(pa, raw, got) {
	const n = raw.length / 2;
	let k = 0;
	for (let i = 0; i < n; i++)
		if (got[i] !== parseInt(raw.substr(i * 2, 2), 16))
			k++;
	const head = "Yellow: these bytes in guest memory differ from the build's disassembly (" + k + " of " + n + " bytes).\n";
	const sameAt = (a) => {
		if (a < 0 || a + n > emuRam)
			return false;
		const m = emulator.read_memory(a, n);
		return m.every((b, i) => b === parseInt(raw.substr(i * 2, 2), 16));
	};
	if (ffBreaks && got.some((b, i) => b === 0xf4 && ffBreaks.has(pa + i)))
		return head + "Run to has put HLT bytes (f4) over this instruction as breakpoints while the JIT runs to it; " +
			"they are put back when it stops.";
	const d = stubShift();
	if (d > 0 && sameAt(pa + d))
		return head + "vmlinux is not in place yet. The bzImage put a " + d + "-byte relocation stub at " + hex(V86_KERNEL_ADDR) +
			" and vmlinux right after it, so the build's bytes for this address are still " + hex(d) + " bytes higher, at " + hex(pa + d) + ". " +
			(pa < V86_KERNEL_ADDR + d ? "The bytes here are the relocation stub's own code, which runs first. " : "") +
			"The stub moves vmlinux down over itself right after the kernel entry; from then on these lines are plain.";
	if (got.every((b) => b === 0))
		return head + "The memory here is zero: nothing has been loaded here yet, or it was cleared.";
	const sec = symbols && symbols.sections.find((x) => pa >= x.paddr && pa < x.paddr + x.size);
	return head + "The code here changed at run time: the kernel patched it (alternatives, jump labels, static calls) or reused the memory" +
		(sec && /^\.init/.test(sec.name) ? " (" + sec.name + " is freed once the kernel has booted)" : "") + ", or something wrote here.";
}

// The marked bytes of a line that differ from the build: a mark with the
// line's popover (tap, Enter, hover).
const diffBytesHtml = (bytes, tip) => '<span class="difb tipmark" tabindex="0" role="button" aria-label="Why yellow" data-tip="' +
	escHtml(tip) + '">' + bytes + "</span>";

// The title of a listing line with EIP inside it (not at its start).
const midTitle = (cur) => "EIP " + hex(cur) + " is inside this instruction of the build's disassembly: the bytes here differ from the build " +
	"(the bzImage's relocation stub at 0x100000 right after the entry, or code changed at run time), so the CPU runs other code. " +
	"Code at EIP in the Debugger tab decodes it live.";

// Slow motion inside one page: move the EIP mark and untint what has
// executed instead of drawing the page's ~1,400 lines again (150 ms on a
// desktop, 0.6-1 s on a phone-class CPU). The whole listing, with the
// observed counts, is drawn again every LISTING_REFRESH_MS (or six times
// as long as its drawing took), and whenever the page or its bytes change.
const LISTING_REFRESH_MS = 4000;
function listingFollow() {
	const box = $("listing"), d = box._drawn, p = selectedPage, cur = api.machine.currentPhys();
	if (!d || d.page !== p || d.stepper !== stepper || !emuReady || jumpHitScroll || cur < p * PAGE || cur >= p * PAGE + PAGE ||
		realNow() - d.at >= Math.max(LISTING_REFRESH_MS, 6 * d.cost))
		return false;
	const same = (x, y) => x === y || (!!x && !!y && x.length === y.length && x.every((v, i) => v === y[i]));
	if (!same(d.bytes, readPage(p)) || !same(d.base, scanBase(p)))
		return false;
	if (!d.lines) {
		d.lines = new Map();
		for (const el of box.querySelectorAll(".ins[data-addr]"))
			d.lines.set(Number(el.dataset.addr), el);
	}
	let line = d.lines.get(cur), mid = false;
	if (!line && d.r.source === "build") {
		const prev = disasm.insns[insnIndex(cur) - 1];
		mid = !!prev && cur < prev[0] + prev[1].length / 2;
		line = mid ? d.lines.get(prev[0]) : null;
	}
	if (!line || !line.isConnected)
		return false;
	for (const el of box.querySelectorAll(".cur")) {
		el.classList.remove("cur", "mid", "tipmark");
		if (el.dataset.eip !== undefined) {
			delete el.dataset.eip;
			const b = el.querySelector(".difb"); // a yellow line keeps its own text
			if (b)
				el.dataset.tip = b.dataset.tip;
			else
				delete el.dataset.tip;
		}
	}
	line.classList.add("cur");
	if (mid) {
		const b = line.querySelector(".difb");
		line.classList.add("mid", "tipmark");
		line.dataset.eip = cur;
		line.dataset.tip = (b ? b.dataset.tip + "\n\n" : "") + midTitle(cur);
	}
	if (d.r.cov.tint) {
		for (const el of box.querySelectorAll(".ins.nx"))
			if (stepper.executed(Number(el.dataset.addr))) {
				el.classList.remove("nx");
				d.r.nNx--;
			}
		renderCovInfo(d.r);
	}
	// .ins.cur is a little taller: the arcs are measured again.
	drawJumpArcs(box, box._jarcs || []);
	scrollListingTo(line);
	box.dataset.draws = Number(box.dataset.draws || 0) + 1;
	box.dataset.moves = Number(box.dataset.moves || 0) + 1;
	return true;
}

function inspect() {
	const p = selectedPage;
	if (p < 0)
		return;
	if (p >= gridRam() / PAGE) {
		setSelectedPage(-1);
		return;
	}
	$("page_title").textContent = "Page " + hex(p * PAGE) + "–" + hex(p * PAGE + PAGE - 1) +
		" (" + pageLabel(p) + ")";
	if ($("symbol_index").dataset.page !== String(p) || $("symbol_index").dataset.kernel !== (image && image.name)) {
		renderSymbolIndex(p);
		$("symbol_index").dataset.page = p;
		$("symbol_index").dataset.kernel = image && image.name;
	}
	const snap = pageSnapView && pageSnapView.page === p ? pageSnapView : null;
	pageSnapBanner(!!snap);
	if (snap) {
		renderPage(p, snap.bytes, snap.base, "against " + snap.against, snap.cur);
		return;
	}
	if (!emulator || !emuReady) {
		$("listing").textContent = emulator ? "Booting..." : "Boot a kernel to read this page.";
		return;
	}
	renderPage(p, readPage(p), scanBase(p), "since the last scan");
}

function selectPage(p) {
	api.show.navJump(() => {
		setSelectedPage(p);
		drawGrid();
		inspect();
		setLive();
		api.show.showTab("page");
	});
}

function setLive() {
	clearInterval(liveTimer);
	liveTimer = null;
	if ($("live").checked && emulator && selectedPage >= 0)
		liveTimer = setInterval(() => {
			if (emulator && (runState === "jit" || runState === "stepped"))
				inspect();
		}, LIVE_MS);
}
let navScrollWant = null;  // { page, top or addr, until }: the listing's scroll for renderPage after Back or jumpTo

// The listing's last line at or before address a.
function listingLineAt(a) {
	let best = null;
	for (const line of $("listing").querySelectorAll("[data-addr]"))
		if (Number(line.dataset.addr) <= a)
			best = line;
	return best;
}

// Other modules change them through these (an imported binding is read-only).
const setNavScrollWant = (v) => { navScrollWant = v; };

// The Page tab reads its page again each time it is shown (showTab).
state.subscribe(() => {
	if (tab === "page")
		inspect();
}, ["tabShows"]);

// The page API's entries of this module (page.js).
provide({ renderPage, coverageFor, inspect, selectPage, insnIndex, setLive });

export { inspect, navScrollWant, listingLineAt, setLive, insnIndex, listingFollow, scrollListingTo, sheetBand,
	diffTip, diffBytesHtml, selectPage, setNavScrollWant };
