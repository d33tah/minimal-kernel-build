"""Part of a browser suite (test_browser.py)."""

import re
import subprocess
import struct

from tests.dwarf.cfi import dwarfdump_cfi
from tests.browser.common import frames_agree
from tests.browser.cfi import CFI_BASE, CFI_OFFSET


# ---- DWARF forms the shipped kernels lack: a compiled fixture ----------------
# The kernels are GCC or clang DWARF 5; an imported vmlinux can be DWARF
# 2 to 4 (.debug_ranges, .debug_loc, the old line program header, a member
# offset as a DW_OP_plus_uconst block, DW_AT_bit_offset bit fields), with
# GCC's DW_OP_GNU_* operations and SSE registers. One C file (bit fields,
# an enum, a union, a varargs and a noreturn function, a declaration and
# its definition, an inlined call with a block, a static local, long long
# and double values) built five ways, each opened through dwarfworker.js
# like an import and compared byte by byte with llvm-symbolizer, gdb and
# llvm-dwarfdump.
DWF_SOURCE = """typedef int (*binop)(int, int);
enum color { RED = -2, GREEN = 7, BLUE = 300 };
struct flags {
	unsigned a : 3;
	signed b : 5;
	unsigned c : 9;
	char tag;
	unsigned long long big;
};
union word { int i; unsigned char b[4]; float f; };
extern int shared;
int shared = 5;
struct flags gflags = { 1, -3, 100, 'x', 0x123456789aULL };
union word gword;
enum color gcolor = BLUE;
const volatile int cv[2][3];
binop gop;
int (*vfp)(int, ...);
double gdbl = 1.5;
long long gll = -7;

__attribute__((noinline)) int sum(int n, ...)
{
	__builtin_va_list ap;
	int s = 0;

	__builtin_va_start(ap, n);
	while (n--)
		s += __builtin_va_arg(ap, int);
	__builtin_va_end(ap);
	return s;
}

__attribute__((noreturn, noinline)) void stop(void)
{
	for (;;)
		__asm__ volatile("hlt");
}

static inline __attribute__((always_inline)) int twice(int v)
{
	int r = v * 2;

	{
		int t = r + shared;

		gword.i = t;
	}
	return r;
}

__attribute__((noinline)) long long mix(long long a, int k, double d)
{
	static int calls;
	long long acc = a;

	calls++;
	for (int i = 0; i < k; i++) {
		acc += twice(i) * (long long)d;
		if (acc > 1000000)
			stop();
	}
	gll = acc;
	return acc + calls;
}

__attribute__((noinline)) int use(struct flags *f, int x)
{
	int y = f->a + f->b + x;

	gflags.c = y;
	return twice(y) + sum(3, x, y, f->c);
}

int entry(int n)
{
	gop = (binop)0;
	return use(&gflags, n) + (int)mix(gll, n, gdbl);
}
"""
# C++: a method defined outside its class is a DIE with DW_AT_specification
# (its name, the varargs) pointing at the declaration in the class, a
# static data member's definition too.
DWF_CXX = """namespace ns {
struct Counter {
	static int total;
	int v;
	int add(int d, ...);
	__attribute__((noinline)) int get(int) const;
};
int Counter::total = 1;
}

__attribute__((noinline)) int ns::Counter::add(int d, ...)
{
	v += d;
	total += v;
	return v;
}

__attribute__((noinline)) int ns::Counter::get(int k) const
{
	return v * k + total;
}

static inline int helper(ns::Counter &c, int x)
{
	return c.get(x) + 1;
}

ns::Counter gc;

extern "C" int entry(int n)
{
	gc.add(n, 1, 2);
	return helper(gc, n);
}
"""
# name: (compiler, flags, DWARF version the page must report, source)
DWF_BUILDS = {
    "gcc-dwarf2": ("gcc", ["-gdwarf-2", "-mregparm=3"], 2, "m.c"),
    "gcc-dwarf3": ("gcc", ["-gdwarf-3", "-O0"], 3, "m.c"),
    "gcc-dwarf4": ("gcc", ["-gdwarf-4", "-mregparm=3", "-msse2", "-mfpmath=sse"], 4, "m.c"),
    "gcc-dwarf5": ("gcc", ["-gdwarf-5", "-mregparm=3"], 5, "m.c"),
    # Location lists with DW_LLE_GNU_view_pair entries (neither gdb nor
    # llvm-dwarfdump reads them): the same code, so the page must say what
    # it says for gcc-dwarf5.
    "gcc-dwarf5-views": ("gcc", ["-gdwarf-5", "-gvariable-location-views=incompat5", "-mregparm=3"], 5, "m.c"),
    "clang-dwarf4": ("clang", ["-gdwarf-4"], 4, "m.c"),
    "clang-dwarf5": ("clang", ["-gdwarf-5", "-mregparm=3"], 5, "m.c"),
    "g++-dwarf4": ("g++", ["-gdwarf-4", "-fno-exceptions", "-fno-rtti"], 4, "c.cc"),
}
DWF_FUNCS = ("sum", "stop", "mix", "use", "entry")
DWF_VARS = ("shared", "gflags", "gword", "gcolor", "cv", "gop", "vfp", "gdbl", "gll")

