"use strict";

// Jump paths in the listings (jumps.js has the model and the counts):
// the sources above an instruction and its targets after it, the arcs,
// the popover, the Debugger's jumps at EIP.

import { hex, escHtml, $ } from "./util.js";
import { stepper, api } from "./page.js";
import { asmTargetName } from "./asmnames.js";
import { jumpsIndex, JUMP_TEXT, JK_ARC_MASK, SVG_NS, jumpPaths, jumpName, JK_TEXT, JK_LATE, jumpNoneText,
	jumpTakenText, jumpsJitNote, jumpKindsText, jumpCountText, setJumpHit, setJumpHitScroll, jumpNoneScope,
	JUMP_NONE_OTHER } from "./jumps.js";

// ---- inline: the sources above an instruction, its targets after it ----
//
// Without a click: above each instruction that a jump, call, return,
// interrupt or exception reaches, "← n paths in:" and the first few
// sources (the instruction's word, its name as llvm-objdump writes a
// target, file:line, how often taken), "+k more" opening the list; after
// an instruction its targets the text does not name (observed returns,
// indirect jumps and calls, exceptions) and, for a direct one out of
// view, a button to it. Each named end says where it is: ↑ or ↓ in the
// same function out of view, ↗ another function; the ends in view have
// their arc. Out-of-view ends also get a stub at the gutter edge.

// Kind words for observed ends (a static end shows its own mnemonic).
const JK_WORD = ["", "jcc", "jmp", "call", "ret", "loop", "int", "iret", "sysenter/sysexit", "jmp *", "call *", "ljmp",
	"lcall", "lret", "exception", "interrupt", "ud2"];
const JUMP_GLYPH = { up: "↑", down: "↓", far: "↗", view: "" };
const JUMP_WHERE_TEXT = { up: "earlier in this function, out of view", down: "later in this function, out of view",
	far: "in another function", view: "in view (its arc)" };

// Ends named inline before "+n more" in box.
const jumpInlineMax = (box) => (box.clientWidth < 520 ? 2 : 3);

// The word for the transfer at x along e: x's mnemonic when e is of its
// kind (jne, calll, retl), then the other kinds observed there
// (interrupt, exception).
function jumpWord(S, e, x) {
	const i = S && x >= 0 ? jumpsIndex(S, x) : -1;
	const own = i >= 0 && S.kind[i] && e.mask & 1 << S.kind[i] ? S.kind[i] : 0;
	const m = own ? JUMP_TEXT.exec(S.insns[i][2]) : null;
	const words = m ? [m[1]] : [];
	for (let k = 1; k < JK_WORD.length; k++)
		if (e.mask & 1 << k && k !== own)
			words.push(JK_WORD[k]);
	return words.join("/");
}

// Where at is seen from pa: "view" (in the listing), "up" or "down" (the
// same function), "far" (another function, or outside the image).
function jumpWhere(pa, at, inView, lookup) {
	if (at < 0)
		return "far";
	if (inView(at))
		return "view";
	if (!lookup)
		return at < pa ? "up" : "down";
	const a = lookup(pa), b = lookup(at);
	return a && b && a.paddr === b.paddr && a.name === b.name ? (at < pa ? "up" : "down") : "far";
}

// One named end: "jne from ↑put_line+0x20 hello.c:16 ×3".
function jumpEndHtml(S, e, pa, dir, inView, lookup) {
	const where = jumpWhere(pa, e.at, inView, lookup);
	const name = e.at === -2 ? hex(e.va) : asmTargetName(e.at, lookup) || hex(e.at);
	let h = '<span class="je" data-at="' + e.at + '" data-where="' + where + '">' + escHtml(jumpWord(S, e, dir === "in" ? e.at : pa)) +
		(dir === "in" ? " from " : " to ");
	const label = (JUMP_GLYPH[where] ? JUMP_GLYPH[where] + "\u2009" : "") + name;
	h += e.at >= 0 ? '<button type="button" class="jto" data-go="' + e.at + '" data-tip="' + escHtml(name + " (" + hex(e.at) + "), " +
		JUMP_WHERE_TEXT[where] + ": go there in the Page tab") + '">' + escHtml(label) + "</button>" : escHtml(label);
	if (e.at >= 0 && typeof api.symbols.dbgHas === "function" && api.symbols.dbgHas()) {
		const r = api.symbols.dbgFrames(e.at);
		if (r.state === "ok" && r.frames[0].file && r.frames[0].line)
			h += " " + api.source.srcLinkHtml(r.frames[0].file, r.frames[0].line, "", e.at);
	}
	if (e.count)
		h += " ×" + e.count.toLocaleString("en");
	return h + "</span>";
}

