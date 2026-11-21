"use strict";
// SPDX-License-Identifier: GPL-2.0
// usr/gen_init_cpio of the minified Linux in JavaScript (plan E9): the
// initramfs the page's rebuild makes from minified/elo (kit.js runs it as
// a generator of the kit, before the compiles). Byte for byte the archive
// gen_init_cpio writes for the same input lines (test_kit.mjs --unit
// compares them; --build compares it with CI's usr/initramfs_data.cpio).

// One line of gen_init_cpio's input: { type: "file", name, location, mode,
// uid, gid }, { type: "dir", ... } or { type: "nod", ..., major, minor }.
// As its sscanf: the keyword first on the line, any spaces between the
// fields.
function kitCpioLine(line) {
	const w = line.replace(/\s+$/, "").split(/\s+/);
	const num = (s, base) => /^[0-9]+$/.test(s) && (base === 10 || !/[89]/.test(s)) ? parseInt(s, base) : NaN;
	let e = null;
	if (w[0] === "file" && w.length === 6)
		e = { type: "file", name: w[1], location: w[2], mode: num(w[3], 8), uid: num(w[4], 10), gid: num(w[5], 10) };
	else if (w[0] === "dir" && w.length === 5)
		e = { type: "dir", name: w[1], mode: num(w[2], 8), uid: num(w[3], 10), gid: num(w[4], 10) };
	else if (w[0] === "nod" && w.length === 8 && w[5] === "c")
		e = { type: "nod", name: w[1], mode: num(w[2], 8), uid: num(w[3], 10), gid: num(w[4], 10), major: num(w[6], 10), minor: num(w[7], 10) };
	if (!e || Object.values(e).some((v) => Number.isNaN(v)))
		throw new Error("gen_init_cpio: unsupported line: " + line);
	return e;
}

// The newc ("070701") archive gen_init_cpio writes for list (its input
// lines); files: location -> bytes. Every mtime 0, inode numbers from 721,
// the trailer, padded to 512 bytes: CI's usr/initramfs_data.cpio.
function kitCpio(list, files) {
	const parts = [];
	let at = 0, ino = 721;
	const put = (b) => {
		parts.push(b);
		at += b.length;
	};
	const pad = (n) => {
		if (at % n)
			put(new Uint8Array(n - at % n));
	};
	const header = (i, mode, uid, gid, nlink, size, maj, min, name) => {
		name = name.replace(/^\//, "");
		const f = [i, mode, uid, gid, nlink, 0, size, i ? 3 : 0, i ? 1 : 0, maj, min, name.length + 1, 0];
		put(new TextEncoder().encode("070701" + f.map((x) => (x >>> 0).toString(16).toUpperCase().padStart(8, "0")).join("") + name + "\0"));
		pad(4);
	};
	for (const line of list) {
		const e = kitCpioLine(line);
		if (e.type === "file") {
			const b = files.get(e.location);
			if (!b)
				throw new Error("gen_init_cpio: cannot read: " + e.location);
			header(ino++, 0o100000 | e.mode, e.uid, e.gid, 1, b.length, 0, 0, e.name);
			put(b);
			pad(4);
		} else if (e.type === "dir") {
			header(ino++, 0o40000 | e.mode, e.uid, e.gid, 2, 0, 0, 0, e.name);
		} else {
			header(ino++, 0o20000 | e.mode, e.uid, e.gid, 1, 0, e.major, e.minor, e.name);
		}
	}
	header(0, 0, 0, 0, 1, 0, 0, 0, "TRAILER!!!");
	pad(512);
	const out = new Uint8Array(at);
	let k = 0;
	for (const b of parts) {
		out.set(b, k);
		k += b.length;
	}
	return out;
}

export { kitCpioLine, kitCpio };