DWF_JS = """async ([b64, offset, lo, hi, src, probe]) => {
    const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    const w = new Worker('dwarfworker.js' + location.search, { type: 'module' });
    const ask = (m, t) => new Promise((res) => { w.onmessage = (e) => { if (e.data.type === t || e.data.type === 'error') res(e.data); };
        w.postMessage(m); });
    const o = await ask({ type: 'open', file: new File([bytes], 'fixture.elf'), offset }, 'open');
    if (!o.ok) { w.terminate(); return { open: o.why }; }
    const out = { open: 'ok', versions: o.versions, units: o.units };
    // Asked before the types: the Worker's error.
    out.early = await ask({ type: 'locals', i: 0 }, 'locals');
    const tables = [];
    for (let i = 0; i < o.units; i++) {
        const u = await ask({ type: 'unit', i }, 'unit');
        if (u.type === 'error') { w.terminate(); return { ...out, error: 'unit: ' + u.message }; }
        tables.push(u.table);
    }
    const at = (pa) => { for (const t of tables) { const f = MKB.dwarfFrames(t, pa); if (f) return f; } return null; };
    out.frames = [];
    for (let pa = lo; pa < hi; pa++)
        out.frames.push(at(pa));
    const T = (await ask({ type: 'types' }, 'types')).types;
    const groups = [];
    for (let i = 0; i < o.units; i++) {
        const l = await ask({ type: 'locals', i }, 'locals');
        if (l.type === 'error') { w.terminate(); return { ...out, error: 'locals: ' + l.message }; }
        T.types.push(...l.added);
        groups.push(...l.groups);
    }
    const c = await ask({ type: 'cfi' }, 'cfi');
    const lines = await ask({ type: 'lines', q: src }, 'lines');
    out.nofile = (await ask({ type: 'lines', q: 'nosuch.c' }, 'lines')).why;
    w.terminate();
    out.code = lines.code ? [...lines.code].map(([line, e]) => [line, e.rows.map((r) => [r[0], r[1]]), e.calls.map((x) => [x[0], x[2]])]) :
        lines.why || lines.message;
    out.cfi = c.why || c.source;
    out.rows = [];
    for (let pa = lo; pa < hi && c.table; pa++) {
        const r = MKB.cfiRow(c.table, pa);
        out.rows.push(r === null ? null : r.why ? 'why: ' + r.why : MKB.cfiRowText(r));
    }
    out.funcs = T.funcs.fns.map((e) => ({ name: e[0], at: T.funcs.files[e[1]] + ':' + e[2], type: MKB.dwFuncProto(T, e, false),
        noret: !!(e[3] & 4), varargs: !!(e[3] & 2) }));
    out.vars = T.vars.map((v) => ({ name: v[0], va: v[1] + offset, size: v[3], at: MKB.dwVarDeclText(T, v), type: MKB.dwTypeName(T, v[2]),
        where: [...Array(v[3]).keys()].map((k) => { const x = MKB.dwWhere(T, v[1] + k); return x ? x.path + (x.note ? ' ' + x.note : '') : null; }) }));
    // The static local by its function's name (a watch's mix::calls).
    const pick = (q) => { const p = MKB.dwVarsPick(T, T.vars.filter((v) => v[0] === 'calls'), q, 'calls'); return p.v ? [p.v[0], p.v[1] + offset, p.v[6]] : p.why; };
    out.picks = [pick('mix'), pick('use')];
    out.flags = T.types.filter((t) => t[0] === MKB.DWT.struct && t[1] === 'flags' && t[3]).map((t) => t[3])[0] || null;
    out.enums = Object.fromEntries(T.types.filter((t) => t[0] === MKB.DWT.enum).map((t) => [t[1], t[3]]));
    // The variables at each address: name and kind of location there, as
    // gen-dwarf.js --locals classifies them for test_dwarf.py's gdb_scopes.
    const pieces = (e) => { for (let p = 0; p >= 0 && p < e.length; p = MKB.dwOpEnd(e, p)) if (e[p] === 0x93 || e[p] === 0x9d) return true; return false; };
    const cls = (loc, pa) => {
        const l = MKB.dwLocAt(loc, pa);
        if (l.why) return 'out';
        if (l.c !== undefined || l.cb) return 'const';
        const e = l.expr;
        if (pieces(e)) return 'pieces';
        if (e.length === 1 && e[0] >= 0x50 && e[0] <= 0x6f) return 'reg:' + (MKB.CFI_REGS[e[0] - 0x50] || e[0] - 0x50);
        return e.length ? 'expr' : 'out';
    };
    out.locals = [];
    for (let pa = lo; pa < hi; pa++) {
        const fr = MKB.dwarfLocalFrames(MKB.dwarfScopesAt(groups, pa));
        out.locals.push(fr.length ? fr.map((f) => ({ func: f.func, vars: [...f.params, ...f.locals].map((v) => [v[0], cls(v[3], pa)]) })) : null);
    }
    // Every location expression as text, per variable: [name, lo, hi, text].
    const hex = (h) => Uint8Array.from(h.match(/../g) || [], (x) => parseInt(x, 16));
    out.exprs = [];
    for (const g of groups)
        for (const s of g)
            for (const v of s[5]) {
                if (typeof v[3] === 'string')
                    out.exprs.push([v[0], 0, 0, MKB.dwExprText(hex(v[3]))]);
                else if (Array.isArray(v[3]))
                    for (const [a, n, e] of v[3]) // a default location (DW_LLE_default_location): -1, -1
                        out.exprs.push([v[0], n ? a + offset : -1, n ? a + n + offset : -1, MKB.dwExprText(hex(e))]);
            }
    // probe [pa, registers by DWARF number, { address: byte }]: each
    // variable's value there (dwLocation) as bytes, null where not known,
    // or why it has none.
    if (probe) {
        const [pa, regs, mem] = probe;
        const ctx = { reg: (r) => regs[r], read: (a, n) => { let v = 0; for (let k = n - 1; k >= 0; k--) { if (mem[a + k] === undefined) return undefined; v = v * 256 + mem[a + k]; } return v; },
                      readBytes: (a, n) => { const b = []; for (let k = 0; k < n; k++) { if (mem[a + k] === undefined) return null; b.push(mem[a + k]); } return Uint8Array.from(b); } };
        out.values = {};
        for (const f of MKB.dwarfLocalFrames(MKB.dwarfScopesAt(groups, pa)))
            for (const v of [...f.params, ...f.locals]) {
                const l = MKB.dwLocAt(v[3], pa);
                const r = l.why ? l : MKB.dwLocation(l.expr, ctx, MKB.dwTypeSize(T, v[1]));
                out.values[v[0]] = r.why ? r.why : r.mem !== undefined ? 'at ' + r.mem :
                    r.whys.length && !r.known.some((x) => x) ? r.whys.join('; ') : [...r.bytes].map((x, k) => (r.known[k] ? x : null));
            }
    }
    return out;
}"""


