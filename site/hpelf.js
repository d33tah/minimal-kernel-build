"use strict";
// Hot patching (plan E7), the object side: which functions an edit of one
// C file changes, and the code that replaces them, relocated for the
// running kernel. No DOM: the page (hotpatch.jsx) and test_hotpatch.mjs
// (Node, clang.wasm) use it.
//
// In: the unit compiled twice by the kit's clang (the text the running
// kernel was built from, and the edited text), both with
// -ffunction-sections and without LTO, so every function the compiler
// can place on its own is a section and every reference between
// functions is a relocation; the edit's effect is the difference of the
// two objects, so a function that inlines an edited one shows as changed
// too. hpPlan compares them function by function (the bytes with the
// relocated places blanked, plus what each relocation points at: a
// symbol, a function and offset, a string's text, a variable and
// offset) and section by section for the data, then lays the changed
// and the new functions out from a base address with the read-only data
// they use (string literals, jump tables) and applies the relocations:
// a changed or new function is called at its new copy, every other
// function and every variable at its address in the running kernel
// (lookup()), a symbol the object does not define at the kernel's.
// Calls of mcount/__fentry__ (the kernel replaced them with NOPs at
// boot) become 5-byte NOPs.
//
// It refuses, with the reason, an edit that changes the data (a variable
// added, removed, resized or initialised differently, a global constant
// changed), removes a global function, changes a changed function's
// signature (hpProto: its declaration text in the two sources), or
// makes the new code need what a running kernel set up at boot and a
// patch cannot: static keys (__jump_table), exception table entries
// (__ex_table) and the other sections in HP_REFUSE. Alternatives
// (.altinstructions), BUG/WARN entries and lock prefixes keep their
// compiled (original, always valid) instructions. The page refuses a
// function the kernel inlined elsewhere (its DWARF), one too short for
// the jump, and a CPU stopped inside the bytes the jump replaces.

const SHF_WRITE = 1, SHF_ALLOC = 2, SHF_EXEC = 4, SHF_STRINGS = 0x20;
const SHT_SYMTAB = 2, SHT_RELA = 4, SHT_NOBITS = 8, SHT_REL = 9;
const R_386_32 = 1, R_386_PC32 = 2, R_386_PLT32 = 4;
const STT_FUNC = 2, STT_SECTION = 3, STB_LOCAL = 0;
// Sections of what the kernel does with the code at boot or at a fault:
// a patch can keep the compiled instructions (HP_KEEP), or cannot
// (HP_REFUSE: the new code would run unset jump labels, or fault without
// its fixup). Neither is data the edit changes.
const HP_KEEP = [".altinstructions", ".altinstr_aux", "__bug_table", ".smp_locks", "__mcount_loc", ".parainstructions",
	".discard", "__patchable_function_entries"];
const HP_REFUSE = { "__jump_table": "static keys (__jump_table: the kernel set their jump labels at boot)",
	"__ex_table": "exception table entries (__ex_table: a fault there would have no fixup)",
	".static_call_sites": "static call sites (.static_call_sites)", ".retpoline_sites": "retpoline sites",
	".return_sites": "return thunk sites", ".call_sites": "call depth sites", ".ibt_endbr_seal": "IBT seal entries",
	"__dyndbg": "dynamic debug descriptors (__dyndbg)" };
const hpMeta = (name) => HP_KEEP.some((p) => name === p || name.startsWith(p + ".")) || name in HP_REFUSE ||
	/^\.(debug|note|comment|llvm|eh_frame|rel|orc_)/.test(name);
const hpHex = (b) => Array.from(b, (x) => (x < 16 ? "0" : "") + x.toString(16)).join("");

