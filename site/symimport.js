"use strict";

// Import symbols for the current kernel: a System.map (`nm -n` text) or an
// unstripped vmlinux (ELF32). The result has the shape of symbols.json
// (site/gen-symbols.py): { name, sections: [{name, class, vaddr, paddr,
// size}], symbols: [{name, vaddr, paddr, size, type, section}] } plus
// { source, offset, offsetHow }.

const PAGE_OFFSET_I386 = 0xC0000000;
const MAP_KEY = "mkb-map-";

// ---- ELF ------------------------------------------------------------------

function elfSectionClass(name, flags, type) {
	if (name === ".brk")
		return "brk";
	if (type === 8) // SHT_NOBITS
		return "bss";
	if (flags & 0x4) // SHF_EXECINSTR
		return "text";
	if (name.includes(".rodata") || !(flags & 0x1)) // not SHF_WRITE
		return "rodata";
	if (name.includes(".data"))
		return "data";
	return "other";
}

// The parts of an ELF file parseElf reads (headers, section names,
// .symtab, .strtab), fetched as File slices: a distro vmlinux is ~300 MB
// and only a few MB of it are symbols. Returns read(off, len) over them.
async function elfSlices(file) {
	const chunks = [];
	const get = async (off, len) => {
		if (off + len > file.size)
			throw new Error("truncated ELF file");
		const b = new Uint8Array(await file.slice(off, off + len).arrayBuffer());
		chunks.push([off, b]);
		return b;
	};
	const read = (off, len) => {
		for (const [o, b] of chunks)
			if (off >= o && off + len <= o + b.length)
				return b.subarray(off - o, off - o + len);
		throw new Error("ELF: bytes " + off + "+" + len + " were not read");
	};
	const h = await get(0, 52);
	const dv = new DataView(h.buffer);
	if (h.length < 52 || dv.getUint32(0) !== 0x7f454c46)
		throw new Error("not an ELF file");
	if (h[4] !== 1 || h[5] !== 1)
		throw new Error("not a 32-bit little-endian ELF (only i386 kernels boot here)");
	const u16 = (o) => dv.getUint16(o, true), u32 = (o) => dv.getUint32(o, true);
	const phoff = u32(0x1c), shoff = u32(0x20);
	const phentsize = u16(0x2a), phnum = u16(0x2c), shentsize = u16(0x2e), shnum = u16(0x30), shstrndx = u16(0x32);
	if (!shoff || shoff + shnum * shentsize > file.size)
		throw new Error("ELF without section headers");
	await get(phoff, phentsize * phnum);
	const sh = await get(shoff, shentsize * shnum);
	const sdv = new DataView(sh.buffer);
	const sec = (i) => ({ type: sdv.getUint32(i * shentsize + 4, true), offset: sdv.getUint32(i * shentsize + 16, true),
		size: sdv.getUint32(i * shentsize + 20, true), link: sdv.getUint32(i * shentsize + 24, true) });
	if (shstrndx < shnum)
		await get(sec(shstrndx).offset, sec(shstrndx).size);
	for (let i = 0; i < shnum; i++)
		if (sec(i).type === 2) { // SHT_SYMTAB and its string table
			await get(sec(i).offset, sec(i).size);
			if (sec(i).link < shnum)
				await get(sec(sec(i).link).offset, sec(sec(i).link).size);
		}
	return read;
}

