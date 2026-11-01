"use strict";

// Where v86 0.5.471 puts a bzImage (src/kernel.js, load_kernel): the
// real-mode part at 0x80000, the command line at 0x8e000, the 32-bit part
// at 1 MiB whatever loadflags/code32_start say.
const V86_SETUP_ADDR = 0x80000;
const V86_CMDLINE_ADDR = 0x8e000;
const V86_HEAP_END = 0x8de00;
const V86_KERNEL_ADDR = 0x100000;
const PAGE = 4096;

const $ = (id) => document.getElementById(id);
const hex = (n) => "0x" + n.toString(16);
const kib = (n) => (n < 1024 ? n + " bytes" :
	(n / 1024).toLocaleString("en", { maximumFractionDigits: 1 }) + " KiB");

let kernels = [];
let image = null;      // { name, bytes: Uint8Array, hdr }
let emulator = null;
let scanTimer = null;

function setStatus(text) {
	$("status").textContent = text;
}

// ---- boot header ----------------------------------------------------------

function parseHeader(bytes) {
	const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
	const u8 = (o) => (o < bytes.length ? dv.getUint8(o) : 0);
	const u16 = (o) => (o + 2 <= bytes.length ? dv.getUint16(o, true) : 0);
	const u32 = (o) => (o + 4 <= bytes.length ? dv.getUint32(o, true) : 0);
	const h = {
		size: bytes.length,
		setup_sects: u8(0x1f1) || 4,
		syssize: u32(0x1f4),
		boot_flag: u16(0x1fe),
		magic: u32(0x202),
		version: u16(0x206),
		loadflags: u8(0x211),
		code32_start: u32(0x214),
		pref_address: u32(0x258),
		init_size: u32(0x260),
	};
	h.setup_size = (h.setup_sects + 1) * 512;
	h.payload = Math.max(0, h.size - h.setup_size);
	h.loaded_high = (h.loadflags & 1) !== 0;
	h.has_init_size = h.version >= 0x20a;
	return h;
}

// Highest physical address (exclusive) the kernel needs, as v86 loads it.
function kernelEnd(h) {
	let end = V86_KERNEL_ADDR + h.payload;
	if (h.has_init_size && h.pref_address >= V86_KERNEL_ADDR)
		end = Math.max(end, h.pref_address + h.init_size);
	return end;
}

function checkImage(h, ram) {
	const problems = [];
	if (h.magic !== 0x53726448)
		problems.push(["error", "No \"HdrS\" signature at 0x202: this is not a bzImage."]);
	if (h.boot_flag !== 0xaa55)
		problems.push(["error", "Boot flag at 0x1fe is " + hex(h.boot_flag) +
			", not 0xaa55: v86 refuses the image (\"Bad checksum1\")."]);
	if (problems.length)
		return problems;
	if (h.version < 0x202)
		problems.push(["warn", "Boot protocol " + verStr(h.version) +
			" is older than 2.02, which v86 assumes."]);
	if (!h.loaded_high)
		problems.push(["error", "LOADED_HIGH is clear: the kernel expects its 32-bit part at " +
			hex(h.code32_start) + ", but v86 ignores loadflags and copies it to 1 MiB, so it " +
			"will not start here. (qemu -kernel honours the flag.)"]);
	if (V86_KERNEL_ADDR + h.payload > ram)
		problems.push(["error", "v86 copies the " + kib(h.payload) + " 32-bit part to 1 MiB, but RAM " +
			"ends at " + hex(ram) + ". The loader cannot place the kernel: pick more RAM."]);
	else if (kernelEnd(h) > ram)
		problems.push(["error", "The kernel needs memory up to " + hex(kernelEnd(h)) +
			" (pref_address + init_size), but RAM ends at " + hex(ram) + " (" + kib(ram) +
			"). It will fail early, typically with no output: pick at least " +
			kib(kernelEnd(h)) + "."]);
	return problems;
}

function verStr(v) {
	return (v >> 8) + "." + (v & 0xff).toString().padStart(2, "0");
}

function showHeader() {
	const tbody = $("header").tBodies[0];
	tbody.textContent = "";
	$("problems").textContent = "";
	if (!image)
		return;
	const h = image.hdr;
	const rows = [
		["image", image.name + ", " + h.size.toLocaleString("en") + " bytes"],
		["protocol", verStr(h.version)],
		["setup_sects", h.setup_sects + " (" + h.setup_size + " bytes of real-mode setup)"],
		["syssize", hex(h.syssize) + " (" + (h.syssize * 16).toLocaleString("en") + " bytes)"],
		["loadflags", hex(h.loadflags) + (h.loaded_high ? " (LOADED_HIGH)" : " (low kernel)")],
		["code32_start", hex(h.code32_start)],
	];
	if (h.has_init_size) {
		rows.push(["pref_address", hex(h.pref_address)]);
		rows.push(["init_size", hex(h.init_size) + " (" + kib(h.init_size) + ")"]);
	}
	for (const [k, v] of rows) {
		const tr = tbody.insertRow();
		tr.insertCell().textContent = k;
		tr.insertCell().textContent = v;
	}
	for (const [level, text] of checkImage(h, ram())) {
		const div = document.createElement("div");
		div.className = level;
		div.textContent = text;
		$("problems").append(div);
	}
}