// An ELF32 i386 relocatable object: { sections, symbols }. Each section:
// { name, type, flags, size, align, data (null for NOBITS), rels }.
function hpElf(bytes) {
	if (bytes.length < 52 || bytes[0] !== 0x7f || bytes[1] !== 0x45 || bytes[2] !== 0x4c || bytes[3] !== 0x46)
		throw new Error(bytes[0] === 0x42 && bytes[1] === 0x43 ? "an LLVM bitcode file, not an ELF object" : "not an ELF file");
	const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
	const u16 = (o) => dv.getUint16(o, true), u32 = (o) => dv.getUint32(o, true);
	if (bytes[4] !== 1 || bytes[5] !== 1 || u16(16) !== 1 || u16(18) !== 3)
		throw new Error("not a 32-bit x86 relocatable object (ELF32, ET_REL, EM_386)");
	const shoff = u32(0x20), shentsize = u16(0x2e), shnum = u16(0x30), shstrndx = u16(0x32);
	const sections = [];
	for (let i = 0; i < shnum; i++) {
		const o = shoff + i * shentsize;
		const type = u32(o + 4), off = u32(o + 16), size = u32(o + 20);
		sections.push({ index: i, nameOff: u32(o), type, flags: u32(o + 8), size, link: u32(o + 24), info: u32(o + 28),
			align: u32(o + 32) || 1, data: type === SHT_NOBITS || !type ? null : bytes.subarray(off, off + size), rels: [] });
	}
	const cstr = (data, at) => {
		let end = at;
		while (end < data.length && data[end])
			end++;
		return new TextDecoder().decode(data.subarray(at, end));
	};
	const shstr = sections[shstrndx].data;
	for (const s of sections)
		s.name = cstr(shstr, s.nameOff);
	const symbols = [];
	const st = sections.find((s) => s.type === SHT_SYMTAB);
	if (st) {
		const strs = sections[st.link].data;
		for (let o = 0; o + 16 <= st.size; o += 16) {
			const b = st.data.byteOffset - bytes.byteOffset + o, info = bytes[b + 12];
			const sym = { name: cstr(strs, u32(b)), value: u32(b + 4), size: u32(b + 8), type: info & 15, bind: info >> 4, shndx: u16(b + 14) };
			if (sym.type === STT_SECTION && sections[sym.shndx])
				sym.name = sections[sym.shndx].name;
			symbols.push(sym);
		}
	}
	for (const s of sections) {
		if (s.type === SHT_RELA)
			throw new Error(s.name + ": RELA relocations (i386 objects have REL)");
		if (s.type !== SHT_REL || !sections[s.info])
			continue;
		const at = s.data.byteOffset - bytes.byteOffset;
		for (let o = 0; o + 8 <= s.size; o += 8)
			sections[s.info].rels.push({ off: u32(at + o), type: u32(at + o + 4) & 0xff, sym: u32(at + o + 4) >>> 8 });
		sections[s.info].rels.sort((a, b) => a.off - b.off);
	}
	return { sections, symbols };
}

const hpPc = (r) => r.type === R_386_PC32 || r.type === R_386_PLT32;
const hpAddend = (sec, r) => new DataView(sec.data.buffer, sec.data.byteOffset).getInt32(r.off, true);
const hpExec = (s) => !!(s.flags & SHF_EXEC);
// Alloc sections that are neither code nor the kernel's tables of it.
const hpRodata = (s) => (s.flags & SHF_ALLOC) && !(s.flags & SHF_WRITE) && s.type !== SHT_NOBITS && !hpExec(s) && !hpMeta(s.name);
const hpData = (s) => (s.flags & SHF_ALLOC) && ((s.flags & SHF_WRITE) || s.type === SHT_NOBITS) && !hpExec(s) && !hpMeta(s.name);

// The functions (STT_FUNC with a size in code) and, per section, the
// named symbols sorted by value.
function hpIndex(o) {
	const funcs = new Map(), bySec = new Map();
	for (const s of o.symbols) {
		const sec = o.sections[s.shndx];
		if (!sec || s.shndx >= 0xff00 || s.type === STT_SECTION || !s.name)
			continue;
		if (s.type === STT_FUNC && s.size && hpExec(sec))
			funcs.set(s.name, { ...s, sec });
		if (!bySec.has(s.shndx))
			bySec.set(s.shndx, []);
		bySec.get(s.shndx).push(s);
	}
	for (const l of bySec.values())
		l.sort((a, b) => a.value - b.value || b.size - a.size);
	o.funcs = funcs;
	o.bySec = bySec;
	return o;
}

// The named symbol of section k at or before off (the one covering it
// first), or null.
function hpSymAt(o, k, off, funcsOnly) {
	let best = null;
	for (const s of o.bySec.get(k) || [])
		if (s.value <= off && (!funcsOnly || s.type === STT_FUNC) && (!best || s.value > best.value || off < s.value + s.size && !(off < best.value + best.size)))
			best = s;
	return best;
}