// The inline parts of the instruction at pa (P from jumpPaths, S its
// static paths, inView(addr): whether the listing shows that
// instruction, max: ends named before "+n more"): { src: the line above
// it ("" if no path comes in), dst: after it, sin and sout: the ways of
// its ends out of view (for the gutter stubs) }.
function jumpInlineHtml(S, P, pa, inView, max) {
	const lookup = typeof api.symbols.kernelSymLookup === "function" ? api.symbols.kernelSymLookup() : null;
	const r = { src: "", dst: "", sin: "", sout: "" };
	const ways = (list) => [...new Set(list.map((e) => jumpWhere(pa, e.at, inView, lookup)).filter((w) => w !== "view"))].join(" ");
	const more = (n, dir) => '<button type="button" class="jmore" data-pa="' + pa + '" data-dir="' + dir + '" data-tip="All ' + n +
		" paths " + (dir === "in" ? "into" : "out of") + ' this instruction">+' + n + " more</button>";
	if (P.into.length) {
		const n = P.into.length, shown = P.into.slice(0, max);
		r.src = '<span class="jsrc" data-addr="' + pa + '" data-n="' + n + '">← ' + n + " path" + (n > 1 ? "s" : "") + " in: " +
			shown.map((e) => jumpEndHtml(S, e, pa, "in", inView, lookup)).join(", ") + (n > shown.length ? ", " + more(n - shown.length, "in") : "") + "</span>";
		r.sin = ways(P.into);
	}
	// Out: the static direct target is in the instruction's text (its
	// <name>); out of view it gets a button. Other targets are named.
	const i = S ? jumpsIndex(S, pa) : -1;
	const direct = i >= 0 && S.target[i] !== -1 ? P.out.find((e) => e.stat) : null;
	const rest = P.out.filter((e) => e !== direct);
	let d = "";
	if (direct && direct.at >= 0) {
		const where = jumpWhere(pa, direct.at, inView, lookup);
		if (where !== "view") {
			const name = asmTargetName(direct.at, lookup) || hex(direct.at);
			const t = "Target " + name + " (" + hex(direct.at) + "), " + JUMP_WHERE_TEXT[where] + ": go there in the Page tab";
			d += '<span class="je" data-at="' + direct.at + '" data-where="' + where + '"><button type="button" class="jto" data-go="' + direct.at +
				'" data-tip="' + escHtml(t) + '" aria-label="' + escHtml(t) + '">' + JUMP_GLYPH[where] + "</button></span>";
		}
	}
	if (rest.length) {
		const shown = rest.slice(0, max);
		d += (d ? " " : "") + "→ " + shown.map((e) => jumpEndHtml(S, e, pa, "out", inView, lookup)).join(", ") +
			(rest.length > shown.length ? ", " + more(rest.length - shown.length, "out") : "");
	}
	if (d)
		r.dst = ' <span class="jdst" data-addr="' + pa + '">' + d + "</span>";
	r.sout = ways(P.out);
	return r;
}

// The .ins attributes that ask drawJumpArcs for gutter stubs.
const jumpStubAttrs = (J) => (J.sin ? ' data-sin="' + J.sin + '"' : "") + (J.sout ? ' data-sout="' + J.sout + '"' : "");