// ---- memory map -----------------------------------------------------------

const REGION_NAMES = {
	"r-bios": "IVT, BDA, EBDA",
	"r-conv": "conventional RAM",
	"r-setup": "real-mode setup, heap, command line",
	"r-vga": "VGA window",
	"r-rom": "VGA BIOS, option ROMs, BIOS",
	"r-kernel": "32-bit kernel image",
	"r-init": "kernel init_size (runtime footprint)",
	"r-ext": "free extended RAM",
	"r-hole": "beyond RAM",
};

function ram() {
	return Number($("memory").value);
}

function drawBar(el, regions, start, end) {
	el.textContent = "";
	for (const [a, b, cls, label] of regions) {
		const lo = Math.max(a, start), hi = Math.min(b, end);
		if (hi <= lo)
			continue;
		const div = document.createElement("div");
		div.style.flex = (hi - lo) + " 0 0";
		div.style.background = "var(--" + cls + ")";
		div.title = label + ": " + hex(lo) + "–" + hex(hi) + " (" + kib(hi - lo) + ")";
		div.textContent = label;
		el.append(div);
	}
}

function drawMaps() {
	const r = ram();
	$("ram_label").textContent = kib(r);
	const low = [];
	const top = Math.min(r, 0x9fc00);
	low.push([0, 0x500, "r-bios", "IVT+BDA"]);
	if (image) {
		const setupEnd = V86_SETUP_ADDR + image.hdr.setup_size;
		low.push([0x500, Math.min(top, V86_SETUP_ADDR), "r-conv", "conventional"]);
		low.push([V86_SETUP_ADDR, Math.min(top, setupEnd), "r-setup", "setup"]);
		low.push([setupEnd, Math.min(top, V86_CMDLINE_ADDR + 0x100), "r-setup", "heap+cmdline"]);
		low.push([V86_CMDLINE_ADDR + 0x100, top, "r-conv", ""]);
	} else {
		low.push([0x500, top, "r-conv", "conventional"]);
	}
	low.push([0x9fc00, 0xa0000, "r-bios", "EBDA"]);
	low.push([0xa0000, 0xc0000, "r-vga", "VGA"]);
	low.push([0xc0000, 0xc8000, "r-rom", "VGA BIOS"]);
	low.push([0xc8000, 0xe0000, "r-rom", "option ROMs"]);
	low.push([0xe0000, 0x100000, "r-rom", "BIOS"]);
	drawBar($("map_low"), low, 0, 0x100000);

	const all = [[0, 0x100000, "r-bios", "first MiB"]];
	let end = r;
	if (image) {
		const h = image.hdr;
		const kEnd = V86_KERNEL_ADDR + h.payload;
		const need = kernelEnd(h);
		all.push([V86_KERNEL_ADDR, kEnd, "r-kernel", "kernel"]);
		all.push([kEnd, need, "r-init", "init_size"]);
		all.push([need, r, "r-ext", "free"]);
		end = Math.max(r, need);
		all.push([r, end, "r-hole", "beyond RAM"]);
	} else {
		all.push([0x100000, r, "r-ext", "free"]);
	}
	drawBar($("map_all"), all, 0, end);

	const legend = $("legend");
	legend.textContent = "";
	for (const [cls, name] of Object.entries(REGION_NAMES)) {
		const li = document.createElement("li");
		const sw = document.createElement("span");
		sw.style.background = "var(--" + cls + ")";
		li.append(sw, name);
		legend.append(li);
	}
}

// ---- page scan ------------------------------------------------------------

function scan() {
	if (!emulator)
		return;
	const r = ram();
	const mem = emulator.read_memory(0, r);
	const words = new Uint32Array(mem.buffer.slice(mem.byteOffset, mem.byteOffset + r));
	const pages = r / PAGE;
	const used = new Uint8Array(pages);
	let count = 0, low = 0;
	for (let p = 0; p < pages; p++) {
		const base = p * (PAGE / 4);
		for (let i = 0; i < PAGE / 4; i++) {
			if (words[base + i]) {
				used[p] = 1;
				count++;
				if (p < 256)
					low++;
				break;
			}
		}
	}
	const cols = pages <= 1024 ? 32 : 128;
	const cell = pages <= 1024 ? 12 : 5;
	const canvas = $("pages");
	canvas.width = cols * cell;
	canvas.height = Math.ceil(pages / cols) * cell;
	const ctx = canvas.getContext("2d");
	const css = getComputedStyle(document.documentElement);
	const on = css.getPropertyValue("--page-used").trim();
	const off = css.getPropertyValue("--page-free").trim();
	for (let p = 0; p < pages; p++) {
		ctx.fillStyle = used[p] ? on : off;
		ctx.fillRect((p % cols) * cell, Math.floor(p / cols) * cell, cell - 1, cell - 1);
	}
	const fp = $("footprint");
	fp.dataset.nonzero = count;
	fp.dataset.total = pages;
	fp.textContent = count + " of " + pages + " pages non-zero = " + kib(count * PAGE) +
		" (" + low + " below 1 MiB, " + (count - low) + " above)";
}