// read(off, len): bytes of the file (elfSlices).
function parseElf(read, name) {
	const h = read(0, 52);
	const dvOf = (b) => new DataView(b.buffer, b.byteOffset, b.byteLength);
	const hv = dvOf(h);
	const phoff = hv.getUint32(0x1c, true), shoff = hv.getUint32(0x20, true);
	const phentsize = hv.getUint16(0x2a, true), phnum = hv.getUint16(0x2c, true), shentsize = hv.getUint16(0x2e, true);
	const shnum = hv.getUint16(0x30, true), shstrndx = hv.getUint16(0x32, true);
	const ph = dvOf(read(phoff, phentsize * phnum));
	const loads = [];
	for (let i = 0; i < phnum; i++) {
		const o = i * phentsize;
		if (ph.getUint32(o, true) === 1 && ph.getUint32(o + 20, true))
			loads.push([ph.getUint32(o + 8, true), ph.getUint32(o + 8, true) + ph.getUint32(o + 20, true),
				ph.getUint32(o + 8, true) - ph.getUint32(o + 12, true)]);
	}
	const shv = dvOf(read(shoff, shentsize * shnum));
	const sh = [];
	for (let i = 0; i < shnum; i++) {
		const o = i * shentsize, u32 = (x) => shv.getUint32(o + x, true);
		sh.push({ name: u32(0), type: u32(4), flags: u32(8), addr: u32(12), offset: u32(16), size: u32(20), link: u32(24), entsize: u32(36) });
	}
	const strOf = (s) => read(s.offset, s.size);
	const str = (tab, off) => {
		let end = off;
		while (end < tab.length && tab[end])
			end++;
		return new TextDecoder().decode(tab.subarray(off, end));
	};
	const shstr = sh[shstrndx] ? strOf(sh[shstrndx]) : new Uint8Array(0);
	for (const s of sh)
		s.sname = str(shstr, s.name);
	const delta = (va) => {
		for (const [lo, hi, d] of loads)
			if (va >= lo && va < hi)
				return d;
		return null;
	};
	const sections = [];
	for (const s of sh) {
		if (!(s.flags & 0x2) || !s.size)
			continue;
		const d = delta(s.addr);
		if (d === null)
			continue;
		sections.push({ name: s.sname, class: elfSectionClass(s.sname, s.flags, s.type), vaddr: s.addr,
			paddr: s.addr - d, size: s.size, delta: d });
	}
	sections.sort((x, y) => x.vaddr - y.vaddr);
	const symtab = sh.find((s) => s.type === 2); // SHT_SYMTAB
	if (!symtab)
		throw new Error("this ELF has no .symtab (stripped?)");
	const strtab = strOf(sh[symtab.link]);
	const tab = read(symtab.offset, symtab.size), tv = dvOf(tab);
	const symbols = [];
	for (let o = 16; o + 16 <= tab.length; o += 16) {
		const value = tv.getUint32(o + 4, true), size = tv.getUint32(o + 8, true), info = tab[o + 12], shndx = tv.getUint16(o + 14, true);
		const stype = info & 0xf, bind = info >> 4;
		if (!shndx || shndx >= 0xff00 || stype === 3 || stype === 4) // undefined, abs/common, SECTION, FILE
			continue;
		const sec = sh[shndx];
		if (!sec || !(sec.flags & 0x2))
			continue;
		const d = delta(value);
		if (d === null)
			continue;
		const cls = elfSectionClass(sec.sname, sec.flags, sec.type);
		let t = { text: "t", rodata: "r", data: "d", bss: "b", brk: "b" }[cls] || "d";
		if (bind !== 0)
			t = t.toUpperCase();
		symbols.push({ name: str(strtab, tv.getUint32(o, true)), vaddr: value, paddr: value - d, size, type: t, section: sec.sname });
	}
	symbols.sort((x, y) => x.paddr - y.paddr || x.name.localeCompare(y.name));
	const d0 = loads.length ? loads[0][2] : 0;
	return { name, sections: sections.map(({ delta: _d, ...s }) => s), symbols, source: "ELF",
		offset: d0, offsetHow: "from the ELF program headers (PT_LOAD)" };
}

// ---- System.map -----------------------------------------------------------

const MAP_CLASS = { t: "text", r: "rodata", d: "data", g: "data", s: "bss", b: "bss" };
const MAP_SECTION = { text: ".text", rodata: ".rodata", data: ".data", bss: ".bss", brk: ".brk" };

