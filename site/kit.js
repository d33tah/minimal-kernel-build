"use strict";
// A kernel's build kit (plan E3, made in CI by gen-kit.py): build the
// kernel again in the page with clang.wasm and the build's host tools in
// wasm (clang.js's ClangWorker), from CI's prebuilt -g objects or from the
// sources. The result is CI's bytes: vmlinux (or hello's kernel.elf) and
// the bzImage.
//
//   const src = kitSource("kits/");                  // where the kits are
//   const kit = await src.kit("minified-linux");     // kits/minified-linux.json
//   const cc = new ClangWorker();
//   await cc.load();                                 // clang.wasm
//   const r = await kitBuild(cc, kit, src, { compile: "all" });
//   // r.products [[path, sha256, size]], r.objects (compiled), r.steps, r.ms
//
// compile: false (default) links CI's objects; "all" compiles every object
// from the kit's sources first; "changed" compiles the objects that
// include a file of edits (kitUsers). edits: { path: bytes or text } in
// place of the kit's files. keep: paths whose bytes come back in
// r.files. onstep(event) says how far the build is: { kind: "fetch", done,
// of } after each batch of blobs, { kind: "compile", out, done, of } before
// an object is compiled and { kind: "object", ... } after it, { kind:
// "step", tool, cmd, done, of } after a step (rebuild.js shows them). For
// the build log, each compile and step also says { kind: "command", argv,
// stdin } before it runs and { kind: "output", code, ms, stdout, stderr,
// to, bytes } after it (also when it failed): to is the file its stdout
// went to, "" for the next step, null when stdout is in the event.
// The kit's steps run in order: clang.wasm's tools, a module of
// the kit (a host tool), and three run here: sed (kitSed, a GNU sed
// subset), cat, and cat-size (cat, then the inputs' total size as 4
// little-endian bytes: kbuild's size_append). A step's stdout goes to a
// file or into a variable (the next step's stdin, or a value an argument
// has: the shell's 0x$(nm | sed)).
//
// Loaded by index.html (the page) and by test_kit.mjs (Node, require).

import { realNow } from "./clock.js";

const KIT_FORMAT = 1;

// Real time: on the page, performance.now() is clock.js's virtual clock.
const kitNow = realNow;

async function kitHash(bytes) {
	const h = new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
	return Array.from(h, (x) => x.toString(16).padStart(2, "0")).join("");
}

// Bytes as a string of one char per byte, and back (sed works on text,
// the files are bytes).
function kitLatin1(bytes) {
	let s = "";
	for (let i = 0; i < bytes.length; i += 0x8000)
		s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
	return s;
}

function kitBytes(text) {
	const b = new Uint8Array(text.length);
	for (let i = 0; i < text.length; i++)
		b[i] = text.charCodeAt(i);
	return b;
}

function kitConcat(parts) {
	const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
	let at = 0;
	for (const p of parts) {
		out.set(p, at);
		at += p.length;
	}
	return out;
}

// fetch(url), a server error tried again (after 0.5, 1 and 2 s): GitHub
// Pages answered 503 to one of a build's parallel blob fetches, and to
// kits/index.json right after a deploy. The last answer is returned, ok or
// not.
async function kitFetch(url) {
	for (let wait = 500; ; wait *= 2) {
		const r = await fetch(url);
		if (r.ok || r.status < 500 || wait > 2000)
			return r;
		await new Promise((ok) => setTimeout(ok, wait));
	}
}

