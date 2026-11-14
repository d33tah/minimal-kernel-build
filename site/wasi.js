"use strict";
// WASI preview1 for the page's toolchain (clang/clang.wasm, a
// wasm32-wasip1 program) over an in-memory file system. Loaded by
// clangworker.js (importScripts) and by check-wasm-objects.mjs (Node,
// require) with nothing else around it.
//
//   const fs = new MemFS();
//   fs.writeFile("/src/a.c", bytes);          // parents made as needed
//   const r = wasiRun(module, fs, { args, env, cwd });
//   // r: { code, stdout, stderr (Uint8Array), mem (bytes of wasm memory), instantiateMs }
//   fs.readFile("/src/a.o");                  // what the program wrote
//
// The program sees the directory cwd of the tree twice, as the preopened
// directories "/" and cwd (an absolute path), with PWD = cwd: wasi-libc
// resolves a relative path against its own working directory "/", so
// "boot/hello.c" is cwd/boot/hello.c, an absolute path under cwd is
// found as it is, and LLVM takes PWD for the working directory (its
// DW_AT_comp_dir) because PWD and "." are the same directory. Node's
// WASI with the same two preopens (check-wasm-objects.mjs) and a native
// clang run in cwd make the same objects. Paths outside cwd are not
// reachable.
//
// Written for this toolchain after the E0 spike, which ran clang.wasm on
// @bjorn3/browser_wasi_shim 0.4.2: that shim copied a whole file on every
// write extending it (43% of a Chromium profile of a Debian compile).
// Here a file is a buffer that grows by doubling plus a length, and the
// calls a compile makes most (fd_read, fd_write, fd_seek, fd_pread) take
// and give no BigInt except the 64-bit offsets WebAssembly passes as one.
//
// fs.lookup, when set, is asked for a path the tree does not have (an
// absolute path; answers { data } for a file, { dir: true } for a
// directory, or null): check-wasm-objects.mjs uses it to see which files
// a compile opens.

const WASI_ERRNO = {
	SUCCESS: 0, ACCES: 2, BADF: 8, EXIST: 20, INVAL: 28, IO: 29, ISDIR: 31,
	NOENT: 44, NOSYS: 52, NOTDIR: 54, NOTEMPTY: 55, NOTSUP: 58, SPIPE: 70,
};
const WASI_FILETYPE = { CHAR: 2, DIR: 3, REG: 4 };

// Thrown by proc_exit to unwind out of the program's _start.
class WasiExit {
	constructor(code) { this.code = code; }
}

class MemFS {
	constructor() {
		this.root = { dir: new Map(), ino: 1, mtime: 0 };
		this.nextIno = 2;
		this.lookup = null;
	}

	node(data) {
		return data === undefined ? { dir: new Map(), ino: this.nextIno++, mtime: Date.now() }
			: { data, size: data.length, ino: this.nextIno++, mtime: Date.now() };
	}

	// Path components ("." dropped, ".." applied; none above the start).
	static parts(path) {
		const out = [];
		for (const p of path.split("/")) {
			if (p === "" || p === ".")
				continue;
			if (p === "..")
				out.pop();
			else
				out.push(p);
		}
		return out;
	}

	// The node at a list of components from dir (whose absolute path is
	// at), or null; asks lookup for what is missing.
	walk(dir, at, parts) {
		let n = dir;
		for (let i = 0; i < parts.length; i++) {
			if (!n.dir)
				return null;
			let c = n.dir.get(parts[i]);
			if (!c && this.lookup) {
				const abs = "/" + at.concat(parts.slice(0, i + 1)).join("/");
				const got = this.lookup(abs);
				if (got) {
					c = got.dir ? this.node() : this.node(got.data);
					n.dir.set(parts[i], c);
				}
			}
			if (!c)
				return null;
			n = c;
		}
		return n;
	}

	stat(path) {
		return this.walk(this.root, [], MemFS.parts(path));
	}

	mkdirs(parts) {
		let n = this.root;
		for (const p of parts) {
			let c = n.dir.get(p);
			if (!c) {
				c = this.node();
				n.dir.set(p, c);
			}
			if (!c.dir)
				throw new Error("not a directory: /" + parts.join("/"));
			n = c;
		}
		return n;
	}