// What a gutter stub means, by its way (jumpStubsSvg).
const JUMP_STUB_TEXT = {
	up: "A path comes into this line from earlier in this function, out of view: the line above it names the source (↑).",
	down: "A path comes into this line from later in this function, out of view: the line above it names the source (↓).",
	far: "A path comes into this line from another function: the line above it names the source (↗).",
	"out-up": "This line jumps to an earlier place in this function, out of view: ↑ after the line goes there.",
	"out-down": "This line jumps to a later place in this function, out of view: ↓ after the line goes there.",
	"out-far": "This line jumps or calls into another function, or its target is known only at run time: ↗ or the name after the line goes there.",
};

// Stubs at the gutter edge for the ends out of view (.ins data-sin and
// data-sout, from jumpInlineHtml): into the line from above, below or
// afar (a circle), and out of it the same ways.
function jumpStubsSvg(box, y, xR) {
	let out = "";
	const xS = xR - 10;
	for (const el of box.querySelectorAll(".ins[data-sin], .ins[data-sout]")) {
		const pa = Number(el.dataset.addr), yl = y(pa);
		if (yl === null)
			continue;
		// Each stub also gets a wider invisible stroke on top that takes
		// the pointer: its popover says what the stub is.
		const path = (way, d) => '<path class="jstub" data-addr="' + pa + '" data-way="' + way + '" d="' + d + '"/>' +
			'<path class="jstubhit tipmark" data-addr="' + pa + '" data-way="' + way + '" data-tip="' + JUMP_STUB_TEXT[way] + '" d="' + d + '"/>';
		for (const w of (el.dataset.sin || "").split(" ").filter(Boolean)) {
			if (w === "far")
				out += path(w, "M" + (xS - 2) + " " + yl + "H" + (xR - 3)) + '<circle class="jstubend" cx="' + (xS - 3) + '" cy="' + yl + '" r="1.6"/>';
			else
				out += path(w, "M" + xS + " " + (yl + (w === "up" ? -8 : 8)) + "V" + yl + "H" + (xR - 3));
			out += '<path class="jarrow" d="M' + xR + " " + yl + 'l-4 -3v6z"/>';
		}
		const yo = yl + 2;
		for (const w of (el.dataset.sout || "").split(" ").filter(Boolean)) {
			if (w === "far")
				out += path("out-far", "M" + (xR - 3) + " " + yo + "H" + (xS - 1)) + '<path class="jarrow" d="M' + (xS - 4) + " " + yo + 'l4 -3v6z"/>';
			else {
				const e = yo + (w === "up" ? -9 : 9);
				out += path("out-" + w, "M" + (xR - 3) + " " + yo + "H" + xS + "V" + e) +
					'<path class="jarrow" d="M' + xS + " " + (e + (w === "up" ? -1 : 1)) + "l-3 " + (w === "up" ? 4 : -4) + "h6z\"/>";
			}
		}
	}
	return out;
}

// The arcs of a listing: jumps (JK_ARC) between instructions it shows.
function jumpArcs(paths) {
	const arcs = [];
	for (const [pa, P] of paths)
		for (const e of P.out)
			if (e.mask & JK_ARC_MASK && e.at !== pa && paths.has(e.at))
				arcs.push({ from: pa, to: e.at, taken: e.count > 0, stat: e.stat });
	return arcs;
}