// Where the kits are: base/NAME.json and base/blobs/SHA256.gz (a file or
// object, gzipped) and SHA256.wasm (a module). Every blob is checked
// against its SHA-256.
function kitSource(base) {
	const at = (p) => new URL(p, new URL(base, location.href)).href;
	const fetchOk = async (url) => {
		const r = await kitFetch(url);
		if (!r.ok)
			throw new Error(url + ": HTTP " + r.status);
		return r;
	};
	return {
		async kit(name) {
			const k = await (await fetchOk(at(name + ".json"))).json();
			if (k.format !== KIT_FORMAT)
				throw new Error("kit " + name + ": format " + k.format + ", this page reads " + KIT_FORMAT);
			return k;
		},
		async get(sha) {
			let b = new Uint8Array(await (await fetchOk(at("blobs/" + sha + ".gz"))).arrayBuffer());
			// A server may have unpacked it already (Content-Encoding).
			if (b[0] === 0x1f && b[1] === 0x8b)
				b = new Uint8Array(await new Response(new Blob([b]).stream().pipeThrough(new DecompressionStream("gzip"))).arrayBuffer());
			const h = await kitHash(b);
			if (h !== sha)
				throw new Error("blobs/" + sha + ".gz: its bytes have SHA-256 " + h);
			return b;
		},
		moduleUrl(sha) {
			return at("blobs/" + sha + ".wasm");
		},
	};
}

// --- sed: the subset kbuild and hello's Makefile use ---

// A sed regular expression (BRE, or ERE with -E) as a JavaScript one.
function kitSedRegex(src, ere) {
	const classes = {
		alpha: "a-zA-Z", digit: "0-9", alnum: "a-zA-Z0-9", upper: "A-Z", lower: "a-z",
		space: " \\t\\n\\r\\f\\v", blank: " \\t", xdigit: "0-9A-Fa-f", punct: "!-\\/:-@\\[-`{-~",
	};
	let out = "";
	// In a BRE, ^ and a leading * are special only at the start of the
	// expression or of a group or alternative.
	let start = true;
	let i = 0;
	while (i < src.length) {
		const c = src[i++];
		const wasStart = start;
		start = false;
		if (c === "\\") {
			// kitSedPart keeps a backslash with the char after it.
			const n = src[i++];
			if (!ere && "(|".includes(n)) {
				out += n;
				start = true;
			} else if (!ere && "){}+?".includes(n)) {
				out += n;
			} else if (n === "<" || n === ">") {
				out += "\\b";
			} else if (/[1-9wWsSbnt]/.test(n) || !/\w/.test(n)) {
				// The same in JavaScript (an escaped delimiter or other
				// punctuation is that char).
				out += "\\" + n;
			} else {
				throw new Error("sed: unsupported escape \\" + n + " in " + src);
			}
		} else if (c === "[") {
			let set = "[";
			if (src[i] === "^") {
				set += "^";
				i++;
			}
			if (src[i] === "]") {
				set += "\\]";
				i++;
			}
			for (; i < src.length && src[i] !== "]"; i++) {
				if (src[i] === "[" && src[i + 1] === ":") {
					const e = src.indexOf(":]", i + 2);
					const cls = e < 0 ? undefined : classes[src.slice(i + 2, e)];
					if (cls === undefined)
						throw new Error("sed: unsupported bracket expression in " + src);
					set += cls;
					i = e + 1;
				} else {
					// Literal in a POSIX bracket expression, special in JavaScript's.
					set += "\\[]".includes(src[i]) ? "\\" + src[i] : src[i];
				}
			}
			if (i >= src.length)
				throw new Error("sed: unterminated [ in " + src);
			out += set + "]";
			i++;
		} else if (ere) {
			out += c;
			start = c === "(" || c === "|";
		} else if ("(){}|+?".includes(c) || c === "*" && wasStart || c === "^" && !wasStart) {
			out += "\\" + c;
		} else if (c === "$" && !(i === src.length || src.startsWith("\\)", i) || src.startsWith("\\|", i))) {
			out += "\\$";
		} else {
			out += c;
			start = c === "^" && wasStart;
		}
	}
	return new RegExp(out, "g");
}

// The text between delimiters from s[i] on (a backslash keeps the next
// char with it); returns [text, index after the closing delimiter].
function kitSedPart(s, i, delim) {
	let t = "";
	for (; i < s.length && s[i] !== delim; i++) {
		if (s[i] === "\\" && i + 1 < s.length) {
			t += s[i] + s[i + 1];
			i++;
		} else if (s[i] === "\n") {
			throw new Error("sed: newline inside " + delim + "..." + delim);
		} else {
			t += s[i];
		}
	}
	if (i >= s.length)
		throw new Error("sed: unterminated `s' command");
	return [t, i + 1];
}