	// bytes: a Uint8Array (kept, not copied) or a string (UTF-8).
	writeFile(path, bytes) {
		const parts = MemFS.parts(path);
		const name = parts.pop();
		const data = typeof bytes === "string" ? new TextEncoder().encode(bytes) : bytes;
		this.mkdirs(parts).dir.set(name, this.node(data));
	}

	// A copy of the file's bytes, or null.
	readFile(path) {
		const n = this.stat(path);
		return n && !n.dir ? n.data.slice(0, n.size) : null;
	}

	remove(path) {
		const parts = MemFS.parts(path);
		const name = parts.pop();
		const d = this.walk(this.root, [], parts);
		return !!(d && d.dir && d.dir.delete(name));
	}

	// Every file as [path, size], sorted.
	list(dir = this.root, at = "") {
		const out = [];
		for (const [name, n] of dir.dir) {
			if (n.dir)
				out.push(...this.list(n, at + "/" + name));
			else
				out.push([at + "/" + name, n.size]);
		}
		return out.sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0));
	}
}

// Write data at off into file node f, growing its buffer by doubling; a
// gap after the end reads as zeros.
function memfsWrite(f, data, off) {
	const end = off + data.length;
	if (end > f.data.length) {
		const grown = new Uint8Array(Math.max(end, 2 * f.data.length, 4096));
		grown.set(f.data.subarray(0, f.size));
		f.data = grown;
	}
	if (off > f.size)
		f.data.fill(0, f.size, off);
	f.data.set(data, off);
	if (end > f.size)
		f.size = end;
	f.mtime = Date.now();
}