function parseSystemMap(buf, name) {
	const text = new TextDecoder("utf-8", { fatal: false }).decode(buf);
	const raw = [];
	let bad = 0, lineNo = 0;
	for (const line of text.split(/\r?\n/)) {
		lineNo++;
		const l = line.trim();
		if (!l)
			continue;
		const m = /^([0-9a-fA-F]{1,16})\s+([A-Za-z?])\s+(\S+)/.exec(l);
		if (!m) {
			if (++bad > 5 || raw.length === 0 && bad > 2)
				throw new Error("line " + lineNo + " is not \"<hex address> <type> <name>\": " + l.slice(0, 60));
			continue;
		}
		raw.push({ vaddr: parseInt(m[1], 16) >>> 0, type: m[2], name: m[3] });
	}
	if (raw.length < 2) {
		// Debian and Ubuntu ship a one-line placeholder in /boot:
		// "ffffffffffffffff B The real System.map is in the linux-image-<version>-dbg package"
		if (/real System\.map is in the linux-image-.*-dbg package/i.test(text))
			throw new Error("this is Debian's placeholder System.map, not a symbol table. Install " +
				"linux-image-<version>-dbg and use /usr/lib/debug/boot/System.map-<version>, or the unstripped " +
				"/usr/lib/debug/boot/vmlinux-<version> from the same package");
		throw new Error("no symbols found");
	}
	raw.sort((x, y) => x.vaddr - y.vaddr);
	const at = (n) => { const s = raw.find((x) => x.name === n); return s ? s.vaddr : null; };
	// Section ranges from the usual markers, else from the symbols' extent.
	const ranges = {
		text: [at("_text") ?? at("_stext"), at("_etext")],
		rodata: [at("__start_rodata"), at("__end_rodata")],
		data: [at("_sdata"), at("_edata")],
		bss: [at("__bss_start"), at("__bss_stop")],
		brk: [at("__brk_base"), at("__brk_limit")],
	};
	const kinds = {};
	for (const s of raw) {
		const cls = MAP_CLASS[s.type.toLowerCase()];
		if (!cls)
			continue;
		const k = kinds[cls] || (kinds[cls] = [Infinity, 0]);
		k[0] = Math.min(k[0], s.vaddr);
		k[1] = Math.max(k[1], s.vaddr + 1);
	}
	const sections = [];
	for (const cls of ["text", "rodata", "data", "bss", "brk"]) {
		let [lo, hi] = ranges[cls];
		if (lo === null || hi === null || hi <= lo) {
			if (!kinds[cls] || cls === "brk")
				continue;
			[lo, hi] = kinds[cls];
		}
		sections.push({ name: MAP_SECTION[cls], class: cls, vaddr: lo, size: hi - lo });
	}
	const secOf = (va, cls) => sections.find((s) => s.class === cls && va >= s.vaddr && va < s.vaddr + s.size) ||
		sections.find((s) => va >= s.vaddr && va < s.vaddr + s.size);
	// No sizes in a System.map: up to the next symbol, capped at the section end.
	const symbols = [];
	for (let i = 0; i < raw.length; i++) {
		const s = raw[i];
		const cls = MAP_CLASS[s.type.toLowerCase()];
		if (!cls)
			continue; // absolute (A/a), weak-undefined, debug ...
		let next = null;
		for (let j = i + 1; j < raw.length; j++)
			if (raw[j].vaddr > s.vaddr && MAP_CLASS[raw[j].type.toLowerCase()]) {
				next = raw[j].vaddr;
				break;
			}
		const sec = secOf(s.vaddr, cls);
		let end = next === null ? (sec ? sec.vaddr + sec.size : s.vaddr) : next;
		if (sec)
			end = Math.min(end, sec.vaddr + sec.size);
		const size = end > s.vaddr && end - s.vaddr < 0x100000 ? end - s.vaddr : 0;
		symbols.push({ name: s.name, vaddr: s.vaddr, size, type: s.type, section: sec ? sec.name : MAP_SECTION[cls] });
	}
	return { name, sections, symbols, source: "System.map", textStart: ranges.text[0] ?? (kinds.text ? kinds.text[0] : null) };
}