function kitSedReplacement(t, delim) {
	const parts = [];
	for (let i = 0; i < t.length; i++) {
		const c = t[i];
		if (c === "\\") {
			const n = t[++i];
			if (/[0-9]/.test(n))
				parts.push(Number(n));
			else
				parts.push(n === "n" ? "\n" : n === "t" ? "\t" : n === "&" ? "&" : n === delim ? delim : n);
		} else if (c === "&") {
			parts.push(0);
		} else {
			parts.push(c);
		}
	}
	return (m) => parts.map((p) => typeof p === "number" ? (m[p] ?? "") : p).join("");
}

// One script: commands separated by newlines or ';', each with an
// optional address (a line number, $, or /re/) and s, p or d.
function kitSedParse(script, ere) {
	const cmds = [];
	let i = 0;
	while (i < script.length) {
		const c0 = script[i];
		if (c0 === ";" || c0 === "\n" || c0 === " " || c0 === "\t") {
			i++;
			continue;
		}
		const cmd = {};
		let m;
		if ((m = /^[0-9]+/.exec(script.slice(i)))) {
			cmd.line = Number(m[0]);
			i += m[0].length;
		} else if (c0 === "$") {
			cmd.last = true;
			i++;
		} else if (c0 === "/") {
			const [re, j] = kitSedPart(script, i + 1, "/");
			cmd.re = kitSedRegex(re, ere);
			i = j;
		}
		const op = script[i++];
		if (op === "s") {
			const delim = script[i];
			const [re, j] = kitSedPart(script, i + 1, delim);
			const [rep, k] = kitSedPart(script, j, delim);
			i = k;
			cmd.op = "s";
			cmd.regex = kitSedRegex(re, ere);
			cmd.repl = kitSedReplacement(rep, delim);
			cmd.nth = 1;
			for (; i < script.length && !";\n}".includes(script[i]); i++) {
				const f = script[i];
				if (f === "g")
					cmd.global = true;
				else if (f === "p")
					cmd.print = true;
				else if (/[0-9]/.test(f))
					cmd.nth = Number(/^[0-9]+/.exec(script.slice(i))[0]), i += String(cmd.nth).length - 1;
				else if (f !== " ")
					throw new Error("sed: unsupported flag " + f + " in " + script);
			}
		} else if (op === "p" || op === "d") {
			cmd.op = op;
		} else {
			throw new Error("sed: unsupported command " + JSON.stringify(op || "") + " in " + JSON.stringify(script));
		}
		cmds.push(cmd);
	}
	return cmds;
}

// sed's arguments: -n, -e SCRIPT, -E/-r, the script as the first argument
// without -e, then file names.
function kitSedArgs(args) {
	let quiet = false, ere = false;
	const scripts = [], names = [];
	for (let i = 0; i < args.length; i++) {
		const a = args[i];
		if (a === "--quiet" || a === "--silent") {
			quiet = true;
		} else if (a === "--regexp-extended") {
			ere = true;
		} else if (a.length > 1 && a[0] === "-" && a[1] !== "-") {
			for (let j = 1; j < a.length; j++) {
				if (a[j] === "n")
					quiet = true;
				else if (a[j] === "E" || a[j] === "r")
					ere = true;
				else if (a[j] === "e") {
					scripts.push(j + 1 < a.length ? a.slice(j + 1) : args[++i]);
					break;
				} else {
					throw new Error("sed: unsupported option -" + a[j]);
				}
			}
		} else if (!scripts.length) {
			scripts.push(a);
		} else {
			names.push(a);
		}
	}
	if (!scripts.length)
		throw new Error("sed: no script");
	return { quiet, ere, scripts, names };
}

