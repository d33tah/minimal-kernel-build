"use strict";

// The bzImage's boot header (the Machine tab's header table), the status
// line, and the guest's RAM.

import { $, V86_KERNEL_ADDR, hex, kib, V86_INITRD_ADDR } from "./util.js";
import { initrd, image, provide } from "./page.js";

// Where v86 0.5.471 puts a bzImage (src/kernel.js, load_kernel): the
// real-mode part at 0x80000, the command line at 0x8e000, the 32-bit part
// at 1 MiB whatever loadflags/code32_start say.
const V86_SETUP_ADDR = 0x80000;
const V86_CMDLINE_ADDR = 0x8e000;
const V86_HEAP_END = 0x8de00;
const DEFAULT_RAM = 2 * 1024 * 1024;

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
	// kernel_version (boot protocol 2.00+): offset from 0x200 of a
	// NUL-terminated string such as "6.1.0-50-686 (debian-kernel@...) #1 SMP ...".
	const kv = u16(0x20e);
	h.kernel_version = "";
	if (kv && kv + 0x200 < Math.min(bytes.length, h.setup_size))
		for (let o = kv + 0x200; o < Math.min(bytes.length, kv + 0x400) && u8(o); o++)
			h.kernel_version += String.fromCharCode(u8(o));
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
		problems.push(["warn", "The kernel says it needs memory up to " + hex(kernelEnd(h)) +
			" (pref_address + init_size), but RAM ends at " + hex(ram) + " (" + kib(ram) +
			"). init_size covers bss and brk in full; a kernel that does not touch all of them " +
			"can still boot, otherwise it fails early, typically with no output. " +
			kib(kernelEnd(h)) + " is enough."]);
	if (initrd) {
		const end = V86_INITRD_ADDR + initrd.bytes.length;
		if (end > ram)
			problems.push(["error", "v86 puts the initrd (" + kib(initrd.bytes.length) + ") at 64 MiB, so RAM must be " +
				"larger than " + kib(end) + "; it is " + kib(ram) + ". Pick more RAM."]);
		if (h.version >= 0x203) {
			const max = new DataView(image.bytes.buffer, image.bytes.byteOffset).getUint32(0x22c, true);
			if (max && end - 1 > max)
				problems.push(["warn", "The kernel accepts an initrd only below " + hex(max) + " (initrd_addr_max), but v86 " +
					"puts it at " + hex(V86_INITRD_ADDR) + "\u2013" + hex(end - 1) + "."]);
		}
		if (V86_KERNEL_ADDR + h.payload > V86_INITRD_ADDR || kernelEnd(h) > V86_INITRD_ADDR)
			problems.push(["warn", "The kernel's memory (up to " + hex(kernelEnd(h)) + ") reaches the initrd at 64 MiB."]);
	}
	return problems;
}

function verStr(v) {
	return (v >> 8) + "." + (v & 0xff).toString().padStart(2, "0");
}

function showHeader() {
	$("problems").textContent = "";
	$("kinfo").textContent = "";
	if (!image)
		return;
	const h = image.hdr;
	const parts = [h.size.toLocaleString("en") + " B", "proto " + verStr(h.version),
		"setup " + h.setup_size + " B", "32-bit " + (h.syssize * 16).toLocaleString("en") + " B @ " + hex(h.code32_start),
		h.loaded_high ? "LOADED_HIGH" : "low"];
	if (h.has_init_size)
		parts.push("init_size " + kib(h.init_size));
	$("kinfo").textContent = parts.join(" \u00b7 ");
	for (const [level, text] of checkImage(h, ram())) {
		const div = document.createElement("div");
		div.className = level;
		div.textContent = text;
		$("problems").append(div);
	}
}

function ram() {
	return Number($("memory").value);
}

// The page API's entries of this module (page.js).
provide({ setStatus, ram, showHeader });

export { ram, V86_SETUP_ADDR, V86_CMDLINE_ADDR, kernelEnd, checkImage, setStatus, parseHeader, DEFAULT_RAM,
	showHeader };