// Physical addresses: an explicit offset, or (auto) _text at the 32-bit
// entry v86 uses (1 MiB), or the i386 PAGE_OFFSET rule.
function placeSymbols(map, offsetText) {
	let off, how;
	const t = String(offsetText || "auto").trim().toLowerCase();
	if (t !== "auto" && t !== "") {
		off = parseInt(t.replace(/^0x/, ""), 16);
		if (!Number.isFinite(off))
			throw new Error("offset must be \"auto\" or a hex number");
		how = "set by hand";
	} else if (map.source === "ELF") {
		return map;
	} else if (map.textStart !== null && map.textStart >= PAGE_OFFSET_I386) {
		// An i386 kernel linked at PAGE_OFFSET runs at vaddr - 0xC0000000: true
		// for the minified kernel (_text 0xc0100000 at 1 MiB) and for distro
		// kernels (_text 0xc1000000 at 16 MiB, where their decompressor puts
		// it), unless KASLR moved it: boot those with "nokaslr" or set the
		// offset by hand.
		off = PAGE_OFFSET_I386;
		how = "auto: vaddr - 0xC0000000 (i386 PAGE_OFFSET; boot with nokaslr if the kernel has KASLR)";
	} else if (map.textStart !== null && map.textStart >= V86_KERNEL_ADDR) {
		off = map.textStart - V86_KERNEL_ADDR;
		how = "auto: _text at " + hex(V86_KERNEL_ADDR) + ", where v86 puts the 32-bit kernel";
	} else {
		const high = map.symbols.length && map.symbols[0].vaddr >= PAGE_OFFSET_I386;
		off = high ? PAGE_OFFSET_I386 : 0;
		how = high ? "auto: vaddr - 0xC0000000 (i386 PAGE_OFFSET)" : "auto: identity";
	}
	for (const s of map.sections)
		s.paddr = (s.vaddr - off) >>> 0;
	for (const s of map.symbols)
		s.paddr = (s.vaddr - off) >>> 0;
	map.symbols.sort((x, y) => x.paddr - y.paddr || x.name.localeCompare(y.name));
	map.offset = off;
	map.offsetHow = how;
	return map;
}

// A System.map (text, read whole) or an ELF (only its symbol parts).
async function parseSymbolFile(file, offsetText) {
	const head = new Uint8Array(await file.slice(0, 4).arrayBuffer());
	const elf = head[0] === 0x7f && head[1] === 0x45 && head[2] === 0x4c && head[3] === 0x46;
	const map = elf ? parseElf(await elfSlices(file), file.name) : parseSystemMap(await file.arrayBuffer(), file.name);
	if (elf && String(offsetText || "auto").trim().toLowerCase() !== "auto")
		map.textStart = null;
	const placed = placeSymbols(map, offsetText);
	if (!placed.symbols.length)
		throw new Error("no symbols with an address in it");
	return placed;
}

async function imageKey() {
	if (!image || !crypto.subtle)
		return null;
	if (!image.sha)
		image.sha = [...new Uint8Array(await crypto.subtle.digest("SHA-256", image.bytes))]
			.map((x) => x.toString(16).padStart(2, "0")).join("");
	return MAP_KEY + image.sha;
}

// Make imported symbols the current kernel's: everything that reads
// `symbols` uses them after the reboot. No disasm.json / sources.json; the
// DWARF is the imported vmlinux's (dbgImport), or none for a System.map.
function applySymbols(map) {
	symbols = { name: map.name, sections: map.sections, symbols: map.symbols };
	symbols.imported = { file: map.name, source: map.source, offset: map.offset, how: map.offsetHow };
	disasm = null;
	sources = null;
	if (map.source !== "ELF")
		dbgFresh(null, "a System.map has no debug info: import the unstripped vmlinux for file:line");
	syncSrcToggle();
	$("symbol_index").dataset.page = "";
	showImported();
}

function showImported() {
	const el = $("sym_status");
	const im = symbols && symbols.imported;
	el.dataset.state = im ? "imported" : "";
	el.textContent = im ? im.file + (im.file === im.source ? "" : " (" + im.source + ")") + ": " + symbols.symbols.length + " symbols, offset " +
		hex(im.offset) + ", " + im.how + ". No disasm.json for imported symbols: code is disassembled live. " +
		(dbgHas() ? DBG.info : "No file:line: " + DBG.why) + "." : "";
	el.dataset.dwarf = im ? DBG.mode || "" : "";
	$("sym_forget").hidden = !im;
}