// sed ARGS on input (bytes: stdin, or its file arguments' bytes one after
// the other). Returns the output bytes.
function kitSed(args, input) {
	const { quiet, ere, scripts } = kitSedArgs(args);
	const cmds = kitSedParse(scripts.join("\n"), ere);
	const text = kitLatin1(input || new Uint8Array(0));
	const lines = text === "" ? [] : text.split("\n");
	const lastNl = text.endsWith("\n");
	if (lastNl)
		lines.pop();
	let out = "";
	// As GNU sed: a last line without a newline is written without one,
	// and gets it when more output follows.
	let missing = false;
	const emit = (ps, last) => {
		out += (missing ? "\n" : "") + ps;
		missing = last && !lastNl;
		if (!missing)
			out += "\n";
	};
	lines.forEach((line, n) => {
		const last = n === lines.length - 1;
		let ps = line;
		let del = false;
		for (const c of cmds) {
			if (c.line !== undefined && c.line !== n + 1 || c.last && !last)
				continue;
			if (c.re) {
				c.re.lastIndex = 0;
				if (!c.re.test(ps))
					continue;
			}
			if (c.op === "p") {
				emit(ps, last);
			} else if (c.op === "d") {
				del = true;
				break;
			} else {
				let k = 0, done = false;
				c.regex.lastIndex = 0;
				ps = ps.replace(c.regex, (...m) => {
					k++;
					const groups = m.slice(0, -2);
					if (k < c.nth || done && !c.global)
						return m[0];
					done = true;
					return c.repl(groups);
				});
				if (done && c.print)
					emit(ps, last);
			}
		}
		if (!quiet && !del)
			emit(ps, last);
	});
	return kitBytes(out);
}

// --- the build ---

// The words of one job line of clang -### ("a" "b\"c": backslash escapes).
function kitJobWords(line) {
	const words = [];
	const re = /"((?:[^"\\]|\\.)*)"/g;
	let m;
	while ((m = re.exec(line)))
		words.push(m[1].replace(/\\(.)/g, "$1"));
	return words;
}

// One clang command. A .S file goes through two jobs (preprocess, then
// the assembler), and clang.wasm cannot start the second as a process
// (WASI has none): clang -### lists the jobs and each runs as clang -cc1
// or clang -cc1as, the arguments the driver would give them.
async function kitClang(cc, argv, opts) {
	if (!argv.some((a) => /\.S$/.test(a)))
		return cc.run(argv, opts);
	const p = await cc.run([argv[0], "-###"].concat(argv.slice(1)), Object.assign({}, opts, { outputs: [] }));
	if (p.code !== 0)
		return p;
	const jobs = p.stderr.split("\n").filter((l) => l.startsWith(" \"")).map(kitJobWords);
	let r = p;
	for (const j of jobs) {
		// clang.wasm (llvm-driver) writes the executable as "" followed by
		// the tool's name: "" "clang" "-cc1" ...
		r = await cc.run(j[0] === "" ? j.slice(1) : ["clang"].concat(j.slice(1)), Object.assign({}, opts, { outputs: [] }));
		if (r.code !== 0)
			return r;
	}
	const tmp = opts.env.TMPDIR + "/";
	await cc.remove([...new Set(jobs.flat().filter((a) => a.startsWith(tmp)))]);
	r.files = (await cc.read(opts.outputs || [])).files;
	return r;
}

// Which objects include a file: { path: [object paths] } over the
// objects' dependencies (kbuild's deps_ lines; the source first).
function kitUsers(kit) {
	const users = new Map();
	for (const o of kit.objects)
		for (const d of o.deps) {
			const p = kit.files[d][0];
			if (!users.has(p))
				users.set(p, []);
			users.get(p).push(o.out);
		}
	return users;
}