// Draw arcs in the gutter left of box's lines, like objdump
// --visualize-jumps: the shorter an arc the nearer the code. Columns go
// by address, so they do not depend on how the lines wrap; positions are
// measured from the lines (an SVG over the left padding).
function drawJumpArcs(box, arcs) {
	box._jarcs = arcs;
	const narrow = box.clientWidth < 520;
	const maxCols = narrow ? 4 : 10, COL = narrow ? 5 : 6;
	const iv = arcs.map((a) => ({ a, lo: Math.min(a.from, a.to), hi: Math.max(a.from, a.to), col: -1 }))
		.sort((x, y) => (x.hi - x.lo) - (y.hi - y.lo) || x.lo - y.lo);
	const cols = [];
	let dropped = 0;
	for (const v of iv) {
		let c = 0;
		while (c < maxCols && (cols[c] || []).some(([l, h]) => v.lo <= h && l <= v.hi))
			c++;
		if (c === maxCols) {
			dropped++;
			continue;
		}
		(cols[c] = cols[c] || []).push([v.lo, v.hi]);
		v.col = c;
	}
	const stubs = box.querySelector(".ins[data-sin], .ins[data-sout]") !== null;
	const STUB = stubs ? 13 : 0;
	const gutter = cols.length || stubs ? cols.length * COL + 9 + STUB : 0;
	box.style.setProperty("--jgutter", gutter + "px");
	box.dataset.arcs = arcs.length - dropped;
	box.dataset.arcsDropped = dropped;
	let svg = box.querySelector(":scope > svg.jarcs");
	if (!cols.length && !stubs) {
		if (svg)
			svg.remove();
		return dropped;
	}
	if (!svg) {
		svg = document.createElementNS(SVG_NS, "svg");
		svg.setAttribute("class", "jarcs");
		svg.setAttribute("aria-hidden", "true");
		box.prepend(svg);
	}
	const pad = parseFloat(getComputedStyle(box).paddingLeft) || gutter;
	const br = box.getBoundingClientRect();
	const yOf = new Map();
	// The lines by address, found once (a querySelector per end was
	// quadratic in a page's ~1400 lines).
	let lines = null;
	const y = (pa) => {
		if (!yOf.has(pa)) {
			if (!lines) {
				lines = new Map();
				for (const el of box.querySelectorAll(".ins[data-addr]"))
					if (!lines.has(Number(el.dataset.addr)))
						lines.set(Number(el.dataset.addr), el);
			}
			const el = lines.get(pa);
			const r = el ? el.getClientRects()[0] : null;
			yOf.set(pa, r ? r.top - br.top + box.scrollTop + r.height / 2 : null);
		}
		return yOf.get(pa);
	};
	const xR = pad - 3;
	let out = "";
	for (const v of iv) {
		if (v.col < 0)
			continue;
		const y1 = y(v.a.from), y2 = y(v.a.to);
		if (y1 === null || y2 === null)
			continue;
		const x = xR - 6 - STUB - v.col * COL, d = y2 > y1 ? 1 : -1, r = Math.min(3, Math.abs(y2 - y1) / 2);
		const cls = "jarc" + (v.a.taken ? " taken" : "") + (v.a.stat ? "" : " dyn");
		out += '<path class="' + cls + '" data-from="' + v.a.from + '" data-to="' + v.a.to + '" d="M' + xR + " " + y1 + "H" + (x + r) +
			"Q" + x + " " + y1 + " " + x + " " + (y1 + d * r) + "V" + (y2 - d * r) + "Q" + x + " " + y2 + " " + (x + r) + " " + y2 + "H" + (xR - 3) + '"/>' +
			'<path class="jarrow' + (v.a.taken ? " taken" : "") + '" d="M' + xR + " " + y2 + "l-4 -3v6z\"/>";
	}
	out += jumpStubsSvg(box, y, xR);
	svg.setAttribute("width", pad);
	svg.setAttribute("height", box.scrollHeight);
	svg.innerHTML = out;
	return dropped;
}

// After a listing's HTML is in box: its paths for the list, its arcs.
function jumpsDrawn(box, S, paths) {
	box._jS = S;
	return drawJumpArcs(box, jumpArcs(paths));
}

function jumpPopClose(refocus) {
	const pop = $("jump_pop");
	if (pop.hidden)
		return;
	pop.hidden = true;
	if (!refocus || !pop._from)
		return;
	// The listing may have been drawn again meanwhile: its new button for
	// the same instruction and direction.
	let b = pop._from;
	if (!b.isConnected && pop._box)
		b = pop._box.querySelector('button.jc[data-pa="' + pop.dataset.pa + '"][data-dir="' + pop.dataset.dir + '"]');
	if (b)
		b.focus();
}

