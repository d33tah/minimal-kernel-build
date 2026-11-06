"use strict";

// What DW_TAG_subprogram says about a function (dbginfo.js dbgFnFacts,
// dwarf.js DwarfFuncs): its prototype, where it is declared, its
// linkage, whether it was declared inline and inlined, its out-of-line
// code with the frame the call frame information gives it, and every
// inlined copy. One render path, fnFactsEl, for the Debugger's function
// rows, the Coverage tab's function details and the Graph tab's details;
// fnFactsText says the same in one line for symbol tooltips.

// Inlined copies listed before "and N more".
const FN_COPIES_SHOWN = 40;
// Out-of-line copies listed before "and N more" (static inline helpers
// can have one per compile unit).
const FN_OOL_SHOWN = 8;
// DW_AT_inline values (DWARF 5, 7.13); -1: the DIE has none.
const FN_INLINE = {
	"-1": "not declared inline",
	0: "not inlined (DW_INL_not_inlined)",
	1: "not declared inline, inlined by the compiler (DW_INL_inlined)",
	2: "declared inline, not inlined (DW_INL_declared_not_inlined)",
	3: "declared inline and inlined (DW_INL_declared_inlined)",
};
const fnOpen = new Set(); // keys of open copy lists

// A function the DWARF only names: no declaration, no prototype (an
// assembler's -g output records the name and the code only).
const fnBare = (e) => e[1] < 0 && !(e[3] & 8) && e[5] < 0 && !e[6].length;

// The prototype as the page shows it.
const fnProto = (T, e) => fnBare(e) ? e[0] + ": no prototype or declaration in its DWARF (assembly: only the name and the code are recorded)" :
	dwFuncProto(T, e);

// Out-of-line code of entry e: [[lo, length], ...] per DW_TAG_subprogram.
const fnParts = (e) => e[7].map((r) => { const out = []; for (let k = 0; k < r.length; k += 2) out.push([r[k], r[k + 1]]); return out; });

// "start_kernel+0x23" for code at pa (the ELF symbols), "" without one.
function fnSymAt(pa) {
	const st = typeof stepper !== "undefined" ? stepper : null;
	if (st) {
		const i = st.symbolOf(pa);
		return i >= 0 ? st.symName[i] + (pa > st.symStart[i] ? "+0x" + (pa - st.symStart[i]).toString(16) : "") : "";
	}
	const s = typeof symbols !== "undefined" && symbols ? symbolAt(symbols.symbols, pa) : null;
	return s ? s.name + (pa > s.paddr ? "+0x" + (pa - s.paddr).toString(16) : "") : "";
}

function fnLinkage(e) {
	return e[3] & 1 ? "external (DW_AT_external: visible to other files)" : "static (no DW_AT_external: this file only)";
}

function fnInlining(e) {
	const n = e[8].length, ool = e[7].length;
	return FN_INLINE[e[4]] + "; " + (n ? n.toLocaleString("en") + " inlined cop" + (n === 1 ? "y" : "ies") : "not inlined anywhere") +
		"; " + (ool ? ool + " out-of-line cop" + (ool === 1 ? "y" : "ies") : "no out-of-line code (only inlined)");
}

// The frame of out-of-line code at lo, from the call frame information.
function fnFrameText(lo) {
	const f = dbgCfiFrame(lo);
	return f === "loading" ? "reading the call frame information…" : f === null ? "no call frame information covers it" : cfiFrameText(f);
}

// One line: "int foo(int a); defined at f.c:12; static; ...; frame ...".
function fnFactsText(x, T) {
	const e = x.e;
	const parts = fnParts(e);
	return fnProto(T, e) + "; defined at " + (x.file ? x.file + ":" + e[2] : "an unknown place") + "; " +
		(e[3] & 1 ? "external" : "static") + "; " + fnInlining(e) + (e[3] & 4 ? "; does not return" : "") +
		(parts.length ? "; frame: " + fnFrameText(parts[0][0][0]) : "");
}

// Open the declaration: the whole file at that line (a shipped kernel:
// its file list says which lines have code), else (an import) the
// function around its code.
function fnShowDecl(x) {
	if (x.file && dbgFiles().state !== "none")
		openSourceLine(x.file, x.e[2]);
	else if (x.e[7].length)
		openSource(x.e[7][0][0]);
}