async function importSymbols(file) {
	const el = $("sym_status");
	let map;
	try {
		map = await parseSymbolFile(file, $("sym_offset").value);
	} catch (e) {
		el.dataset.state = "error";
		el.textContent = "Could not use " + file.name + ": " + e.message + ". The previous symbols stay.";
		return false;
	}
	applySymbols(map);
	let key = null;
	try {
		key = await imageKey();
		if (key)
			localStorage.setItem(key, JSON.stringify(map));
	} catch (e) {
		// too big for localStorage, or no storage: not remembered
	}
	if (map.source === "ELF")
		dbgImport(file, map.offset, key);
	if (image)
		await boot();
	return true;
}

// The map imported earlier for this bzImage, if any.
async function restoreImported() {
	try {
		const key = await imageKey();
		const saved = key && localStorage.getItem(key);
		if (saved) {
			const map = JSON.parse(saved);
			applySymbols(map);
			if (map.source === "ELF")
				await dbgRestore(key);
			$("sym_status").textContent += " (remembered for this bzImage)";
			return true;
		}
	} catch (e) {
		// ignore
	}
	return false;
}

async function forgetImported() {
	try {
		const key = await imageKey();
		if (key) {
			localStorage.removeItem(key);
			await dbgForget(key);
		}
	} catch (e) {
		// ignore
	}
	const k = kernels.find((x) => x.name === (image && image.name));
	if (k)
		loadKernel(k).catch((e) => setStatus(String(e)));
	else {
		symbols = null;
		showImported();
		boot();
	}
}

// ---- initrd and command line ----------------------------------------------

const CMDLINE_KEY = "mkb-cmdline-";
let cmdlineFromUrl = null;

async function restoreCmdline() {
	if (cmdlineFromUrl !== null) {
		$("cmdline").value = cmdlineFromUrl;
		return;
	}
	// A remembered cmdline for this image wins; otherwise keep what the user
	// typed before choosing the file (e.g. "nokaslr") instead of clearing it.
	let v = null;
	try {
		const key = await imageKey();
		v = key ? localStorage.getItem(key.replace(MAP_KEY, CMDLINE_KEY)) : null;
	} catch (e) {
		v = null;
	}
	if (v !== null)
		$("cmdline").value = v;
	else if (!$("cmdline").dataset.typed)
		$("cmdline").value = defaultCmdline();
}

// The command line a kernel starts with when nothing is remembered and
// nothing was typed: its kernels.json "cmdline", else nokaslr (imported
// System.maps only line up without KASLR) and cryptomgr.notests (skips
// the crypto self-tests, which take long when stepped). Kernels that do
// not know them ignore them; the minified Linux ignores the command line.
const DEFAULT_CMDLINE = "nokaslr cryptomgr.notests";
function defaultCmdline() {
	const k = image && kernels.find((x) => x.name === image.name);
	return k && typeof k.cmdline === "string" ? k.cmdline : DEFAULT_CMDLINE;
}

async function saveCmdline() {
	try {
		const key = await imageKey();
		if (key)
			localStorage.setItem(key.replace(MAP_KEY, CMDLINE_KEY), $("cmdline").value);
	} catch (e) {
		// not remembered
	}
}

function initrdNote() {
	const el = $("initrd_status");
	$("initrd_clear").hidden = !initrd;
	if (!initrd) {
		el.textContent = "";
		return;
	}
	// hello and the minified Linux have no bootloader-initrd support; the
	// Debian sample and kernels from disk use it.
	const ignores = image && /^(hello|minified-linux)$/.test(image.name);
	el.textContent = initrd.name + ": " + kib(initrd.bytes.length) + ", v86 puts it at " + hex(V86_INITRD_ADDR) + "." +
		(ignores ? " This kernel ignores a bootloader initrd (the minified Linux only unpacks its built-in initramfs)." : "") +
		(initrd.info && !ignores ? initrdMatch(initrd.info) : "");
	el.dataset.mismatch = initrd.info && !ignores && initrdMismatch(initrd.info) ? "1" : "";
}

// ---- what an initrd is for ---------------------------------------------------