async function kitBuild(cc, kit, src, opts = {}) {
	const t0 = kitNow();
	const dir = kit.dir;
	const abs = (p) => p.startsWith("/") ? p : dir + "/" + p;
	const compile = opts.compile || false;
	const say = (e) => {
		if (opts.onstep)
			opts.onstep(e);
	};
	const edits = new Map(Object.entries(opts.edits || {}).map(([p, v]) => [p, typeof v === "string" ? new TextEncoder().encode(v) : v]));
	const fileSha = new Map(kit.files.map((f) => [f[0], f[1]]));
	for (const p of edits.keys())
		if (!fileSha.has(p))
			throw new Error("kit " + kit.name + ": " + p + " is not a file of the kit");
	const users = kitUsers(kit);
	let recompile = new Set();
	if (compile === "all")
		recompile = new Set(kit.objects.map((o) => o.out));
	else if (compile === "changed")
		for (const p of edits.keys())
			for (const o of users.get(p) || [])
				recompile.add(o);
	else if (compile)
		throw new Error("kitBuild: compile is false, \"all\" or \"changed\"");
	const objects = kit.objects.filter((o) => recompile.has(o.out));
	const res = { name: kit.name, objects: [], steps: [], products: [], files: [], fetched: 0, fetchedBytes: 0 };

	// The files: what the steps read, plus the dependencies of the objects
	// compiled here; the other objects come prebuilt.
	const want = new Set();
	for (const s of kit.steps)
		for (const p of s.inputs)
			if (fileSha.has(p))
				want.add(p);
	for (const o of objects)
		for (const d of o.deps)
			want.add(kit.files[d][0]);
	const blobs = [];
	for (const p of want)
		if (!edits.has(p))
			blobs.push([abs(p), fileSha.get(p)]);
	for (const o of kit.objects)
		if (!recompile.has(o.out))
			blobs.push([abs(o.out), o.sha256]);
	for (let i = 0; i < blobs.length; i += 32) {
		const part = blobs.slice(i, i + 32);
		const data = await Promise.all(part.map(([, h]) => src.get(h)));
		res.fetched += data.length;
		res.fetchedBytes += data.reduce((n, b) => n + b.length, 0);
		await cc.write(part.map(([p], k) => [p, data[k]]));
		if (opts.onstep)
			opts.onstep({ kind: "fetch", done: res.fetched, of: blobs.length });
	}
	await cc.write([...edits].map(([p, b]) => [abs(p), b.slice()]));
	res.fetchMs = kitNow() - t0;

	// The host tools.
	const loaded = new Set();
	const load = async (name) => {
		if (name === "clang" || loaded.has(name))
			return;
		const m = kit.modules[name];
		if (!m)
			throw new Error("kit " + kit.name + ": no module " + name);
		await cc.load(src.moduleUrl(m.sha256), name);
		loaded.add(name);
	};
	const fail = (what, r) => {
		throw new Error(what + " exited with " + r.code + (r.stderr ? ": " + r.stderr.slice(0, 2000) : ""));
	};
	// clang compiles a .S through a temporary file (TMPDIR, else /tmp,
	// which the program would see in the build directory).
	const env = { TMPDIR: dir + "/.kit-tmp" };
	const dirs = new Set([env.TMPDIR]);
	for (const o of objects)
		dirs.add(abs(o.out).replace(/\/[^/]*$/, ""));
	for (const s of kit.steps)
		for (const d of s.mkdir)
			dirs.add(abs(d));
	await cc.mkdir([...dirs]);

	const t1 = kitNow();
	for (const o of objects) {
		const tool = o.argv[0] === "nasm" ? "nasm" : "clang";
		if (opts.onstep)
			opts.onstep({ kind: "compile", out: o.out, done: res.objects.length, of: objects.length });
		await load(tool);
		const ro = { cwd: dir, env, module: tool, outputs: [abs(o.out)] };
		say({ kind: "command", argv: o.argv });
		const tc = kitNow();
		const r = tool === "clang" ? await kitClang(cc, o.argv, ro) : await cc.run(o.argv, ro);
		say({ kind: "output", code: r.code, ms: kitNow() - tc, stdout: r.stdout || "", stderr: r.stderr || "" });
		if (r.code !== 0)
			fail(o.out + ": " + o.argv[0], r);
		const b = r.files[0][1];
		res.objects.push({ out: o.out, sha256: await kitHash(b), want: o.sha256, size: b.length, ms: r.ms });
		if (opts.onstep)
			opts.onstep({ kind: "object", out: o.out, done: res.objects.length, of: objects.length });
	}
	res.compileMs = kitNow() - t1;

	const t2 = kitNow();
	const vars = new Map();
	const read = async (paths) => (await cc.read(paths.map(abs))).files.map(([p, b]) => {
		if (!b)
			throw new Error(p + ": no such file");
		return b;
	});
	for (const [i, s] of kit.steps.entries()) {
		const ts = kitNow();
		if (s.fresh.length)
			await cc.remove(s.fresh.map(abs));
		const argv = s.argv.map((a) => typeof a === "string" ? a : a.pre + kitLatin1(vars.get(a.var)).replace(/\n+$/, "") + a.post);
		const stdin = s.stdin ? vars.get(s.stdin) : undefined;
		say({ kind: "command", argv, stdin: !!s.stdin });
		let out, r = { code: 0, stdout: "", stderr: "" };
		if (s.tool === "sed") {
			const names = kitSedArgs(argv.slice(1)).names;
			out = kitSed(argv.slice(1), names.length ? kitConcat(await read(names)) : stdin);
		} else if (s.tool === "cat" || s.tool === "cat-size") {
			const parts = argv.length > 1 ? await read(argv.slice(1)) : [stdin || new Uint8Array(0)];
			if (s.tool === "cat-size") {
				const n = parts.reduce((k, p) => k + p.length, 0);
				parts.push(new Uint8Array([n & 255, n >>> 8 & 255, n >>> 16 & 255, n >>> 24 & 255]));
			}
			out = kitConcat(parts);
		} else {
			await load(s.tool);
			const ro = { cwd: dir, env, module: s.tool, stdin: stdin || "" };
			r = argv[0] === "clang" ? await kitClang(cc, argv, ro) : await cc.run(argv, ro);
			out = r.stdoutBytes;
		}
		// Where its stdout went: a file, the next step (a pipe or the
		// shell's $(...)), or the log.
		const to = s.stdout && s.stdout.file ? s.stdout.file : s.stdout && s.stdout.var ? "" : null;
		const text = r.stdout || (out && !r.stdoutBytes ? kitLatin1(out) : "");
		say({ kind: "output", code: r.code, ms: kitNow() - ts, stdout: to === null ? text : "", stderr: r.stderr || "",
			to, bytes: out ? out.length : 0 });
		if (r.code !== 0)
			fail("step " + (i + 1) + " (" + argv.join(" ").slice(0, 200) + ")", r);
		if (s.stdout && s.stdout.file)
			await cc.write([[abs(s.stdout.file), out]]);
		else if (s.stdout && s.stdout.var)
			vars.set(s.stdout.var, out);
		const step = { tool: s.tool, ms: kitNow() - ts };
		if (opts.verify !== false && s.outputs.length) {
			const got = await read(s.outputs.map((o) => o[0]));
			step.outputs = [];
			for (const [k, [p, h]] of s.outputs.entries())
				step.outputs.push([p, await kitHash(got[k]), h]);
		}
		res.steps.push(step);
		if (opts.onstep)
			opts.onstep({ kind: "step", tool: s.tool, cmd: argv[0].replace(/^.*\//, ""), done: i + 1, of: kit.steps.length });
	}
	res.linkMs = kitNow() - t2;

	const prods = await read(kit.products.map((p) => p[0]));
	for (const [k, [p, h]] of kit.products.entries())
		res.products.push([p, await kitHash(prods[k]), prods[k].length, h]);
	for (const p of opts.keep || [])
		res.files.push([p, (await read([p]))[0]]);
	res.ms = kitNow() - t0;
	return res;
}

export { KIT_FORMAT, kitNow, kitHash, kitLatin1, kitBytes, kitFetch, kitSource, kitSedArgs, kitSed, kitUsers,
	kitBuild };
