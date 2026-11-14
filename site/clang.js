"use strict";
// The page's toolchain (plan E0c): clang, ld.lld and the llvm tools of
// clang/clang.wasm in a Worker (clangworker.js) over an in-memory file
// system (wasi.js). The editor and the in-page rebuild call this:
//
//   const cc = new ClangWorker();
//   await cc.load();                                  // fetch + compile clang.wasm
//   await cc.write([["/b/hello/boot/hello.c", text]]); // sources and headers
//   const r = await cc.run(["clang", "-c", "-o", "boot/hello.o", "boot/hello.c"],
//                          { cwd: "/b/hello", outputs: ["/b/hello/boot/hello.o"] });
//   // r.code, r.stdout, r.stderr (text), r.stdoutBytes, r.ms, r.mem,
//   // r.files [[path, Uint8Array]]
//   // A kernel build's host tool (gen-host-tools.py), a wasm32-wasip1
//   // module of its own, on the same files:
//   await cc.load("tools/kallsyms.wasm", "kallsyms");
//   await cc.run(["scripts/kallsyms", "--base-relative", "vmlinux.syms"],
//                { cwd: "/b/linux", module: "kallsyms" });
//   cc.terminate();
//
// Requests run in order; a failure rejects with the Worker's message.
class ClangWorker {
	constructor(url = withV("clangworker.js")) {
		this.worker = new Worker(url);
		this.pending = new Map();
		this.next = 0;
		this.onprogress = null;
		this.worker.onmessage = (e) => {
			// A long request (genworker.js) says how far it is.
			if (e.data.progress !== undefined) {
				if (this.onprogress)
					this.onprogress(e.data);
				return;
			}
			const p = this.pending.get(e.data.id);
			this.pending.delete(e.data.id);
			if (e.data.error)
				p.reject(new Error(e.data.error));
			else
				p.resolve(e.data);
		};
		// A Worker that fails to start (or throws outside a request) answers
		// nothing: every request waiting for it fails.
		this.worker.onerror = (e) => {
			e.preventDefault();
			this.fail(new Error("the Worker failed: " + (e.message || "no message")));
		};
	}

	fail(err) {
		for (const p of this.pending.values())
			p.reject(err);
		this.pending.clear();
	}

	request(msg, transfer = []) {
		const id = ++this.next;
		return new Promise((resolve, reject) => {
			this.pending.set(id, { resolve, reject });
			this.worker.postMessage(Object.assign({ id }, msg), transfer);
		});
	}

	// clang.wasm is pinned by SHA-256 (gen-clang-wasm.sh), so its URL has no
	// deploy stamp and stays in the HTTP cache across deploys. name: the
	// module's name for run's module option (another module, e.g. a host
	// tool, under its own name; clang.wasm is "clang").
	load(url = "clang/clang.wasm", name = "clang") {
		return this.request({ op: "load", url: new URL(url, location.href).href, name });
	}

	// files: [[path, Uint8Array | string]]; the arrays' buffers move to the
	// Worker (transferred), so the caller must not use them afterwards.
	write(files) {
		const bufs = new Set(files.filter((f) => f[1] instanceof Uint8Array).map((f) => f[1].buffer));
		return this.request({ op: "write", files }, [...bufs]);
	}

	read(paths) {
		return this.request({ op: "read", paths });
	}

	remove(paths) {
		return this.request({ op: "remove", paths });
	}

	// Directories (with their parents) a tool writes into.
	mkdir(paths) {
		return this.request({ op: "mkdir", paths });
	}

	list() {
		return this.request({ op: "list" });
	}

	// One tool run: args[0] is the tool (clang, ld.lld, llvm-objcopy, ...),
	// cwd the absolute build directory, stdin its input (bytes or text);
	// outputs: paths to send back; module: the loaded module that runs it.
	run(args, { cwd = "/", env = {}, stdin = "", outputs = [], module = "clang" } = {}) {
		return this.request({ op: "run", args, cwd, env, stdin, outputs, module });
	}

	terminate() {
		this.worker.terminate();
		this.fail(new Error("terminated"));
	}
}