// Run module's _start once over fs. opts: args (argv, argv[0] picks the
// llvm tool), env (object), cwd (absolute; also PWD), stdin (bytes).
// Returns { code, stdout, stderr, mem, instantiateMs }; a trap other
// than proc_exit throws.
function wasiRun(module, fs, opts) {
	const E = WASI_ERRNO, enc = new TextEncoder(), dec = new TextDecoder();
	const cwd = opts.cwd || "/";
	const env = Object.assign({ PWD: cwd }, opts.env || {});
	const argBytes = (opts.args || []).map((a) => enc.encode(a + "\0"));
	const envBytes = Object.entries(env).map(([k, v]) => enc.encode(k + "=" + v + "\0"));
	const out = [[], []], outLen = [0, 0];
	const stdin = opts.stdin || new Uint8Array(0);
	let stdinPos = 0;
	// fd -> { node, at (its absolute path parts), pos, append } or a
	// stream { stream: 0 | 1 | 2 }; fd 3 is the preopened "/" (cwd's node),
	// fd 4 the preopened cwd (when it is not "/").
	const at = MemFS.parts(cwd), home = fs.mkdirs(at);
	const fds = [{ stream: 0 }, { stream: 1 }, { stream: 2 }, { node: home, at, pos: 0, preopen: "/" }];
	if (at.length)
		fds.push({ node: home, at, pos: 0, preopen: "/" + at.join("/") });
	let mem = null, buf = null, u8 = null, dv = null;
	const view = () => {
		if (mem.buffer !== buf) {
			buf = mem.buffer;
			u8 = new Uint8Array(buf);
			dv = new DataView(buf);
		}
	};
	const str = (p, n) => { view(); return dec.decode(u8.subarray(p, p + n)); };
	// A u64 as two u32 (no BigInt); values here stay below 2^53.
	const put64 = (p, v) => { dv.setUint32(p, v >>> 0, true); dv.setUint32(p + 4, Math.floor(v / 4294967296), true); };
	const fileOf = (fd) => { const f = fds[fd]; return f && f.node ? f : null; };

	// Resolve path (relative to directory fd; ".." stops there) to
	// { parent, name, node, at }.
	const resolve = (fd, p, n) => {
		const d = fileOf(fd);
		if (!d || !d.node.dir)
			return null;
		const parts = MemFS.parts(str(p, n));
		const name = parts.pop();
		if (name === undefined)
			return { parent: null, name: "", node: d.node, at: d.at };
		const parent = fs.walk(d.node, d.at, parts), pat = d.at.concat(parts);
		const node = parent && parent.dir ? fs.walk(parent, pat, [name]) : null;
		return { parent, name, node, at: pat.concat(name) };
	};

	const filestat = (p, node) => {
		view();
		u8.fill(0, p, p + 64);
		put64(p, 1);
		put64(p + 8, node.ino);
		u8[p + 16] = node.dir ? WASI_FILETYPE.DIR : WASI_FILETYPE.REG;
		put64(p + 24, 1);
		put64(p + 32, node.dir ? 4096 : node.size);
		const t = node.mtime * 1e6;
		put64(p + 40, t);
		put64(p + 48, t);
		put64(p + 56, t);
	};

	const iovecs = (iovs, n) => {
		view();
		const v = [];
		for (let i = 0; i < n; i++)
			v.push([dv.getUint32(iovs + 8 * i, true), dv.getUint32(iovs + 8 * i + 4, true)]);
		return v;
	};

	const readAt = (f, iovs, n, at) => {
		const node = f.node;
		let total = 0;
		for (const [p, len] of iovecs(iovs, n)) {
			const k = Math.max(0, Math.min(len, node.size - at - total));
			u8.set(node.data.subarray(at + total, at + total + k), p);
			total += k;
			if (k < len)
				break;
		}
		return total;
	};

	const writeAt = (f, iovs, n, at) => {
		let total = 0;
		for (const [p, len] of iovecs(iovs, n)) {
			memfsWrite(f.node, u8.subarray(p, p + len), at + total);
			view();
			total += len;
		}
		return total;
	};

	const imports = {
		args_sizes_get(argc, size) {
			view();
			dv.setUint32(argc, argBytes.length, true);
			dv.setUint32(size, argBytes.reduce((s, b) => s + b.length, 0), true);
			return E.SUCCESS;
		},
		args_get(argv, p) {
			view();
			for (const [i, b] of argBytes.entries()) {
				dv.setUint32(argv + 4 * i, p, true);
				u8.set(b, p);
				p += b.length;
			}
			return E.SUCCESS;
		},
		environ_sizes_get(n, size) {
			view();
			dv.setUint32(n, envBytes.length, true);
			dv.setUint32(size, envBytes.reduce((s, b) => s + b.length, 0), true);
			return E.SUCCESS;
		},
		environ_get(envp, p) {
			view();
			for (const [i, b] of envBytes.entries()) {
				dv.setUint32(envp + 4 * i, p, true);
				u8.set(b, p);
				p += b.length;
			}
			return E.SUCCESS;
		},
		clock_time_get(id, precision, p) {
			view();
			// 0 realtime, others monotonic; in nanoseconds.
			put64(p, id === 0 ? Date.now() * 1e6 : Math.round(performance.now() * 1e6));
			return E.SUCCESS;
		},
		fd_close(fd) {
			if (!fds[fd] || fds[fd].preopen || fd < 3)
				return fds[fd] ? E.SUCCESS : E.BADF;
			fds[fd] = undefined;
			return E.SUCCESS;
		},
		fd_fdstat_get(fd, p) {
			const f = fds[fd];
			if (!f)
				return E.BADF;
			view();
			u8.fill(0, p, p + 24);
			u8[p] = f.stream !== undefined ? WASI_FILETYPE.CHAR : f.node.dir ? WASI_FILETYPE.DIR : WASI_FILETYPE.REG;
			dv.setUint16(p + 2, f.append ? 1 : 0, true);
			// Every right, base and inheriting.
			u8.fill(0xff, p + 8, p + 24);
			return E.SUCCESS;
		},
		fd_fdstat_set_flags(fd, flags) {
			const f = fds[fd];
			if (!f)
				return E.BADF;
			f.append = !!(flags & 1);
			return E.SUCCESS;
		},
		fd_filestat_get(fd, p) {
			const f = fds[fd];
			if (!f)
				return E.BADF;
			if (f.stream !== undefined) {
				view();
				u8.fill(0, p, p + 64);
				u8[p + 16] = WASI_FILETYPE.CHAR;
				return E.SUCCESS;
			}
			filestat(p, f.node);
			return E.SUCCESS;
		},
		fd_filestat_set_size(fd, size) {
			const f = fileOf(fd);
			if (!f || f.node.dir)
				return E.BADF;
			const n = Number(size);
			memfsWrite(f.node, new Uint8Array(0), n);
			f.node.size = n;
			return E.SUCCESS;
		},
		fd_filestat_set_times() {
			return E.SUCCESS;
		},
		fd_pread(fd, iovs, n, offset, nread) {
			const f = fileOf(fd);
			if (!f || f.node.dir)
				return E.BADF;
			const got = readAt(f, iovs, n, Number(offset));
			dv.setUint32(nread, got, true);
			return E.SUCCESS;
		},
		fd_pwrite(fd, iovs, n, offset, nwritten) {
			const f = fileOf(fd);
			if (!f || f.node.dir)
				return E.BADF;
			const total = writeAt(f, iovs, n, Number(offset));
			dv.setUint32(nwritten, total, true);
			return E.SUCCESS;
		},
		fd_sync() {
			return E.SUCCESS;
		},
		fd_prestat_get(fd, p) {
			const f = fds[fd];
			if (!f || !f.preopen)
				return E.BADF;
			view();
			dv.setUint32(p, 0, true);
			dv.setUint32(p + 4, enc.encode(f.preopen).length, true);
			return E.SUCCESS;
		},
		fd_prestat_dir_name(fd, p, n) {
			const f = fds[fd];
			if (!f || !f.preopen)
				return E.BADF;
			view();
			u8.set(enc.encode(f.preopen).subarray(0, n), p);
			return E.SUCCESS;
		},
		fd_read(fd, iovs, n, nread) {
			const f = fds[fd];
			if (!f)
				return E.BADF;
			if (f.stream !== undefined) {
				view();
				let total = 0;
				for (const [p, len] of iovecs(iovs, n)) {
					const k = Math.min(len, stdin.length - stdinPos);
					u8.set(stdin.subarray(stdinPos, stdinPos + k), p);
					stdinPos += k;
					total += k;
				}
				dv.setUint32(nread, total, true);
				return E.SUCCESS;
			}
			if (f.node.dir)
				return E.ISDIR;
			const got = readAt(f, iovs, n, f.pos);
			f.pos += got;
			dv.setUint32(nread, got, true);
			return E.SUCCESS;
		},
		fd_readdir(fd, p, len, cookie, used) {
			const f = fileOf(fd);
			if (!f || !f.node.dir)
				return E.NOTDIR;
			view();
			const entries = [[".", f.node], ["..", f.node], ...f.node.dir];
			let o = 0;
			for (let i = Number(cookie); i < entries.length && o < len; i++) {
				const [name, node] = entries[i];
				const nb = enc.encode(name);
				const ent = new Uint8Array(24 + nb.length);
				const edv = new DataView(ent.buffer);
				edv.setUint32(0, i + 1, true);
				edv.setUint32(8, node.ino, true);
				edv.setUint32(16, nb.length, true);
				ent[20] = node.dir ? WASI_FILETYPE.DIR : WASI_FILETYPE.REG;
				ent.set(nb, 24);
				const k = Math.min(ent.length, len - o);
				u8.set(ent.subarray(0, k), p + o);
				o += k;
			}
			dv.setUint32(used, o, true);
			return E.SUCCESS;
		},
		fd_seek(fd, offset, whence, p) {
			const f = fds[fd];
			if (!f)
				return E.BADF;
			if (f.stream !== undefined)
				return E.SPIPE;
			const to = Number(offset) + (whence === 1 ? f.pos : whence === 2 ? f.node.size || 0 : 0);
			if (to < 0)
				return E.INVAL;
			f.pos = to;
			view();
			put64(p, to);
			return E.SUCCESS;
		},
		fd_write(fd, iovs, n, nwritten) {
			const f = fds[fd];
			if (!f)
				return E.BADF;
			view();
			let total = 0;
			if (f.stream !== undefined) {
				if (f.stream === 0)
					return E.BADF;
				for (const [p, len] of iovecs(iovs, n)) {
					out[f.stream - 1].push(u8.slice(p, p + len));
					outLen[f.stream - 1] += len;
					total += len;
				}
			} else {
				if (f.node.dir)
					return E.ISDIR;
				if (f.append)
					f.pos = f.node.size;
				total = writeAt(f, iovs, n, f.pos);
				f.pos += total;
			}
			dv.setUint32(nwritten, total, true);
			return E.SUCCESS;
		},
		path_create_directory(fd, p, n) {
			const r = resolve(fd, p, n);
			if (!r || !r.parent || !r.parent.dir)
				return E.NOENT;
			if (r.node)
				return E.EXIST;
			r.parent.dir.set(r.name, fs.node());
			return E.SUCCESS;
		},
		path_filestat_get(fd, flags, p, n, st) {
			const r = resolve(fd, p, n);
			if (!r || !r.node)
				return E.NOENT;
			filestat(st, r.node);
			return E.SUCCESS;
		},
		path_link() {
			return E.NOTSUP;
		},
		path_open(fd, dirflags, p, n, oflags, rightsBase, rightsInh, fdflags, opened) {
			const r = resolve(fd, p, n);
			if (!r || (!r.node && (!r.parent || !r.parent.dir)))
				return E.NOENT;
			let node = r.node;
			if (node && (oflags & 4))
				return E.EXIST;
			if (!node) {
				if (!(oflags & 1))
					return E.NOENT;
				node = fs.node(new Uint8Array(0));
				r.parent.dir.set(r.name, node);
			}
			if ((oflags & 2) && !node.dir)
				return E.NOTDIR;
			// Opened for writing (the fd_write right, 1 << 6).
			if (node.dir && (rightsBase & 64n))
				return E.ISDIR;
			if (oflags & 8) {
				if (node.dir)
					return E.ISDIR;
				node.size = 0;
			}
			let i = 4;
			while (fds[i])
				i++;
			fds[i] = { node, at: r.at, pos: 0, append: !!(fdflags & 1) };
			view();
			dv.setUint32(opened, i, true);
			return E.SUCCESS;
		},
		path_readlink() {
			return E.INVAL;
		},
		path_remove_directory(fd, p, n) {
			const r = resolve(fd, p, n);
			if (!r || !r.node)
				return E.NOENT;
			if (!r.node.dir)
				return E.NOTDIR;
			if (r.node.dir.size)
				return E.NOTEMPTY;
			r.parent.dir.delete(r.name);
			return E.SUCCESS;
		},
		path_rename(fd, p, n, fd2, p2, n2) {
			const a = resolve(fd, p, n), b = resolve(fd2, p2, n2);
			if (!a || !a.node || !a.parent)
				return E.NOENT;
			if (!b || !b.parent || !b.parent.dir)
				return E.NOENT;
			a.parent.dir.delete(a.name);
			b.parent.dir.set(b.name, a.node);
			return E.SUCCESS;
		},
		path_symlink() {
			return E.NOTSUP;
		},
		path_unlink_file(fd, p, n) {
			const r = resolve(fd, p, n);
			if (!r || !r.node)
				return E.NOENT;
			if (r.node.dir)
				return E.ISDIR;
			r.parent.dir.delete(r.name);
			return E.SUCCESS;
		},
		poll_oneoff() {
			return E.NOTSUP;
		},
		proc_exit(code) {
			throw new WasiExit(code);
		},
		random_get(p, n) {
			view();
			for (let o = 0; o < n; o += 65536)
				crypto.getRandomValues(u8.subarray(p + o, p + Math.min(n, o + 65536)));
			return E.SUCCESS;
		},
	};

	const t = performance.now();
	// A call this layer does not have answers ENOSYS (a program linked
	// against more of WASI still instantiates).
	const wasi = new Proxy(imports, { get: (o, k) => o[k] || (() => E.NOSYS) });
	const inst = new WebAssembly.Instance(module, { wasi_snapshot_preview1: wasi });
	const instantiateMs = performance.now() - t;
	mem = inst.exports.memory;
	let code = 0;
	try {
		inst.exports._start();
	} catch (e) {
		if (!(e instanceof WasiExit))
			throw e;
		code = e.code;
	}
	const join = (i) => {
		const all = new Uint8Array(outLen[i]);
		let o = 0;
		for (const c of out[i]) {
			all.set(c, o);
			o += c.length;
		}
		return all;
	};
	return { code, stdout: join(0), stderr: join(1), mem: mem.buffer.byteLength, instantiateMs };
}

if (typeof module !== "undefined")
	module.exports = { MemFS, wasiRun, WASI_ERRNO };
