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

function parseElf(buf, name) {
	const b = new Uint8Array(buf);
	const dv = new DataView(buf);
	if (b.length < 52 || dv.getUint32(0) !== 0x7f454c46)
		throw new Error("not an ELF file");
	if (b[4] !== 1 || b[5] !== 1)
		throw new Error("not a 32-bit little-endian ELF (only i386 kernels boot here)");
	const u16 = (o) => dv.getUint16(o, true), u32 = (o) => dv.getUint32(o, true);
	const phoff = u32(0x1c), shoff = u32(0x20);
	const phentsize = u16(0x2a), phnum = u16(0x2c), shentsize = u16(0x2e), shnum = u16(0x30), shstrndx = u16(0x32);
	if (!shoff || shoff + shnum * shentsize > b.length)
		throw new Error("ELF without section headers");
	const loads = [];
	for (let i = 0; i < phnum; i++) {
		const o = phoff + i * phentsize;
		if (u32(o) === 1 && u32(o + 20))
			loads.push([u32(o + 8), u32(o + 8) + u32(o + 20), u32(o + 8) - u32(o + 12)]);
	}
	const sh = [];
	for (let i = 0; i < shnum; i++) {
		const o = shoff + i * shentsize;
		sh.push({ name: u32(o), type: u32(o + 4), flags: u32(o + 8), addr: u32(o + 12), offset: u32(o + 16),
			size: u32(o + 20), link: u32(o + 24), entsize: u32(o + 36) });
	}
	const str = (tab, off) => {
		let end = tab + off;
		while (end < b.length && b[end])
			end++;
		return new TextDecoder().decode(b.subarray(tab + off, end));
	};
	const shstr = sh[shstrndx] ? sh[shstrndx].offset : 0;
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
	const strtab = sh[symtab.link].offset;
	const symbols = [];
	for (let o = symtab.offset + 16; o + 16 <= symtab.offset + symtab.size; o += 16) {
		const value = u32(o + 4), size = u32(o + 8), info = b[o + 12], shndx = u16(o + 14);
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
		symbols.push({ name: str(strtab, u32(o)), vaddr: value, paddr: value - d, size, type: t, section: sec.sname });
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

function parseSymbolFile(buf, name, offsetText) {
	const head = new Uint8Array(buf, 0, Math.min(4, buf.byteLength));
	const elf = head[0] === 0x7f && head[1] === 0x45 && head[2] === 0x4c && head[3] === 0x46;
	const map = elf ? parseElf(buf, name) : parseSystemMap(buf, name);
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
// `symbols` uses them after the reboot. No disasm.json / sources.json.
function applySymbols(map) {
	symbols = { name: map.name, sections: map.sections, symbols: map.symbols };
	symbols.imported = { file: map.name, source: map.source, offset: map.offset, how: map.offsetHow };
	disasm = null;
	sources = null;
	$("src_toggle").hidden = true;
	$("symbol_index").dataset.page = "";
	showImported();
}

function showImported() {
	const el = $("sym_status");
	const im = symbols && symbols.imported;
	el.dataset.state = im ? "imported" : "";
	el.textContent = im ? im.file + (im.file === im.source ? "" : " (" + im.source + ")") + ": " + symbols.symbols.length + " symbols, offset " +
		hex(im.offset) + ", " + im.how + ". No disasm.json or C source for imported symbols: code is disassembled live." : "";
	$("sym_forget").hidden = !im;
}

async function importSymbols(file) {
	const el = $("sym_status");
	let map;
	try {
		map = parseSymbolFile(await file.arrayBuffer(), file.name, $("sym_offset").value);
	} catch (e) {
		el.dataset.state = "error";
		el.textContent = "Could not use " + file.name + ": " + e.message + ". The previous symbols stay.";
		return false;
	}
	applySymbols(map);
	try {
		const key = await imageKey();
		if (key)
			localStorage.setItem(key, JSON.stringify(map));
	} catch (e) {
		// too big for localStorage, or no storage: not remembered
	}
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
			applySymbols(JSON.parse(saved));
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
		if (key)
			localStorage.removeItem(key);
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
	const ours = image && kernels.some((k) => k.name === image.name);
	el.textContent = initrd.name + ": " + kib(initrd.bytes.length) + ", v86 puts it at " + hex(V86_INITRD_ADDR) + "." +
		(ours ? " The kernels on this page ignore a bootloader initrd (the minified Linux only unpacks its built-in initramfs)." : "");
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
		const bufs = await Promise.all(files.map((f) => f.arrayBuffer()));
		const k = bufs.findIndex(looksLikeBzImage);
		if (k >= 0) {
			disasm = null;
			sources = null;
			$("src_toggle").hidden = true;
			useImage(files[k].name, bufs[k]);
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
