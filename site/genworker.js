"use strict";
// Worker: the data files CI makes for a shipped kernel (symbols.json,
// disasm.json, dwarf/, src/), made here for an imported vmlinux by kgen.js
// with llvm-nm and llvm-objdump of clang/clang.wasm on wasi.js (plan E1).
// genimport.js is the page's side; it talks to this through clang.js's
// ClangWorker (same requests and answers, plus progress messages):
//
//   { op: "gen", file, name, clang, label, trim, disasm, symbols, dwarf }
//       file: the ELF (a File or Blob, read whole); name: its file name
//       (dwarf/index.json "source"); clang: clang.wasm's URL; label:
//       symbols.json "name" (default: name); trim: true, false or "auto"
//       (gen-symbols.py --trim above 20,000 symbols); disasm: true, false
//       or "auto" (only when the code is at most 8 MiB: CI makes none for
//       Debian's); symbols, dwarf: false leaves those out (CI makes them
//       from two ELFs, the plain build's and the -g build's)
//       -> { files: [[path, Uint8Array]] (symbols.json, disasm.json,
//          dwarf/<file>), log: [text], why: { disasm?, dwarf? } (what was
//          not made and why), times: { tools, symbols, disasm, dwarf } }
//   { op: "src", dwarf: [[name, Uint8Array]], tree: [[path, Blob]], origin, notes }
//       dwarf: the "gen" answer's dwarf/ files (names without "dwarf/");
//       tree: a source tree's files (the kernel tree may be one level or
//       more down, as source.js srcPickedFile finds it)
//       -> { files: [["src/<path>.gz" | "src/index.json", Uint8Array]],
//          missing, pastEnd, log }
//   meanwhile { id, progress: text }; any failure -> { id, error }

importScripts(...["wasi.js", "dwarf.js", "unwind.js", "kgen.js"].map((f) => f + self.location.search));

let clangModule = null;