// The facts of x ({ e, file } from dbgFnFacts) as an element; key names
// the place (its copy list stays open across re-renders).
function fnFactsEl(x, T, key) {
	const e = x.e;
	const d = document.createElement("div");
	d.className = "fnfacts";
	const parts = fnParts(e);
	Object.assign(d.dataset, { name: e[0], proto: fnBare(e) ? "" : dwFuncProto(T, e), type: fnBare(e) ? "" : dwFuncProto(T, e, false), file: x.file || "", line: e[2],
		external: e[3] & 1 ? 1 : 0, inline: e[4], noreturn: e[3] & 4 ? 1 : 0, copies: e[8].length, ool: JSON.stringify(parts) });
	const proto = document.createElement("div");
	proto.className = "fnproto mono";
	const pt = document.createElement("span");
	pt.className = "srctext";
	pt.textContent = fnProto(T, e);
	proto.append(pt);
	proto.title = "The prototype from DW_TAG_subprogram: return type (DW_AT_type), parameters (DW_TAG_formal_parameter), ... for DW_TAG_unspecified_parameters";
	d.append(proto);
	const at = document.createElement("div");
	const loc = symButton("", -1, "Where DW_AT_decl_file and DW_AT_decl_line put the function: open it in the Source panel");
	loc.className = "symlink fnloc";
	if (x.file) {
		loc.append(srcLocEl(x.file + ":" + e[2]));
		loc.disabled = false;
		loc.addEventListener("click", () => fnShowDecl(x));
	} else
		loc.textContent = "an unknown place (no DW_AT_decl_file)";
	at.append("defined at ", loc, "; " + fnLinkage(e) + "; " + FN_INLINE[e[4]] + (e[3] & 4 ? "; does not return (DW_AT_noreturn)" : ""));
	d.append(at);
	const code = document.createElement("div");
	code.className = "fnool";
	if (!parts.length)
		code.textContent = "no out-of-line code: every call was inlined";
	parts.slice(0, FN_OOL_SHOWN).forEach((p, k) => {
		const line = document.createElement("div");
		line.append(parts.length > 1 ? "out-of-line copy " + (k + 1) + ": " : "code: ");
		p.forEach(([lo, len], j) => {
			const b = symButton(hex(lo), lo, "Show this code in the Page tab");
			b.dataset.addr = lo;
			line.append(j ? "; " + (/\.cold$/.test(fnSymAt(lo).replace(/\+.*/, "")) ? "cold part " : "part ") : "", b,
				" " + fnSymAt(lo) + " (" + len.toLocaleString("en") + " bytes)");
		});
		// The line its first instruction belongs to (gdb's info line NAME).
		const fr0 = dbgFrames(p[0][0]);
		const en = document.createElement("span");
		en.title = "The line table's line at its first instruction (what gdb's info line gives for the function)";
		if (fr0.state === "ok" && fr0.frames[0].file && fr0.frames[0].line) {
			en.dataset.entry = fr0.frames[0].file + ":" + fr0.frames[0].line;
			en.append("; starts at ", srcLocEl(en.dataset.entry));
		} else
			en.append(fr0.state === "loading" ? "; reading its line…" : "; its first instruction has no line information");
		line.append(en);
		const fr = document.createElement("div");
		fr.className = "fnframe";
		fr.textContent = "frame: " + fnFrameText(p[0][0]);
		const info = dbgCfiFrame(p[0][0]);
		if (info && info !== "loading" && !info.why)
			fr.dataset.frame = JSON.stringify({ max: info.max, fp: info.fp, fpOff: info.fpOff, saved: info.saved });
		fr.title = "From the call frame information (.debug_frame) of the code at " + hex(p[0][0]) + ": how far below the CFA (the stack pointer before the call) ESP goes, which registers the function saves and where; a frame-pointer function's own part below EBP is not described there.";
		line.append(fr);
		code.append(line);
	});
	if (parts.length > FN_OOL_SHOWN)
		code.append("and " + (parts.length - FN_OOL_SHOWN) + " more out-of-line copies");
	d.append(code);
	const n = e[8].length;
	const inl = document.createElement("details");
	inl.className = "fncopies";
	const k = key + "\0" + e[0] + "\0" + e[2];
	inl.open = fnOpen.has(k);
	inl.addEventListener("toggle", () => { if (inl.open) fnOpen.add(k); else fnOpen.delete(k); fill(); });
	const sum = document.createElement("summary");
	sum.textContent = fnInlining(e).split("; ").slice(1).join("; ");
	inl.append(sum);
	// The list is made when opened: a distro kernel's helpers have thousands of copies.
	const fill = () => {
		if (!inl.open || inl.querySelector("ul"))
			return;
		const ul = document.createElement("ul");
		for (const c of e[8].slice(0, FN_COPIES_SHOWN)) {
			const li = document.createElement("li");
			const b = symButton(hex(c), c, "Show this inlined copy in the Page tab");
			b.dataset.addr = c;
			li.append(b, " in " + (fnSymAt(c) || "code without a symbol"));
			ul.append(li);
		}
		if (n > FN_COPIES_SHOWN) {
			const li = document.createElement("li");
			li.textContent = "… and " + (n - FN_COPIES_SHOWN).toLocaleString("en") + " more";
			ul.append(li);
		}
		inl.append(ul);
	};
	if (!n)
		inl.className += " none";
	fill();
	d.append(inl);
	return d;
}