// Kernel release and bitness of the loaded bzImage, from its header's
// kernel_version string ("6.1.0-50-686 (...) #1 SMP ..."). x86 bzImages
// on this page are 32-bit unless the release says amd64 / x86_64.
function kernelRelease() {
	const v = image && image.hdr && image.hdr.kernel_version;
	if (!v)
		return null;
	const release = v.split(" ")[0];
	return { release, bits: /amd64|x86_64|x86-64/.test(release) ? 64 : 32 };
}

const INITRD_MAGIC = [
	["gzip", [0x1f, 0x8b]], ["zstd", [0x28, 0xb5, 0x2f, 0xfd]], ["xz", [0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00]],
	["lz4", [0x02, 0x21, 0x4c, 0x18]], ["lzma", [0x5d, 0x00, 0x00]], ["bzip2", [0x42, 0x5a, 0x68]],
	["cpio", [0x30, 0x37, 0x30, 0x37, 0x30]],
];

function magicAt(b, o) {
	for (const [name, m] of INITRD_MAGIC)
		if (m.every((x, i) => b[o + i] === x))
			return name;
	return null;
}

// Walks newc cpio entries from offset o; calls fn(name, dataStart, size)
// for each, returns the offset after the TRAILER (4-byte aligned).
function cpioWalk(b, o, fn) {
	const txt = new TextDecoder();
	while (o + 110 <= b.length && magicAt(b, o) === "cpio") {
		const hx = (i) => parseInt(txt.decode(b.subarray(o + 6 + i * 8, o + 14 + i * 8)), 16);
		const size = hx(6), nameSize = hx(11);
		const name = txt.decode(b.subarray(o + 110, o + 110 + nameSize - 1));
		const data = (o + 110 + nameSize + 3) & ~3;
		o = (data + size + 3) & ~3;
		if (name === "TRAILER!!!")
			return o;
		if (fn(name, data, size) === false)
			return -1;
	}
	return o;
}

async function gunzip(bytes) {
	const ds = new DecompressionStream("gzip");
	const out = await new Response(new Blob([bytes]).stream().pipeThrough(ds)).arrayBuffer();
	return new Uint8Array(out);
}

// What the initrd is for: its compression, and (when it can be decoded
// here: plain or gzip cpio) the kernel releases under lib/modules and
// the ELF class of /init's programs. Debian initrds start with an
// uncompressed early cpio (CPU microcode) before the compressed main one.
async function analyzeInitrd(bytes, name) {
	const info = { compression: null, releases: [], bits: 0, fromName: null };
	const m = /(\d+\.\d+[0-9A-Za-z.+~_-]*)/.exec(name || "");
	if (m)
		info.fromName = m[1].replace(/\.(img|gz|cpio|zst|xz|lz4)$/, "");
	let o = 0;
	while (magicAt(bytes, o) === "cpio") {
		const next = cpioWalk(bytes, o, () => true);
		if (next <= o)
			break;
		o = next;
		while (o < bytes.length && bytes[o] === 0)
			o++;
	}
	info.compression = o < bytes.length ? magicAt(bytes, o) || "unknown" : "cpio";
	let main = null;
	try {
		if (info.compression === "gzip" && typeof DecompressionStream === "function")
			main = await gunzip(bytes.subarray(o));
		else if (info.compression === "cpio")
			main = bytes;
	} catch (e) {
		main = null; // truncated or concatenated streams: keep what the name says
	}
	if (main) {
		const rel = new Set();
		cpioWalk(main, 0, (n, data, size) => {
			const r = /^(?:usr\/)?lib\/modules\/([^/]+)/.exec(n);
			if (r)
				rel.add(r[1]);
			if (!info.bits && size > 4 && /^(?:usr\/)?(?:bin\/busybox|sbin\/udevadm|bin\/sh|init)$/.test(n) &&
			    main[data] === 0x7f && main[data + 1] === 0x45 && main[data + 2] === 0x4c && main[data + 3] === 0x46)
				info.bits = main[data + 4] === 2 ? 64 : 32;
			return true;
		});
		info.releases = [...rel];
		info.decoded = true;
	}
	if (!info.bits && info.fromName)
		info.bits = /amd64|x86_64/.test(info.fromName) ? 64 : /686|i386|486/.test(info.fromName) ? 32 : 0;
	return info;
}