async function clangWasm(url) {
	if (!clangModule)
		clangModule = (async () => {
			const resp = await fetch(url);
			if (!resp.ok)
				throw new Error(url.replace(/^.*\//, "") + ": HTTP " + resp.status);
			return WebAssembly.compileStreaming(resp);
		})();
	try {
		return await clangModule;
	} catch (e) {
		clangModule = null;
		throw e;
	}
}

async function gzip(data) {
	const b = typeof data === "string" ? new TextEncoder().encode(data) : data;
	const s = new Blob([b]).stream().pipeThrough(new CompressionStream("gzip"));
	return new Uint8Array(await new Response(s).arrayBuffer());
}

const enc = new TextEncoder();
const bytesOf = (data) => typeof data === "string" ? enc.encode(data) : data;

// The ELF's executable code in bytes (allocated PROGBITS with SHF_EXECINSTR).
function codeBytes(read) {
	return kgElf(read).shdrs.reduce((n, s) => n + (s.type === 1 && s.flags & 2 && s.flags & 4 ? s.size : 0), 0);
}

async function gen({ id, file, name, clang, label = name, trim = "auto", disasm = "auto", symbols = true, dwarf = true }) {
	const say = (progress) => self.postMessage({ id, progress });
	const times = {}, log = [], why = {}, files = [];
	let t = performance.now();
	const lap = (k) => { const now = performance.now(); times[k] = Math.round(now - t); t = now; };
	say("reading " + name);
	const elf = new Uint8Array(new FileReaderSync().readAsArrayBuffer(file));
	const read = (off, len) => {
		if (off + len > elf.length)
			throw new Error("truncated ELF file (" + name + ")");
		return elf.subarray(off, off + len);
	};
	// Not an ELF: fails before the download of the tools.
	if (elf.length < 52 || elf[0] !== 0x7f || elf[1] !== 0x45 || elf[2] !== 0x4c || elf[3] !== 0x46)
		throw new Error(name + " is not a 32-bit little-endian ELF");
	kgElf(read);
	say("loading llvm-nm and llvm-objdump (clang.wasm)");
	const mod = await clangWasm(clang);
	const fs = new MemFS();
	fs.writeFile("/k/vmlinux", elf);
	const tool = (args, stdout) => {
		const r = wasiRun(mod, fs, { args, cwd: "/k", stdout });
		if (r.code !== 0)
			throw new Error(args[0] + " exited with " + r.code + ": " + new TextDecoder().decode(r.stderr).trim().slice(0, 300));
		return r;
	};
	lap("tools");
	if (symbols) {
		say("symbols (llvm-nm)");
		const nm = new TextDecoder().decode(tool(["llvm-nm", "-nS", "vmlinux"]).stdout);
		let s = kgenSymbols(read, nm, label, trim === true);
		if (trim === "auto" && s.symbols.length > 20000)
			s = kgenSymbols(read, nm, label, true);
		files.push(["symbols.json", bytesOf(s.json)]);
		log.push(`symbols.json: ${s.sections.length} sections, ${s.symbols.length} symbols`);
	}
	lap("symbols");
	const code = codeBytes(read);
	if (disasm === true || disasm === "auto" && code <= 8 << 20) {
		say("instructions (llvm-objdump -d)");
		const d = kgenDisasm(read, new TextDecoder().decode(tool(["llvm-objdump", "-d", "vmlinux"]).stdout));
		files.push(["disasm.json", bytesOf(d.json)]);
		log.push(`disasm.json: ${d.insns.length} instructions, ${d.labels.length} labels, ${d.taken.length} taken`);
	} else
		why.disasm = disasm === false ? "not asked for" : (code / 1048576).toFixed(1) + " MiB of code: more than the 8 MiB disasm.json is made for";
	lap("disasm");
	const e = dwarfElf(read);
	if (!dwarf)
		why.dwarf = "not asked for";
	else if (!dwarfSections(e, read))
		why.dwarf = name + " has no DWARF (.debug_info): build it with CONFIG_DEBUG_INFO for file:line";
	else {
		say("instruction starts (llvm-objdump -d --no-show-raw-insn)");
		const starts = new KgInsnStarts(e.toPhys);
		let insns;
		try {
			tool(["llvm-objdump", "-d", "--no-show-raw-insn", "vmlinux"], (b) => starts.push(b));
			insns = starts.finish("llvm-objdump -d");
		} catch (e) {
			insns = { why: e.message };
		}
		await kgenDwarf(read, {
			name, insns,
			put: (n, data) => files.push(["dwarf/" + n, bytesOf(data)]),
			gzip, log: (text) => log.push("dwarf/" + text),
			progress: (what) => say("DWARF: " + what),
		});
	}
	lap("dwarf");
	return { files, log, why, times };
}

async function src({ id, dwarf, tree, origin, notes }) {
	const say = (progress) => self.postMessage({ id, progress });
	const dw = new Map(dwarf);
	const entries = new Map(tree);
	let prefix;
	const find = (p) => {
		if (prefix === undefined)
			for (const n of entries.keys())
				if (n === p || n.endsWith("/" + p)) {
					prefix = n.slice(0, n.length - p.length);
					break;
				}
		return prefix === undefined ? null : entries.get(prefix + p) || null;
	};
	const rd = new FileReaderSync();
	const files = [], log = [];
	let n = 0;
	const r = await kgenSrc((name) => {
		if (!dw.has(name))
			throw new Error("no dwarf/" + name);
		return dw.get(name);
	}, (p) => {
		if (++n % 200 === 0)
			say("C source: " + n + " files");
		const f = find(p);
		return f ? new Uint8Array(rd.readAsArrayBuffer(f)) : null;
	}, { origin, notes, gzip, put: (name, data) => files.push(["src/" + name, bytesOf(data)]), log: (text) => log.push("src: " + text) });
	return { files, missing: r.missing, pastEnd: r.pastEnd, log };
}

const ops = { gen, src };

self.onmessage = async (e) => {
	const { id, op } = e.data;
	const t = performance.now();
	try {
		const answer = await ops[op](e.data);
		answer.id = id;
		answer.ms = performance.now() - t;
		self.postMessage(answer, [...new Set(answer.files.map((f) => f[1].buffer))]);
	} catch (err) {
		self.postMessage({ id, error: String(err && err.message || err) });
	}
};
