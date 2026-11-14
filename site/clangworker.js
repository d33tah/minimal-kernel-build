"use strict";
// Worker: the page's toolchain, clang/clang.wasm (clang, ld.lld and the
// llvm tools 22.1.0 for i386/x86_64, wasm32-wasip1), on an in-memory file
// system that lives as long as the Worker (wasi.js). clang.js is the
// page's side. Each message has an id, each answer the same id:
//
//   { op: "load", url, name }       -> { bytes }: fetch and compile the module, kept as
//                                      name (clang.wasm: "clang"; a host tool: its own)
//   { op: "write", files }          -> { written: n }: files [[path, Uint8Array | string]]
//                                      (the arrays are kept, not copied)
//   { op: "read", paths }           -> { files: [[path, Uint8Array | null]] } (copies)
//   { op: "remove", paths }         -> { removed: n }
//   { op: "mkdir", paths }          -> { made: n }: directories, parents made as needed
//   { op: "list" }                  -> { files: [[path, size]], bytes }
//   { op: "run", args, cwd, env, stdin, outputs, module }
//                                   -> { code, stdout, stderr (text), stdoutBytes (Uint8Array),
//                                        instantiateMs, mem,
//                                        files: [[path, Uint8Array | null]] (outputs) }
//                                      one run of a fresh instance of the module named
//                                      module (default "clang"; in clang.wasm args[0]
//                                      picks the tool), cwd is the build directory (absolute), stdin
//                                      bytes or text (default: empty), mem
//                                      the wasm memory at the exit in bytes
//   any failure                     -> { error }
// Every answer but an error also has ms, the time the Worker took for it
// (the page's own clock may be the virtual one of clock.js).
//
// "run" is synchronous inside the Worker: messages sent meanwhile wait.

importScripts("wasi.js" + self.location.search);

const fs = new MemFS();
const modules = new Map();

const ops = {
	async load({ url, name }) {
		const resp = await fetch(url);
		if (!resp.ok)
			throw new Error(url + ": HTTP " + resp.status);
		modules.set(name, await WebAssembly.compileStreaming(resp));
		return { bytes: Number(resp.headers.get("content-length")) };
	},
	write({ files }) {
		for (const [path, data] of files)
			fs.writeFile(path, data);
		return { written: files.length };
	},
	read({ paths }) {
		return { files: paths.map((p) => [p, fs.readFile(p)]) };
	},
	remove({ paths }) {
		return { removed: paths.filter((p) => fs.remove(p)).length };
	},
	mkdir({ paths }) {
		for (const p of paths)
			fs.mkdirs(MemFS.parts(p));
		return { made: paths.length };
	},
	list() {
		const files = fs.list();
		return { files, bytes: files.reduce((s, f) => s + f[1], 0) };
	},
	run({ args, cwd, env, stdin, outputs, module }) {
		const mod = modules.get(module);
		if (!mod)
			throw new Error(module === "clang" ? "clang.wasm is not loaded" : "no module " + module + " is loaded");
		const r = wasiRun(mod, fs, { args, cwd, env, stdin: typeof stdin === "string" ? new TextEncoder().encode(stdin) : stdin });
		const dec = new TextDecoder();
		return {
			code: r.code, stdout: dec.decode(r.stdout), stderr: dec.decode(r.stderr), stdoutBytes: r.stdout,
			instantiateMs: r.instantiateMs, mem: r.mem,
			files: (outputs || []).map((p) => [p, fs.readFile(p)]),
		};
	},
};

self.onmessage = async (e) => {
	const { id, op } = e.data;
	const t = performance.now();
	try {
		const answer = await ops[op](e.data);
		answer.id = id;
		answer.ms = performance.now() - t;
		const transfer = (answer.files || []).filter((f) => f[1] instanceof Uint8Array).map((f) => f[1].buffer);
		if (answer.stdoutBytes)
			transfer.push(answer.stdoutBytes.buffer);
		self.postMessage(answer, transfer);
	} catch (err) {
		self.postMessage({ id, error: String(err && err.message || err) });
	}
};