// What relocation r of section sec points at: { und, add } (undefined
// symbol), { abs }, or { sec: index, off, sym: the named symbol or null
// for a section symbol }. off is S + A in the object; for a call (PC32)
// the target is off + 4.
function hpTarget(o, sec, r) {
	const s = o.symbols[r.sym], A = hpAddend(sec, r);
	if (!s)
		return { bad: "relocation of an unknown symbol" };
	if (s.shndx === 0)
		return { und: s.name, add: A };
	if (s.shndx >= 0xff00)
		return { abs: s.value + A };
	return { sec: s.shndx, off: s.value + A, sym: s.type === STT_SECTION ? null : s };
}

// A relocation as text that two compiles of a unit agree on when the
// target is the same thing (offsets of moved strings or code do not count).
function hpDesc(o, sec, r) {
	const x = hpTarget(o, sec, r), pc = hpPc(r) ? 4 : 0;
	if (x.bad || x.und !== undefined || x.abs !== undefined)
		return r.type + ":" + (x.bad || (x.und !== undefined ? "U " + x.und + (x.add + pc) : "A " + x.abs));
	const t = o.sections[x.sec];
	if (hpExec(t)) {
		const f = hpSymAt(o, x.sec, x.off + pc, true);
		return r.type + ":T " + (f ? f.name + "+" + (x.off + pc - f.value) : t.name + "+" + (x.off + pc));
	}
	if (t.flags & SHF_STRINGS && t.data) {
		let end = x.off;
		while (end < t.data.length && t.data[end])
			end++;
		return r.type + ":S " + hpHex(t.data.subarray(x.off, end));
	}
	const s = x.sym || hpSymAt(o, x.sec, x.off, false);
	return r.type + ":D " + t.name + " " + (s ? s.name + "+" + (x.off - s.value) : "+" + x.off);
}

// The bytes of [lo, hi) of section sec with the relocated places blanked,
// and the relocations there as text: equal for the same code.
function hpPrint(o, sec, lo, hi) {
	const b = sec.data ? sec.data.slice(lo, hi) : new Uint8Array(0);
	const rels = [];
	for (const r of sec.rels)
		if (r.off >= lo && r.off < hi) {
			if (b.length)
				b.fill(0, r.off - lo, r.off - lo + 4);
			rels.push((r.off - lo) + "=" + hpDesc(o, sec, r));
		}
	return (sec.data ? hpHex(b) : "nobits " + (hi - lo)) + "|" + rels.join(",");
}

// A data section as text: its size, named symbols and contents.
function hpDataPrint(o, sec) {
	const syms = (o.bySec.get(sec.index) || []).map((s) => s.name + "@" + s.value + ":" + s.size + (s.bind === STB_LOCAL ? "" : "g"));
	return sec.size + " " + syms.join(" ") + " " + hpPrint(o, sec, 0, sec.size);
}