def dwf_build(tmp, name):
    """DWF_SOURCE built as DWF_BUILDS[name] for i386, linked at CFI_BASE (-O2 unless the flags say otherwise)."""
    cc, flags, _, src = DWF_BUILDS[name]
    (tmp / src).write_text(DWF_CXX if src == "c.cc" else DWF_SOURCE)
    obj, elf = tmp / f"{name}.o", tmp / f"{name}.elf"
    common = ["-m32", "-O2", "-ffreestanding", "-fno-pic", *flags]
    ld = ["-fuse-ld=lld"] if cc == "clang" else []
    try:
        subprocess.run([cc, *common, "-c", src, "-o", str(obj)], cwd=tmp, check=True, capture_output=True, text=True)
        subprocess.run([cc, "-m32", *ld, "-nostdlib", "-no-pie", "-Wl,-e,entry", f"-Wl,-Ttext=0x{CFI_BASE:x}", str(obj), "-o", str(elf)],
                       cwd=tmp, check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as e:
        return None, f"{cc}: {getattr(e, 'stderr', '') or e}"[:300]
    return elf, None


def dwf_dwarfdump_exprs(elf):
    """llvm-dwarfdump's location of every named (or abstract-origin named)
    parameter and variable inside a function: {(name, lo, hi, text)}, lo =
    hi = 0 for a single expression, text None where it cannot print it;
    DW_OP_GNU_entry_value spelled as DWARF 5's."""
    dump = subprocess.run(["llvm-dwarfdump", "--debug-info", str(elf)], capture_output=True, text=True).stdout
    out = {}
    for block in re.split(r"\n(?=0x[0-9a-f]+:)", dump):
        if not re.match(r"0x[0-9a-f]+:\s+DW_TAG_(formal_parameter|variable)", block):
            continue
        m = re.search(r'DW_AT_(?:name|abstract_origin)\s+\((?:0x[0-9a-f]+ )?"([^"]+)"\)', block)
        loc = re.search(r"DW_AT_location\s+\((.*?)\)\n(?=\s+DW_AT|\s*$)", block + "\n", re.S)
        if not m or not loc:
            continue
        text = loc.group(1).replace("DW_OP_GNU_entry_value", "DW_OP_entry_value")
        entries = re.findall(r"\[(0x[0-9a-f]+), (0x[0-9a-f]+)\): (.*)", text)
        default = re.search(r"<default>: (.*)", text)
        if default:
            out[(m.group(1), -1, -1)] = default.group(1).strip().rstrip(")")
        if entries:
            for a, b, e in entries:
                e = e.strip()
                if e.count("(") < e.count(")"):
                    e = e[:-1]
                if int(b, 16) > int(a, 16):
                    out[(m.group(1), int(a, 16), int(b, 16))] = None if "error" in e else e
        elif "\n" not in text:
            out[(m.group(1), 0, 0)] = None if "error" in text else text.strip()
    return out


def dwf_gdb(elf):
    """gdb's answers: ptype of each function, whatis of each variable,
    ptype/o of struct flags, the enumerators' values, info functions /
    variables (file:line)."""
    ex = []
    for f in DWF_FUNCS:
        ex += ["-ex", f"echo @@func {f}\\n", "-ex", f"ptype {f}"]
    for v in DWF_VARS:
        ex += ["-ex", f"echo @@var {v}\\n", "-ex", f"whatis {v}", "-ex", f"info line {v}"]
    ex += ["-ex", "echo @@flags\\n", "-ex", "ptype/o struct flags", "-ex", "echo @@enum\\n",
           "-ex", "print/d (int)RED", "-ex", "print/d (int)GREEN", "-ex", "print/d (int)BLUE",
           "-ex", "echo @@info\\n", "-ex", "info functions", "-ex", "info variables"]
    out = subprocess.run(["gdb", "-nx", "-batch", "-ex", "set width 0", *ex, str(elf)], capture_output=True, text=True).stdout
    parts = {}
    for p in out.split("@@")[1:]:
        head, _, body = p.partition("\n")
        parts[head.strip()] = body
    return parts


def dwf_sections(b):
    """{name: (file offset, size)} of an ELF32 file's sections."""
    shoff, = struct.unpack_from("<I", b, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", b, 0x2e)
    hdr = [struct.unpack_from("<IIIIII", b, shoff + k * shentsize) for k in range(shnum)]
    strs = hdr[shstrndx][4]
    return {b[strs + h[0]:b.index(0, strs + h[0])].decode(): (h[4], h[5]) for h in hdr}


def dwf_uleb(b, p):
    r = s = 0
    while True:
        x = b[p]
        p += 1
        r |= (x & 0x7f) << s
        s += 7
        if not x & 0x80:
            return r, p


def dwf_broken(tmp, good):
    """Broken copies of the clang -gdwarf-5 build and other files that are
    not an i386 DWARF vmlinux: [(name, bytes, the errors the import must give)]."""
    b = good.read_bytes()
    sec = dwf_sections(b)
    out = [("not an ELF", b"MZ" + bytes(60), ["open: not an ELF file"])]
    info = sec[".debug_info"][0]

    def patched(*edits):
        x = bytearray(b)
        for off, data in edits:
            x[off:off + len(data)] = data
        return bytes(x)
    out.append(("version 6", patched((info + 4, b"\x06\x00")), ["open: DWARF version 6 at .debug_info+0"]))
    out.append(("64-bit DWARF", patched((info, b"\xff\xff\xff\xff")), ["open: 64-bit DWARF is not supported"]))
    # The abbreviations: the compile unit's first attribute gets form 0x7f;
    # abbreviation 2 (the first DIE after the unit's) is renumbered 0x7e.
    ab = sec[".debug_abbrev"][0]
    code, p = dwf_uleb(b, ab)
    _, p = dwf_uleb(b, p)
    _, form_at = dwf_uleb(b, p + 1)
    out.append(("unknown form", patched((form_at, b"\x7f")), ["open: unknown DWARF form 0x7f"]))
    p = ab
    while True:
        code, q = dwf_uleb(b, p)
        if code == 2:
            break
        _, q = dwf_uleb(b, q)
        q += 1
        while True:
            at, q = dwf_uleb(b, q)
            form, q = dwf_uleb(b, q)
            if form == 0x21:
                _, q = dwf_uleb(b, q)
            if not at and not form:
                break
        p = q
    m = re.search(r"^0x([0-9a-f]+):\s+DW_TAG_\w+ \[2\]",
                  subprocess.run(["llvm-dwarfdump", "-v", "--debug-info", str(good)], capture_output=True, text=True).stdout, re.M)
    out.append(("abbreviation", patched((p, b"\x7e")), [f"{w}: bad abbreviation code at .debug_info+{int(m.group(1), 16):x}" for w in ("unit 0", "types")]))
    # Every list of .debug_rnglists / .debug_loclists starts with kind 0x0f.
    for name, what in ((".debug_rnglists", "DW_RLE"), (".debug_loclists", "DW_LLE")):
        o, _ = sec[name]
        n, = struct.unpack_from("<I", b, o + 8)
        offs = [struct.unpack_from("<I", b, o + 12 + 4 * k)[0] for k in range(n)]
        out.append((what, patched(*[(o + 12 + x, b"\x0f") for x in offs]),
                    ["unit 0: unknown DW_RLE 0xf", "types: unknown DW_RLE 0xf"] if what == "DW_RLE" else ["types: unknown DW_LLE 0xf"]))
    # A 64-bit object, and the fixture without -g.
    for name, flags, want in (("x86-64", ["-m64"], ["open: not a 32-bit little-endian ELF (only i386 kernels boot here)"]),
                              ("no -g", ["-m32", "-g0"], ["open: this vmlinux has no DWARF (.debug_info): build it with CONFIG_DEBUG_INFO for file:line"])):
        try:
            subprocess.run(["clang", *flags, "-O2", "-ffreestanding", "-c", "m.c", "-o", "x.o"], cwd=tmp, check=True, capture_output=True)
            out.append((name, (tmp / "x.o").read_bytes(), want))
        except (OSError, subprocess.CalledProcessError) as e:
            out.append((name, None, f"clang: {e}"))
    return out


def dwf_locals_checks(name, elf, got, vas, gdb_scopes):
    """The parameters and locals at every byte (gdb's info scope, by kind
    of location) and the text of every location expression (llvm-dwarfdump)."""
    problems = []
    # Locals at every byte: gdb's info scope, by kind of location.
    scopes = gdb_scopes(str(elf), vas)
    bad = []
    for va, l in zip(vas, got["locals"]):
        w = scopes.get(va)
        if w is None or l is None:
            continue
        if sorted(map(tuple, l[0]["vars"])) != sorted(w):
            bad.append((hex(va), l, w))
    r_checked = sum(1 for va, l in zip(vas, got["locals"]) if l and scopes.get(va) is not None)
    if bad or r_checked < len(vas) // 4:
        problems.append(f"{name}: locals differ from gdb's info scope at {len(bad)}/{r_checked} addresses: {bad[:2]}")
    # Every location expression: llvm-dwarfdump's text.
    # (The page turns clang's DW_OP_addrx into DW_OP_addr with the address.)
    mine = {(n, a, b): t for n, a, b, t in got["exprs"]}
    theirs = {k: v for k, v in dwf_dwarfdump_exprs(elf).items() if k[0] not in DWF_VARS + ("gc", "total") and not (v or "").startswith("DW_OP_addrx")}
    # dwExprText abbreviates the rarer operations' operands ("…"): there
    # only the operations must be llvm-dwarfdump's.
    same = lambda m, t: m == t or (m is not None and "…" in m and re.findall(r"DW_OP_\w+", m) == re.findall(r"DW_OP_\w+", t))
    differ = [(k, mine.get(k), theirs.get(k)) for k in sorted(set(mine) | set(theirs), key=str)
              if theirs.get(k) is not None and not same(mine.get(k), theirs[k])]
    if differ or len(theirs) < 5:
        problems.append(f"{name}: location expressions differ from llvm-dwarfdump's ({len(theirs)}): {differ[:4]}")
    return problems


def dwf_cxx_checks(name, elf, got, nm):
    """The C++ fixture: the methods' and variables' names, file:line and
    addresses through DW_AT_specification (gdb's info functions /
    variables, llvm-nm), add's varargs."""
    problems = []
    gdb = subprocess.run(["gdb", "-nx", "-batch", "-ex", "info functions", "-ex", "info variables", str(elf)],
                         capture_output=True, text=True).stdout
    fn = {f["name"]: f for f in got["funcs"]}
    for f, decl in (("add", "ns::Counter::add(int, ...)"), ("get", "ns::Counter::get(int) const"), ("entry", "entry(int)")):
        m = re.search(r"^(\d+):\t[^\n]*" + re.escape(decl) + ";$", gdb, re.M)
        if not m or not fn.get(f) or fn[f]["at"] != "c.cc:" + m.group(1):
            problems.append(f"{name}: {f} at {fn.get(f)}, gdb's info functions {m and m.group(0)!r}")
    if not fn.get("add", {}).get("varargs") or fn.get("get", {}).get("varargs"):
        problems.append(f"{name}: varargs: add {fn.get('add')}, get {fn.get('get')}")
    vs = {v["name"]: v for v in got["vars"]}
    for v, decl, sym in (("total", "int ns::Counter::total", "_ZN2ns7Counter5totalE"), ("gc", "ns::Counter gc", "gc")):
        m = re.search(r"^(\d+):\t" + re.escape(decl) + ";$", gdb, re.M)
        if not m or not vs.get(v) or vs[v]["at"] != "c.cc:" + m.group(1) or vs[v]["va"] != nm.get(sym):
            problems.append(f"{name}: {v}: {vs.get(v)}, gdb {m and m.group(0)!r}, nm {nm.get(sym)}")
    return problems


def dwf_checks(name, elf, got, lo, hi, nm, symbolize, gdb_scopes, seen):
    problems = []
    want_ver = DWF_BUILDS[name][2]
    if got["versions"] != [want_ver]:
        problems.append(f"{name}: DWARF versions {got['versions']}, want [{want_ver}]")
    if "not read yet" not in str(got["early"]):
        problems.append(f"{name}: locals before types answered {got['early']}")
    if got["nofile"] != "no source file nosuch.c with code in this kernel's line table":
        problems.append(f"{name}: lines of an unknown file: {got['nofile']!r}")
    # Frames at every byte: llvm-symbolizer --inlines.
    vas = list(range(lo, hi))
    want = symbolize(str(elf), vas, [str(elf.parent) + "/"])
    bad = [(hex(va), g, w) for va, g, w in zip(vas, got["frames"], want) if not frames_agree(g or [], w)]
    if bad:
        problems.append(f"{name}: frames differ from llvm-symbolizer at {len(bad)}/{len(vas)} addresses: {bad[:2]}")
    # The code of each line of the source: its rows' first and last bytes are that line.
    sym = dict(zip(vas, want))
    if not isinstance(got["code"], list) or not got["code"]:
        problems.append(f"{name}: no code for {DWF_BUILDS[name][3]}: {got['code']!r}")
    else:
        for line, rows, calls in got["code"]:
            for a, b in rows:
                for va in (a + CFI_OFFSET, b - 1 + CFI_OFFSET):
                    w = sym.get(va)
                    if w and w[0][2] != line:
                        problems.append(f"{name}: {DWF_BUILDS[name][3]}:{line} lists {va:#x}, llvm-symbolizer says {w[0]}")
    # CFI rows.
    fdes = dwarfdump_cfi(str(elf))
    rows = {}
    for va in vas:
        f = next((f for f in fdes if f[0] <= va < f[1]), None)
        prev = [t for a, t in (f[2] if f else []) if a <= va]
        rows[va] = prev[-1] if prev else None
    bad = [(hex(va), rows[va], g) for va, g in zip(vas, got["rows"]) if rows[va] != g]
    if bad or not fdes:
        problems.append(f"{name}: CFI rows ({got['cfi']}) differ from llvm-dwarfdump: {bad[:3]} ({len(fdes)} FDEs)")
    if name.endswith("-views"):
        other, _, olo, ohi = seen.get(name[:-len("-views")], (None, None, 0, 0))
        text = lambda e: subprocess.run(["llvm-objcopy", "-O", "binary", "--only-section=.text", str(e), "/dev/stdout"], capture_output=True).stdout
        if not other or (olo, ohi) != (lo, hi) or text(elf) != text(elf.parent / (name[:-len("-views")] + ".elf")):
            problems.append(f"{name}: not the same code as {name[:-len('-views')]}")
        elif got["locals"] != other["locals"] or sorted(map(tuple, got["exprs"])) != sorted(map(tuple, other["exprs"])):
            problems.append(f"{name}: parameters and locals or their locations differ from {name[:-len('-views')]}'s")
        return problems
    if DWF_BUILDS[name][3] == "c.cc":
        return problems + dwf_cxx_checks(name, elf, got, nm) + dwf_locals_checks(name, elf, got, vas, gdb_scopes)
    # Functions and variables against gdb.
    g = dwf_gdb(elf)
    fn = {f["name"]: f for f in got["funcs"]}
    for f in DWF_FUNCS:
        want_t = (g.get(f"func {f}", "").strip().partition("type = ")[2])
        have = fn.get(f)
        if not have or have["type"] != want_t:
            problems.append(f"{name}: {f}'s type {have and have['type']!r}, gdb's ptype {want_t!r}")
        m = re.search(rf"^(\d+):\t[^\n]*\b{f}\(", g.get("info", ""), re.M)
        if have and m and have["at"] != f"m.c:{m.group(1)}":
            problems.append(f"{name}: {f} at {have['at']}, gdb's info functions m.c:{m.group(1)}")
    # DW_AT_noreturn is DWARF 5's (an extension before).
    noret = "DW_AT_noreturn" in subprocess.run(["llvm-dwarfdump", "--debug-info", str(elf)], capture_output=True, text=True).stdout
    if fn.get("stop", {}).get("noret") != noret or fn.get("sum", {}).get("noret") or not fn.get("sum", {}).get("varargs"):
        problems.append(f"{name}: noreturn/varargs flags {fn.get('stop')} {fn.get('sum')}")
    vs = {v["name"]: v for v in got["vars"]}
    for v in DWF_VARS:
        want_t = g.get(f"var {v}", "").strip().partition("type = ")[2].split("\n")[0]
        have = vs.get(v)
        if not have or have["type"] != want_t or have["va"] != nm.get(v):
            problems.append(f"{name}: {v}: {have and (have['type'], hex(have['va']))}, gdb {want_t!r}, nm {nm.get(v) and hex(nm[v])}")
        m = re.search(rf"^(\d+):\t[^\n]*\b{v}\b[^\n]*;$", g.get("info", ""), re.M)
        if have and m and have["at"] != f"m.c:{m.group(1)}":
            problems.append(f"{name}: {v} at {have['at']!r}, gdb's info variables m.c:{m.group(1)}")
    # mix's static local by its function's name, at its ELF symbol (GCC calls.0, clang mix.calls).
    sym = next((a for k, a in nm.items() if re.fullmatch(r"calls(\.\d+)?|mix\.calls", k)), None)
    if got["picks"] != [["calls", sym, "mix"], "no variable use::calls in this kernel's DWARF"]:
        problems.append(f"{name}: mix::calls and use::calls: {got['picks']}, the symbol at {sym}")
    # struct flags as gdb's ptype/o lays it out: [name, byte, bits, bit in byte].
    lay = []
    for m in re.finditer(r"/\*\s+(\d+)(?:\s*:\s*(\d+))?\s*\|\s*\d+\s*\*/\s+[^;]*?(\w+)(?:\s*:\s*(\d+))?;", g.get("flags", "")):
        off, bit, mem, bits = int(m.group(1)), int(m.group(2) or 0), m.group(3), m.group(4)
        lay.append([mem, off, int(bits), bit] if bits else [mem, off])
    have = [[m[0], m[1], m[3], m[4]] if len(m) > 3 else [m[0], m[1]] for m in got["flags"] or []]
    # gdb counts a bit field's offset from the start of its storage
    # unit's byte; the page from the byte holding its lowest bit.
    norm = lambda l: [[x[0], x[1] * 8 + x[3], x[2]] if len(x) > 2 else x for x in l]
    if norm(lay) != norm(have) or len(lay) != 5:
        problems.append(f"{name}: struct flags {have}, gdb's ptype/o {lay}")
    enums = re.findall(r"= (-?\d+)", g.get("enum", ""))
    if [e[1] if e[1] < 2 ** 31 else e[1] - 2 ** 32 for e in got["enums"].get("color", [])] != [int(x) for x in enums]:
        problems.append(f"{name}: enum color {got['enums']}, gdb {enums}")
    # The member at every byte of gflags (dwWhere) is one gdb's ptype/o
    # puts there (bit fields share bytes), else padding.
    gf = vs.get("gflags")
    sizes = {"tag": 1, "big": 8}
    for k, path in enumerate(gf["where"] if gf else []):
        there = [x[0] for x in lay if (x[1] + x[3] // 8 <= k <= x[1] + (x[3] + x[2] - 1) // 8 if len(x) > 2 else
                                       x[1] <= k < x[1] + sizes.get(x[0], 0))]
        mem = re.match(r"gflags\.(\w+)", path or "")
        if (mem.group(1) not in there if mem else there or path != "gflags padding"):
            problems.append(f"{name}: gflags byte {k}: the page says {path!r}, gdb's ptype/o has {there or 'padding'}")
            break
    problems += dwf_locals_checks(name, elf, got, vas, gdb_scopes)
    return problems