function initrdMismatch(info) {
	const k = kernelRelease();
	if (!k)
		return false;
	const rel = info.releases.length ? info.releases : info.fromName ? [info.fromName] : [];
	return (rel.length && !rel.includes(k.release)) || (info.bits && info.bits !== k.bits);
}

function initrdMatch(info) {
	const k = kernelRelease();
	const rel = info.releases.length ? info.releases.join(", ") : info.fromName;
	if (!rel && !info.bits)
		return ""; // nothing says which kernel it is for
	const what = (rel ? rel : "an unknown kernel") + (info.bits ? " (" + info.bits + "-bit)" : "");
	const how = info.decoded ? "" : " (from its file name; " + info.compression + " is not decoded here)";
	if (!k)
		return " It is for " + what + how + ".";
	if (initrdMismatch(info))
		return " Warning: this initrd is for " + what + how + "; the kernel is " + k.release + " (" + k.bits + "-bit)" +
			(info.bits && info.bits !== k.bits ? ": it cannot run its programs." : ": its modules will not load.");
	return " It matches the kernel (" + k.release + ")" + how + ".";
}

// RAM for the initrd: v86 puts it at 64 MiB, so pick the smallest menu
// size above 64 MiB + its size.
function ramForInitrd() {
	if (!initrd)
		return;
	const need = V86_INITRD_ADDR + initrd.bytes.length;
	if (ram() > need)
		return;
	const opt = [...$("memory").options].map((o) => Number(o.value)).filter((v) => v > need).sort((a, b) => a - b)[0];
	if (opt)
		$("memory").value = String(opt);
}

function initInitrd() {
	$("initrd_file").addEventListener("change", async () => {
		const f = $("initrd_file").files[0];
		if (!f)
			return;
		initrd = { name: f.name, bytes: new Uint8Array(await f.arrayBuffer()) };
		initrd.info = await analyzeInitrd(initrd.bytes, f.name);
		$("initrd_file").value = "";
		ramForInitrd();
		initrdNote();
		showHeader();
		drawMaps();
		await boot();
	});
	$("initrd_clear").addEventListener("click", async () => {
		initrd = null;
		initrdNote();
		showHeader();
		drawMaps();
		await boot();
	});
	$("cmdline").addEventListener("input", () => { $("cmdline").dataset.typed = "1"; });
	$("cmdline").addEventListener("change", saveCmdline);
	$("cmdline").addEventListener("keydown", (ev) => {
		if (ev.key === "Enter") {
			saveCmdline();
			boot();
		}
	});
}

function looksLikeBzImage(buf) {
	return buf.byteLength > 0x206 && new DataView(buf).getUint32(0x202, true) === 0x53726448;
}

function initImport() {
	$("symfile").addEventListener("change", async () => {
		const f = $("symfile").files[0];
		if (f)
			await importSymbols(f);
		$("symfile").value = "";
	});
	$("sym_forget").addEventListener("click", forgetImported);
	// Drop a bzImage, a System.map / vmlinux, or both on the page.
	document.addEventListener("dragover", (ev) => { ev.preventDefault(); });
	document.addEventListener("drop", async (ev) => {
		ev.preventDefault();
		const files = [...(ev.dataTransfer ? ev.dataTransfer.files : [])];
		// Only the setup header decides what a file is: a dropped vmlinux
		// can be hundreds of MB.
		const heads = await Promise.all(files.map((f) => f.slice(0, 0x210).arrayBuffer()));
		const k = heads.findIndex(looksLikeBzImage);
		if (k >= 0) {
			disasm = null;
			sources = null;
			dbgFresh(null, "no debug info for a bzImage from disk: import its unstripped vmlinux for file:line");
			syncSrcToggle();
			useImage(files[k].name, await files[k].arrayBuffer());
		}
		const m = files.findIndex((_, i) => i !== k);
		if (m >= 0)
			await importSymbols(files[m]);
		else if (k >= 0) {
			await restoreImported();
			await boot();
		}
	});
}