// The list of paths into or out of an instruction, next to the button.
function jumpPopShow(btn) {
	const pop = $("jump_pop");
	const pa = Number(btn.dataset.pa), dir = btn.dataset.dir;
	const box = btn.closest("[data-jbox]");
	const S = box ? box._jS : null;
	const P = jumpPaths(S, stepper, pa);
	const list = dir === "in" ? P.into : P.out;
	pop._from = btn;
	pop._box = box;
	pop.dataset.pa = pa;
	pop.dataset.dir = dir;
	pop.replaceChildren();
	const head = document.createElement("div");
	head.className = "jhead";
	const title = document.createElement("b");
	title.textContent = (dir === "in" ? "Paths into " : "Paths out of ") + jumpName(pa) + ": " + list.length;
	const close = document.createElement("button");
	close.type = "button";
	close.className = "jclose";
	close.textContent = "Close";
	close.addEventListener("click", () => jumpPopClose(true));
	head.append(title, " ", close);
	pop.append(head);
	const ul = document.createElement("ul");
	for (const e of list) {
		const li = document.createElement("li");
		li.dataset.at = e.at;
		li.dataset.count = e.count;
		li.dataset.stat = e.stat ? 1 : 0;
		li.dataset.kinds = jumpKindsText(e.mask);
		const go = document.createElement("button");
		go.type = "button";
		go.className = "symlink jgo";
		go.textContent = jumpName(e.at, e.va);
		if (e.at >= 0)
			go.addEventListener("click", () => { jumpPopClose(false); jumpGo(e.at); });
		else
			go.disabled = true;
		li.append(jumpKindsText(e.mask) + (dir === "in" ? " from " : " to "), go);
		if (e.at >= 0 && typeof api.symbols.dbgHas === "function" && api.symbols.dbgHas()) {
			const r = api.symbols.dbgFrames(e.at);
			if (r.state === "ok" && r.frames[0].file && r.frames[0].line)
				li.append(" ", api.source.srcLinkEl(r.frames[0].file, r.frames[0].line, "", e.at));
		}
		li.append(", " + jumpCountText(e));
		ul.append(li);
	}
	pop.append(ul);
	const notes = [];
	if (!list.length && dir === "out" && P.late)
		notes.push("A " + JK_TEXT[P.late] + ": where it goes is known only at run time, and no transfer from here has been observed yet.");
	else if (dir === "out" && JK_LATE.has(P.late))
		notes.push("A " + JK_TEXT[P.late] + ": the targets listed are the ones observed while stepping.");
	if (dir === "in" && P.noDirect)
		notes.push(jumpNoneText(P));
	else if (dir === "in" && P.taken)
		notes.push(jumpTakenText(P) + ".");
	if (S && box && box.dataset.jlocal)
		notes.push("Static paths only from the " + box.dataset.jlocal + " (no disasm.json): jumps from other code are not known.");
	const jit = jumpsJitNote();
	if (jit)
		notes.push("Observed paths: " + jit + ".");
	for (const t of notes) {
		const p = document.createElement("p");
		p.className = "note";
		p.textContent = t;
		pop.append(p);
	}
	pop.hidden = false;
	const r = btn.getBoundingClientRect();
	const w = Math.min(420, window.innerWidth - 16);
	pop.style.width = w + "px";
	const x = Math.min(Math.max(8, r.left), window.innerWidth - w - 8);
	let y = r.bottom + 4;
	pop.style.left = x + "px";
	pop.style.top = y + "px";
	const h = pop.offsetHeight;
	if (y + h > window.innerHeight - 8)
		pop.style.top = Math.max(8, r.top - h - 4) + "px";
	(pop.querySelector("button.jgo:not(:disabled)") || close).focus({ preventScroll: true });
}

// "go there": the Page tab at pa, its line marked.
function jumpGo(pa) {
	api.show.navJump(() => {
		setJumpHit(pa);
		setJumpHitScroll(true);
		api.show.jumpTo(pa);
	});
}