// Text for a place where facts are missing: loading, or why not.
function fnWhy(r, name) {
	if (r.state === "loading")
		return r.why || "reading the function facts…";
	if (r.state === "none")
		return r.why;
	return r.others ? r.others + " functions are called " + name + " and none of them is known to be here" : "no DW_TAG_subprogram facts for " + name;
}

// The Debugger's "function" row: the facts of every function of the
// inline chain at EIP, innermost first; the last one is the function
// whose code this is, the others were inlined into it.
function renderFnRows(box, phys) {
	const row = document.createElement("div");
	row.id = "cur_fn";
	const b = document.createElement("b");
	b.textContent = "function";
	row.append(b, " ");
	const r = phys >= 0 ? dbgFnFactsAt(phys) : { state: "none", why: "EIP is not mapped" };
	row.dataset.state = r.state;
	if (r.state !== "ok") {
		row.append(r.state === "loading" ? "reading DWARF…" : r.why);
		row.dataset.count = 0;
	} else {
		r.list.forEach((x, k) => {
			const one = document.createElement("div");
			one.className = "fnframe-row" + (k ? " inl" : "");
			const head = document.createElement("div");
			head.className = "fnhead";
			const last = k === r.list.length - 1;
			head.textContent = (x.func || "(no DWARF function)") + (r.list.length > 1 ? last ? ": the function whose code this is" :
				k ? ": inlined here, itself inlined into the next" : ": inlined here (EIP is in its code)" : "");
			one.append(head);
			if (x.f)
				one.append(fnFactsEl(x.f, r.T, "dbg" + k));
			else {
				const w = document.createElement("div");
				w.className = "dim";
				w.textContent = fnWhy(x, x.func);
				one.append(w);
			}
			one.dataset.func = x.func || "";
			row.append(one);
		});
		row.dataset.count = r.list.length;
	}
	box.append(row);
}

// The facts of a function as a detail element (the Coverage and Graph
// tabs): name at its code lo; id for the element.
function fnDetailEl(name, lo, key, id) {
	const d = document.createElement("div");
	if (id)
		d.id = id;
	d.className = "fndetail";
	const r = dbgFnFactsFor(name, lo);
	d.dataset.state = r.state === "ok" && !r.f ? "none" : r.state;
	if (r.state === "ok" && r.f)
		d.append(fnFactsEl(r.f, r.T, key));
	else
		d.textContent = fnWhy(r, name);
	return d;
}

// The Page listing's label line of a code symbol: { proto, title } of its
// DWARF function when the symbol is that function's code, else null.
function fnLabelOf(sym) {
	if (!dbgHas() || !(sym.size > 0))
		return null;
	const r = dbgFnFactsFor(sym.name, sym.paddr);
	if (r.state !== "ok" || !r.f || !dbgFnHolds(r.f.e, sym.paddr))
		return null;
	return fnBare(r.f.e) ? null : { proto: dwFuncProto(r.T, r.f.e), title: fnFactsText(r.f, r.T) };
}