function setAutoscan() {
	clearInterval(scanTimer);
	scanTimer = null;
	if ($("autoscan").checked && emulator)
		scanTimer = setInterval(scan, 1000);
}

// ---- boot -----------------------------------------------------------------

async function boot() {
	if (!image)
		return;
	if (emulator) {
		const old = emulator;
		emulator = null;
		try {
			await old.destroy();
		} catch (e) {
			console.warn("destroy:", e);
		}
	}
	$("scan").disabled = true;
	$("footprint").textContent = "";
	$("screen_text").textContent = "";
	const fatal = checkImage(image.hdr, ram()).some(([level]) => level === "error");
	setStatus("Booting " + image.name + " with " + kib(ram()) + "...");
	try {
		emulator = new V86({
			wasm_path: "v86/v86.wasm",
			memory_size: ram(),
			vga_memory_size: 256 * 1024,
			screen_container: $("screen_container"),
			bios: { url: "v86/seabios.bin" },
			vga_bios: { url: "v86/vgabios.bin" },
			bzimage: { buffer: image.bytes.buffer.slice(0) },
			cmdline: "",
			autostart: true,
			disable_mouse: true,
		});
	} catch (e) {
		setStatus("v86 refused to start: " + e);
		return;
	}
	const em = emulator;
	em.add_listener("emulator-started", () => {
		if (em !== emulator)
			return;
		setStatus("Running " + image.name + " with " + kib(ram()) +
			(fatal ? ". The header check above predicts this will not work." : "."));
		$("scan").disabled = false;
		setAutoscan();
	});
}

function reportError(err) {
	setStatus("Emulator error: " + err + (image ? " (" + image.name + ", " + kib(ram()) + " RAM)" : ""));
}
window.addEventListener("error", (e) => reportError(e.message));
window.addEventListener("unhandledrejection", (e) => reportError(e.reason));

function smallestFitting(h) {
	const need = Math.max(kernelEnd(h), 0x100000);
	for (const opt of $("memory").options)
		if (Number(opt.value) >= need)
			return opt.value;
	return $("memory").options[$("memory").options.length - 1].value;
}

function useImage(name, buffer, memFromUrl) {
	const bytes = new Uint8Array(buffer);
	image = { name, bytes, hdr: parseHeader(bytes) };
	$("memory").value = memFromUrl || smallestFitting(image.hdr);
	showHeader();
	drawMaps();
}

async function loadKernel(entry, memFromUrl) {
	setStatus("Fetching " + entry.path + "...");
	const resp = await fetch(entry.path);
	if (!resp.ok)
		throw new Error(entry.path + ": HTTP " + resp.status);
	useImage(entry.name, await resp.arrayBuffer(), memFromUrl);
	await boot();
}

async function init() {
	const params = new URLSearchParams(location.search);
	// ?mem=BYTES may name any multiple of 4 KiB, for probing the minimum.
	let memParam = null;
	const m = Number(params.get("mem"));
	if (Number.isInteger(m) && m > 0 && m % PAGE === 0) {
		memParam = String(m);
		if (![...$("memory").options].some((o) => o.value === memParam))
			$("memory").add(new Option(kib(m), memParam));
	}
	drawMaps();
	$("memory").addEventListener("change", () => { showHeader(); drawMaps(); });
	$("boot").addEventListener("click", boot);
	$("scan").addEventListener("click", scan);
	$("autoscan").addEventListener("change", setAutoscan);
	$("kernel").addEventListener("change", () => {
		const k = kernels.find((x) => x.name === $("kernel").value);
		if (k)
			loadKernel(k).catch((e) => setStatus(String(e)));
	});
	$("file").addEventListener("change", async () => {
		const f = $("file").files[0];
		if (!f)
			return;
		useImage(f.name, await f.arrayBuffer());
		await boot();
	});

	try {
		const resp = await fetch("kernels.json");
		kernels = (await resp.json()).kernels;
	} catch (e) {
		setStatus("No kernels.json (" + e + "). Load a bzImage from disk.");
		return;
	}
	for (const k of kernels)
		$("kernel").add(new Option(k.name, k.name));
	const wanted = kernels.find((k) => k.name === params.get("kernel")) || kernels[0];
	if (!wanted) {
		setStatus("kernels.json lists no kernels. Load a bzImage from disk.");
		return;
	}
	$("kernel").value = wanted.name;
	await loadKernel(wanted, memParam);
}

init().catch((e) => setStatus(String(e)));