// The Debugger's summary of EIP's own paths.
function renderEipJumps(S, pa) {
	const el = $("eip_jumps");
	el._jS = S;
	el.replaceChildren();
	if (pa < 0 || !S) {
		el.textContent = pa < 0 ? "" : "Paths at EIP: no disassembly here.";
		return;
	}
	const P = jumpPaths(S, stepper, pa);
	const part = (dir, list) => {
		const span = document.createElement("span");
		span.className = "jpart";
		span.dataset.dir = dir;
		span.dataset.n = list.length;
		span.append((dir === "in" ? "Paths into EIP: " : "Paths out: ") + list.length);
		if (list.length || (dir === "out" && P.late)) {
			const b = document.createElement("button");
			b.type = "button";
			b.className = "jc";
			b.dataset.pa = pa;
			b.dataset.dir = dir;
			b.textContent = "list";
			span.append(" ", b);
		}
		const show = list.slice(0, 3).map((e) => jumpKindsText(e.mask) + (dir === "in" ? " from " : " to ") + jumpName(e.at, e.va) + " (" + jumpCountText(e) + ")");
		if (show.length)
			span.append(": " + show.join("; ") + (list.length > 3 ? "; " + (list.length - 3) + " more" : ""));
		else if (dir === "out" && JK_LATE.has(P.late))
			span.append(" (a " + JK_TEXT[P.late] + ": known at run time)");
		if (dir === "in" && P.noDirect)
			span.append(P.local ? " (no direct path " + jumpNoneScope(P) + "; " + JUMP_NONE_OTHER + ")" :
				" (no path leads here: nothing jumps or calls here, its address is nowhere in the kernel and the instruction before does not fall through)");
		else if (dir === "in" && P.taken)
			span.append(" (" + jumpTakenText(P).replace(/^./, (c) => c.toLowerCase()) + ")");
		return span;
	};
	el.append(part("in", P.into), document.createElement("br"), part("out", P.out));
	const jit = jumpsJitNote();
	if (jit) {
		el.append(document.createElement("br"), "Observed paths: " + jit + ".");
	}
	el.dataset.pa = pa;
	el.dataset.into = P.into.length;
	el.dataset.out = P.out.length;
}

function initJumps() {
	document.addEventListener("click", (ev) => {
		const go = ev.target.closest ? ev.target.closest("button.jto") : null;
		if (go) {
			ev.stopPropagation();
			ev.preventDefault();
			jumpPopClose(false);
			jumpGo(Number(go.dataset.go));
			return;
		}
		const b = ev.target.closest ? ev.target.closest("button.jc, button.jmore") : null;
		if (b) {
			ev.stopPropagation();
			ev.preventDefault();
			const pop = $("jump_pop");
			if (!pop.hidden && pop._from === b)
				jumpPopClose(false);
			else
				jumpPopShow(b);
			return;
		}
		const pop = $("jump_pop");
		if (!pop.hidden && !pop.contains(ev.target))
			jumpPopClose(false);
	}, true);
	// v86 takes every key on the window (preventDefault): Enter and Space
	// click the buttons here first, Escape closes the list.
	document.addEventListener("keydown", (ev) => {
		const b = ev.target.closest ? ev.target.closest("button.jc, button.jmore, button.jto, #jump_pop button") : null;
		if (b && (ev.key === "Enter" || ev.key === " ")) {
			ev.preventDefault();
			ev.stopPropagation();
			b.click();
		} else if (ev.key === "Escape" && !$("jump_pop").hidden) {
			ev.stopPropagation();
			jumpPopClose(true);
		}
	}, true);
	// Lines wrap differently at another width: draw the arcs again.
	if (typeof ResizeObserver === "function") {
		const ro = new ResizeObserver((entries) => {
			for (const en of entries)
				if (en.target._jarcs)
					drawJumpArcs(en.target, en.target._jarcs);
		});
		for (const id of ["listing", "context"])
			ro.observe($(id));
	}
}

export { jumpInlineMax, jumpInlineHtml, jumpStubAttrs, drawJumpArcs, jumpsDrawn, jumpPopClose, jumpGo,
	renderEipJumps, initJumps };