// The declaration of function name in C text, whitespace folded and
// comments dropped (everything from the end of the previous top-level
// item to its body's "{"), or null when not found (made by a macro).
function hpProto(text, name) {
	const t = text.replace(/\/\*[\s\S]*?\*\//g, " ").replace(/\/\/[^\n]*/g, " ").replace(/^[ \t]*#[^\n]*(\\\n[^\n]*)*/gm, " ");
	const re = new RegExp("\\b" + name.replace(/[$.]/g, "\\$&") + "\\s*\\(", "g");
	let m;
	while ((m = re.exec(t))) {
		// Top level only: the braces before it balance.
		let depth = 0;
		for (let i = 0; i < m.index; i++)
			depth += t[i] === "{" ? 1 : t[i] === "}" ? -1 : 0;
		if (depth)
			continue;
		let i = m.index + m[0].length, p = 1;
		for (; i < t.length && p; i++)
			p += t[i] === "(" ? 1 : t[i] === ")" ? -1 : 0;
		const rest = /^[^;{}]*\{/.exec(t.slice(i));
		if (!rest)
			continue;
		const start = Math.max(t.lastIndexOf(";", m.index), t.lastIndexOf("}", m.index)) + 1;
		return (t.slice(start, i) + rest[0].slice(0, -1)).replace(/\s+/g, " ").trim();
	}
	return null;
}

// The plan of a hot patch, or { ok: false, why }.
//   before, after: the two objects' bytes; opts: { base (VA of the
//   reserved memory), room (its free bytes), lookup(name, sym) -> { va,
//   size } of the running kernel or { why }, src: [before text, after
//   text] (for hpProto) }.
// Result: { ok: true, changed, added, code (bytes at base), funcs: [{
//   name, va, size, old (the kernel's va), sec }], jumps: [{ name, va,
//   bytes }], sections: [{ name, va, size }], notes }.
function hpPlan(before, after, opts) {
	const no = (why) => ({ ok: false, why });
	let A, B;
	try {
		A = hpIndex(hpElf(before));
		B = hpIndex(hpElf(after));
	} catch (e) {
		return no("the compiled unit is not an object a patch can use: " + e.message);
	}
	const notes = [];
	// Data: each data section (by name) the same in both.
	const dataOf = (o) => new Map(o.sections.filter(hpData).map((s) => [s.name, hpDataPrint(o, s)]));
	const da = dataOf(A), db = dataOf(B);
	for (const name of new Set([...da.keys(), ...db.keys()]))
		if (da.get(name) !== db.get(name)) {
			const sa = new Set((A.bySec.get((A.sections.find((s) => s.name === name) || {}).index) || []).map((s) => s.name));
			const sb = new Set((B.bySec.get((B.sections.find((s) => s.name === name) || {}).index) || []).map((s) => s.name));
			const added = [...sb].filter((n) => !sa.has(n)), gone = [...sa].filter((n) => !sb.has(n));
			return no("the edit changes the data in " + name + (added.length ? ": it adds " + added.join(", ") : gone.length ? ": it removes " +
				gone.join(", ") : " (a variable's size, place or initial value)") + "; a patch replaces code only, so rebuild and boot");
		}
	// Global constants: the same.
	const rodataGlobals = (o) => new Map(o.symbols.filter((s) => s.bind !== STB_LOCAL && o.sections[s.shndx] && hpRodata(o.sections[s.shndx]))
		.map((s) => [s.name, s.size + " " + hpPrint(o, o.sections[s.shndx], s.value, s.value + s.size)]));
	const ga = rodataGlobals(A), gb = rodataGlobals(B);
	for (const name of new Set([...ga.keys(), ...gb.keys()]))
		if (ga.get(name) !== gb.get(name))
			return no("the edit changes the global constant " + name + "; a patch replaces code only, so rebuild and boot");
	// Functions.
	const fp = (o, f) => hpPrint(o, f.sec, f.value, f.value + f.size);
	const changed = [], added = [];
	for (const [name, f] of B.funcs) {
		const a = A.funcs.get(name);
		if (!a)
			added.push(name);
		else if (fp(A, a) !== fp(B, f) || a.sec.name !== f.sec.name)
			changed.push(name);
	}
	for (const [name, a] of A.funcs)
		if (!B.funcs.has(name) && a.bind !== STB_LOCAL)
			return no("the edit removes the global function " + name + " (other units call it); rebuild and boot");
	// A function's split-off cold part goes with it.
	for (const name of [...changed])
		for (const [n] of B.funcs)
			if (n.startsWith(name + ".cold") && !changed.includes(n) && !added.includes(n))
				changed.push(n);
	if (!changed.length && !added.length)
		return no("the edit changes no function's code");
	if (!changed.length)
		return no("the edit only adds functions (" + added.join(", ") + "): nothing calls them in the running kernel");
	for (const name of changed) {
		const pa = hpProto(opts.src[0], name.replace(/\.cold.*$/, "")), pb = hpProto(opts.src[1], name.replace(/\.cold.*$/, ""));
		if (pa !== pb)
			return no("the edit changes the signature of " + name + " (" + pa + " -> " + pb + "): its callers in the running kernel would pass the old arguments");
	}
	const P = new Set([...changed, ...added]);
	// The sections placed: those of the functions in P (a section of
	// several functions, such as .init.text, whole), then the read-only
	// data their relocations reach (local strings, tables), transitively.
	const placed = [], isPlaced = new Set();
	const place = (sec) => {
		if (!isPlaced.has(sec.index)) {
			isPlaced.add(sec.index);
			placed.push(sec);
		}
	};
	for (const name of P)
		place(B.funcs.get(name).sec);
	for (let i = 0; i < placed.length; i++)
		for (const r of placed[i].rels) {
			const x = hpTarget(B, placed[i], r);
			const t = x.sec !== undefined ? B.sections[x.sec] : null;
			if (t && hpRodata(t) && !(x.sym && x.sym.bind !== STB_LOCAL))
				place(t);
		}
	// The kernel's tables of the placed code.
	const inPlaced = (k, off) => isPlaced.has(k) && hpExec(B.sections[k]) ? hpSymAt(B, k, off, true) : null;
	for (const s of B.sections) {
		if (!hpMeta(s.name) || /^\.(debug|rel)/.test(s.name))
			continue;
		const refuse = HP_REFUSE[s.name];
		for (const r of s.rels) {
			const x = hpTarget(B, s, r);
			const f = x.sec !== undefined ? inPlaced(x.sec, x.off) : null;
			if (!f)
				continue;
			if (refuse)
				return no(f.name + (P.has(f.name) ? "" : " (in " + B.sections[x.sec].name + " with the edited code)") + " uses " + refuse +
					"; a patch cannot set that up, so rebuild and boot");
			const note = s.name.replace(/\..*$/, "") + " entries keep their compiled instructions";
			if (!notes.includes(note) && s.name !== "__mcount_loc")
				notes.push(note);
		}
	}
	// Layout from base.
	let at = opts.base;
	const secVa = new Map();
	for (const s of placed) {
		at = Math.ceil(at / s.align) * s.align;
		secVa.set(s.index, at);
		at += s.size;
	}
	const size = at - opts.base;
	if (size > opts.room)
		return no("the new code needs " + size + " bytes, the reserved memory has " + opts.room + " left: boot again to empty it");
	const code = new Uint8Array(size);
	for (const s of placed)
		if (s.data)
			code.set(s.data, secVa.get(s.index) - opts.base);
	const cv = new DataView(code.buffer);
	const kfunc = new Map();
	// LTO: the kernel functions the replaced ones called before (their
	// calls checked by hpVerify); a call of another may need LLVM's fastcc.
	const calledBefore = new Set();
	for (const name of changed) {
		const a = A.funcs.get(name);
		for (const r of a ? a.sec.rels : [])
			if (r.off >= a.value && r.off < a.value + a.size && hpPc(r)) {
				const x = hpTarget(A, a.sec, r);
				const g = x.und !== undefined ? { name: x.und } : x.sec !== undefined && hpExec(A.sections[x.sec]) ? hpSymAt(A, x.sec, x.off + 4, true) : null;
				if (g)
					calledBefore.add(g.name);
			}
	}
	const ltoCall = (name) => {
		if (opts.lto && !calledBefore.has(name))
			throw new Error("the new code calls " + name + ", which the replaced code did not: the kernel was linked with LTO, " +
				"which may pass that function's arguments in other registers (LLVM's fastcc)");
	};
	const kernel = (name, sym) => {
		if (!kfunc.has(name))
			kfunc.set(name, opts.lookup(name, sym));
		const k = kfunc.get(name);
		if (!k || k.why)
			throw new Error(k && k.why ? k.why : name + " is not a symbol of the running kernel");
		return k.va;
	};
	try {
		for (const s of placed)
			for (const r of s.rels) {
				const P0 = secVa.get(s.index) + r.off, pc = hpPc(r);
				if (r.type !== R_386_32 && !pc)
					throw new Error(s.name + "+" + r.off + ": relocation type " + r.type + " (only R_386_32, PC32 and PLT32)");
				const x = hpTarget(B, s, r), bias = pc ? 4 : 0;
				let T;
				if (x.und !== undefined) {
					if (pc && (x.und === "mcount" || x.und === "__fentry__") && code[P0 - opts.base - 1] === 0xe8) {
						code.set([0x0f, 0x1f, 0x44, 0x00, 0x00], P0 - opts.base - 1);
						continue;
					}
					if (pc)
						ltoCall(x.und);
					T = kernel(x.und, null) + x.add;
				} else if (x.abs !== undefined) {
					T = x.abs;
				} else {
					const t = B.sections[x.sec];
					if (hpExec(t)) {
						const off = x.off + bias, f = hpSymAt(B, x.sec, off, true);
						// A replaced function at its copy; any other at the
						// kernel's (set up at boot), also when its section was
						// placed whole (the assembler's own calls go to the copy).
						if (isPlaced.has(x.sec) && (!f || P.has(f.name)))
							T = secVa.get(x.sec) + x.off;
						else if (f) {
							if (pc)
								ltoCall(f.name);
							T = kernel(f.name, f) + (off - f.value) - bias;
						}
						else
							throw new Error("a reference into " + t.name + " at " + off + ", outside any function");
					} else if (isPlaced.has(x.sec)) {
						T = secVa.get(x.sec) + x.off;
					} else {
						const v = x.sym || hpSymAt(B, x.sec, x.off, false);
						if (!v)
							throw new Error("a reference into " + t.name + " at " + x.off + " with no variable there");
						T = kernel(v.name, v) + (x.off - v.value);
					}
				}
				cv.setUint32(P0 - opts.base, (pc ? T - P0 : T) >>> 0, true);
			}
	} catch (e) {
		return no(e.message);
	}
	const funcs = [], jumps = [];
	for (const name of [...changed, ...added]) {
		const f = B.funcs.get(name), va = secVa.get(f.sec.index) + f.value;
		let old = null;
		if (changed.includes(name)) {
			const k = opts.lookup(name, A.funcs.get(name));
			if (!k || k.why)
				return no(k && k.why ? k.why : name + " is not a function of the running kernel");
			if (k.size < 5)
				return no(name + " is " + k.size + " bytes in the running kernel: too short for the 5-byte jump");
			old = k.va;
			const why = hpVerify(A, A.funcs.get(name), k, opts);
			if (why)
				return no(why);
			const rel = (va - (old + 5)) >>> 0;
			jumps.push({ name, va: old, bytes: Uint8Array.of(0xe9, rel & 255, rel >>> 8 & 255, rel >>> 16 & 255, rel >>> 24) });
		}
		funcs.push({ name, va, size: f.size, old, sec: f.sec.name });
	}
	return { ok: true, changed, added, code, funcs, jumps, notes,
		sections: placed.map((s) => ({ name: s.name, va: secVa.get(s.index), size: s.size })),
		// The new address of offset off of the after object's section k (placed code), or null.
		at: (k, off) => (secVa.has(k) && hpExec(B.sections[k]) ? secVa.get(k) + off : null) };
}

// Whether the running kernel's code of function f (k: its { va, size })
// is what the before object has: the same bytes but at the relocated
// places and the sites the kernel patches at boot (mcount calls, jump
// labels, alternatives, lock prefixes: what HP_KEEP and HP_REFUSE
// sections point at) and the first 5 bytes when an earlier patch put its
// jump there (opts.jumped). A link-time optimisation (the minified
// Linux's LTO) gives other code: LLVM calls internal functions with its
// fastcc convention (arguments in ECX and EDX), so a function compiled
// here would pass and take its arguments elsewhere. null, or why not.
function hpVerify(A, f, k, opts) {
	const kb = opts.read ? opts.read(k.va, k.size) : null;
	if (!kb)
		return null;
	const lo = f.value, hi = f.value + f.size;
	const ob = f.sec.data.slice(lo, hi);
	const blank = (a, n) => { for (let i = Math.max(a, 0); i < Math.min(a + n, ob.length); i++) ob[i] = kb[i] = 0; };
	for (const r of f.sec.rels)
		if (r.off >= lo && r.off < hi) {
			const x = hpTarget(A, f.sec, r);
			blank(r.off - lo - (x.und === "mcount" || x.und === "__fentry__" ? 1 : 0), 5);
		}
	for (const s of A.sections)
		if (hpMeta(s.name) && !/^\.(debug|rel)/.test(s.name))
			for (const r of s.rels) {
				const x = hpTarget(A, s, r);
				if (x.sec === f.sec.index && x.off >= lo && x.off < hi)
					blank(x.off - lo, 6);
			}
	if (opts.jumped && opts.jumped.has(k.va))
		blank(0, 5);
	if (k.size !== f.size || ob.some((b, i) => b !== kb[i]))
		return "the running kernel's code of " + f.name + " is not what its source compiles to here (" + (k.size !== f.size ? k.size + " bytes, here " + f.size :
			"bytes differ") + (opts.lto ? ": the kernel was linked with LTO, which can change how its functions pass arguments" : "") + "); rebuild and boot";
	return null;
}

// The line rows of the placed code from the object's .debug_line (DWARF 4
// or 5, as clang writes it): { files: [path], rows: [[va, file index or
// -1 (an end), line]] } sorted, or null without a line table. at(k, off):
// the new address of offset off in section k, or null (code not placed:
// its rows are dropped). Paths relative to strip (the build directory).
function hpLines(bytes, at, strip) {
	const o = hpElf(bytes);
	const sec = o.sections.find((s) => s.name === ".debug_line");
	if (!sec || !sec.data)
		return null;
	const named = (n) => (o.sections.find((s) => s.name === n) || {}).data || new Uint8Array(0);
	const lineStr = named(".debug_line_str"), str = named(".debug_str");
	const d = sec.data, dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
	const rel = new Map(sec.rels.map((r) => [r.off, r]));
	const cstr = (b, p) => { let e = p; while (e < b.length && b[e]) e++; return new TextDecoder().decode(b.subarray(p, e)); };
	let p = 0;
	const u8 = () => d[p++], u16 = () => (p += 2, dv.getUint16(p - 2, true)), u32 = () => (p += 4, dv.getUint32(p - 4, true));
	const leb = (signed) => {
		let v = 0, shift = 0, b;
		do { b = d[p++]; v += (b & 0x7f) * 2 ** shift; shift += 7; } while (b & 0x80);
		return signed && b & 0x40 ? v - 2 ** shift : v;
	};
	const form = (f) => {
		switch (f) {
		case 0x08: { const s = cstr(d, p); p += new TextEncoder().encode(s).length + 1; return s; }
		case 0x1f: return cstr(lineStr, u32());
		case 0x0e: return cstr(str, u32());
		case 0x0f: return leb(false);
		case 0x0b: return u8();
		case 0x05: return u16();
		case 0x06: return u32();
		case 0x07: p += 8; return 0;
		case 0x1e: p += 16; return 0;
		case 0x09: p += leb(false); return 0;
		default: throw new Error("DW_FORM " + f + " in a line table header");
		}
	};
	const files = [], fileIx = new Map(), rows = [];
	const fid = (path) => {
		const q = strip && path.startsWith(strip) ? path.slice(strip.length) : path.replace(/^\.\//, "");
		if (!fileIx.has(q)) {
			fileIx.set(q, files.length);
			files.push(q);
		}
		return fileIx.get(q);
	};
	while (p + 4 <= d.length) {
		const len = u32(), end = p + len, ver = u16();
		if (ver >= 5)
			p += 2; // address size, segment selector size
		const hlen = u32(), prog = p + hlen;
		const minInst = u8();
		if (ver >= 4)
			u8(); // maximum operations per instruction
		u8(); // default is_stmt
		const lineBase = (u8() << 24) >> 24, lineRange = u8(), opBase = u8();
		const stdLen = Array.from({ length: opBase - 1 }, () => u8());
		const dirs = [], names = [];
		if (ver >= 5) {
			const entries = (out, isFile) => {
				const fmt = Array.from({ length: u8() }, () => [leb(false), leb(false)]);
				const n = leb(false);
				for (let i = 0; i < n; i++) {
					const e = { path: "", dir: 0 };
					for (const [ct, f] of fmt) {
						const v = form(f);
						if (ct === 1) e.path = v;
						else if (ct === 2) e.dir = v;
					}
					out.push(isFile ? e : e.path);
				}
			};
			entries(dirs, false);
			entries(names, true);
		} else {
			dirs.push("");
			for (let s; (s = form(0x08));)
				dirs.push(s);
			for (let s; (s = form(0x08));) {
				const dir = leb(false);
				leb(false);
				leb(false);
				names.push({ path: s, dir });
			}
			names.unshift(null); // DWARF 4 files count from 1
		}
		const path = (k) => {
			const f = names[k];
			if (!f)
				return "?";
			const dir = ver >= 5 && f.dir === 0 ? "" : dirs[f.dir] || "";
			return f.path.startsWith("/") || !dir ? f.path : dir.replace(/\/$/, "") + "/" + f.path;
		};
		p = prog;
		let addr = null, file = 1, line = 1;
		const row = (e) => {
			if (addr !== null)
				rows.push([addr, e ? -1 : fid(path(file)), line]);
		};
		while (p < end) {
			const op = u8();
			if (op >= opBase) {
				const adj = op - opBase;
				if (addr !== null)
					addr += Math.floor(adj / lineRange) * minInst;
				line += lineBase + adj % lineRange;
				row(false);
			} else if (op === 0) {
				const n = leb(false), at0 = p, sub = u8();
				if (sub === 1) {
					row(true);
					addr = null;
					file = 1;
					line = 1;
				} else if (sub === 2) {
					const r = rel.get(p);
					const x = r ? hpTarget(o, sec, r) : null;
					addr = x && x.sec !== undefined ? at(x.sec, x.off) : null;
				}
				p = at0 + n;
			} else if (op === 1) {
				row(false);
			} else if (op === 2) {
				const a = leb(false);
				if (addr !== null)
					addr += a * minInst;
			} else if (op === 3) {
				line += leb(true);
			} else if (op === 4) {
				file = leb(false);
			} else if (op === 8) {
				if (addr !== null)
					addr += Math.floor((255 - opBase) / lineRange) * minInst;
			} else if (op === 9) {
				if (addr !== null)
					addr += u16();
			} else {
				for (let i = 0; i < stdLen[op - 1]; i++)
					leb(false);
			}
		}
		p = end;
	}
	// At one address an end goes before the next sequence's first row.
	rows.sort((a, b) => a[0] - b[0] || (b[1] < 0) - (a[1] < 0));
	return { files, rows };
}

// lookup(name, sym) for hpPlan over the running kernel's symbols
// (symbols.json's { name, vaddr, paddr, size, type }) and, for the
// variables a trimmed symbols.json leaves out (Debian's statics), its
// types.json T (vars [name, pa, type, size, section, flags, function,
// file]); unit: the compiled source's path. A name LTO promoted
// (name.llvm.N) counts as name; of several, the one of sym's size, then
// (variables) the one T says unit defines.
function hpLookup(symbols, T, unit) {
	const byName = new Map();
	const add = (n, e) => {
		if (!byName.has(n))
			byName.set(n, []);
		byName.get(n).push(e);
	};
	let koff = 0;
	for (const s of symbols) {
		add(s.name.replace(/\.llvm\.\d+$/, ""), { va: s.vaddr, size: s.size, global: s.type === s.type.toUpperCase() });
		koff = s.vaddr - s.paddr;
	}
	return (name, sym) => {
		let c = byName.get(name) || [];
		if (!c.length && T) {
			// clang's name of a function's static: function.variable.
			const dot = name.indexOf("."), fn = dot > 0 ? name.slice(0, dot) : "", v = dot > 0 ? name.slice(dot + 1) : name;
			c = T.vars.filter((x) => (x[0] === name && !x[6]) || (fn && x[0] === v && x[6] === fn))
				.map((x) => ({ va: x[1] + koff, size: x[3], file: x[7] >= 0 ? T.files[x[7]] : null }));
		}
		if (c.length > 1 && sym && sym.size)
			c = c.filter((e) => e.size === sym.size).length ? c.filter((e) => e.size === sym.size) : c;
		if (c.length > 1 && sym)
			c = c.filter((e) => e.global === (sym.bind !== STB_LOCAL)).length ? c.filter((e) => e.global === (sym.bind !== STB_LOCAL)) : c;
		if (c.length > 1 && unit)
			c = c.filter((e) => e.file === unit).length ? c.filter((e) => e.file === unit) : c;
		if (c.length > 1 && new Set(c.map((e) => e.va)).size > 1)
			return { why: c.length + " symbols of the running kernel are called " + name + ": which one is this unit's is not known" };
		return c.length ? c[0] : { why: name + " is not a symbol of the running kernel" };
	};
}

// The kit compile command for a patch's object: no LTO (machine code, not
// bitcode), every function in its own section, output to out. (kbuild's
// -Wp,-MMD stays: without it clang.wasm 22.1.0 hit "unreachable" on the
// Debian kit's init/main.c.)
function hpArgv(argv, out) {
	const a = argv.filter((x) => !/^-f(no-)?lto(=.*)?$/.test(x) && x !== "-fno-function-sections");
	const k = a.indexOf("-o");
	if (k < 0)
		throw new Error("no -o in the compile command");
	a[k + 1] = out;
	a.splice(1, 0, "-ffunction-sections");
	return a;
}

export { hpElf, hpIndex, hpDesc, hpProto, hpPlan, hpVerify, hpLines, hpLookup, hpArgv, hpHex, HP_REFUSE };
