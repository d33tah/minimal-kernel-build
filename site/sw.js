"use strict";

// The page's service worker: an installable app that works offline after
// one visit (README.md, "Offline and installing"). Lives at the site's root
// so its scope is the whole site (on Pages: /minimal-kernel-build/).
//
// V is the deployed commit (12 hex digits, the ?v= stamp of the page's
// assets); site/stamp.py writes it when the workflow deploys. "dev" is a
// local checkout: nothing is stamped there, everything is network first.
const V = "dev";
const SHELL = "mkb-shell-" + V;
const DATA = "mkb-data-" + V;

// Fetched at install. The app shell: what index.html loads, v86 and its
// BIOSes. Then the small kernels, so hello and the minified Linux boot
// offline even if only one of them was opened (about 3.5 MB). The rest
// (DWARF shards, C source, Debian's 5 MB bzImage and initrd, the iced
// disassembler) is kept when the page first fetches it. site/check-pwa.py
// checks that every file here exists in the deployed site and that every
// asset of index.html is here.
const PRECACHE_PLAIN = ["index.html", "manifest.webmanifest", "icons/icon-192.png", "icons/icon-512.png", "icons/icon-maskable-512.png"];
const PRECACHE_STAMPED = [
	"style.css", "clock.js", "v86/libv86.js", "jumps.js", "stepper.js", "graph.js", "dwarf.js", "unwind.js", "asmnames.js",
	"dbginfo.js", "source.js", "coverage.js", "symimport.js", "livedis.js", "fastforward.js", "cpustate.js", "vars.js",
	"fnfacts.js", "settings.js", "snapshots.js", "replay.js", "reverse.js", "bookmarks.js", "clang.js", "snapworker.js", "lz4frame.js", "lz4/lz4_wasm_bg.wasm", "app.js", "pwa.js", "kernels.json", "v86/v86.wasm", "v86/seabios.bin", "v86/vgabios.bin",
];
const PRECACHE_KERNELS = [
	"kernels/hello/bzImage", "kernels/hello/symbols.json", "kernels/hello/disasm.json",
	"kernels/hello-os/bzImage", "kernels/hello-os/symbols.json", "kernels/hello-os/disasm.json",
	"kernels/minified-linux/bzImage", "kernels/minified-linux/symbols.json", "kernels/minified-linux/disasm.json",
	"kernels/minified-linux-os/bzImage", "kernels/minified-linux-os/symbols.json", "kernels/minified-linux-os/disasm.json",
];

// The URL the page fetches: withV() in app.js adds the same ?v=.
const stamped = (path) => (V === "dev" ? path : path + "?v=" + V);
const scopeUrl = (path) => new URL(path, self.registration.scope).href;

self.addEventListener("install", (event) => {
	event.waitUntil((async () => {
		// The shell must be complete or this worker does not install (the
		// old one, if any, stays). Kernels: best effort (a local checkout
		// may lack some).
		await precache(SHELL, [...PRECACHE_PLAIN, ...PRECACHE_STAMPED.map(stamped)], true);
		await precache(DATA, PRECACHE_KERNELS.map(stamped), false);
	})());
});

// Four requests at a time: the page is loading too (a burst of 40 used up
// a small server's connection queue; test_browser.py, Server).
async function precache(name, paths, required) {
	const cache = await caches.open(name);
	const queue = paths.slice();
	const one = async () => {
		while (queue.length) {
			const p = queue.shift();
			try {
				await cache.add(new Request(scopeUrl(p), { cache: "no-cache" }));
			} catch (e) {
				if (required)
					throw e;
			}
		}
	};
	await Promise.all([one(), one(), one(), one()]);
}

// A new deploy's worker drops the caches of every other deploy.
self.addEventListener("activate", (event) => {
	event.waitUntil((async () => {
		for (const k of await caches.keys())
			if (k.startsWith("mkb-") && k !== SHELL && k !== DATA)
				await caches.delete(k);
		await self.clients.claim();
	})());
});

self.addEventListener("message", (event) => {
	const m = event.data || {};
	if (m.type === "version" && event.ports[0])
		event.ports[0].postMessage(V);
	else if (m.type === "skip-waiting")
		self.skipWaiting();
});

self.addEventListener("fetch", (event) => {
	const req = event.request;
	if (req.method !== "GET")
		return;
	const url = new URL(req.url);
	if (url.origin !== self.location.origin || !req.url.startsWith(self.registration.scope))
		return;
	if (req.mode === "navigate")
		event.respondWith(navigate(event));
	else if (url.pathname.endsWith("/version.json"))
		event.respondWith(networkFirst(event));
	else
		event.respondWith(asset(event, url));
});

// The page: always the network's when online (a deploy is never hidden
// behind a cached page), the cached index.html offline.
async function navigate(event) {
	try {
		const resp = await fetch(event.request);
		const path = new URL(event.request.url).pathname;
		if (resp.ok && (path.endsWith("/") || path.endsWith("/index.html")))
			event.waitUntil(keepPage(resp.clone()));
		return resp;
	} catch (e) {
		const shell = await caches.open(SHELL);
		const page = await shell.match(scopeUrl("index.html"));
		if (page)
			return page;
		return offline(event, event.request.url);
	}
}

// Keep a fresher index.html only if it is this deploy's (its assets carry
// this worker's stamp): an index.html of the next deploy next to this
// deploy's cached files would mix the two offline.
async function keepPage(resp) {
	const text = await resp.clone().text();
	if (V !== "dev" && !text.includes("?v=" + V + "\""))
		return;
	const shell = await caches.open(SHELL);
	await shell.put(scopeUrl("index.html"), resp);
}

// This deploy's stamped files never change: cache first. Anything else
// (unstamped, or another deploy's stamp): network first, the cache offline.
async function asset(event, url) {
	if (V !== "dev" && url.searchParams.get("v") === V) {
		const hit = await caches.match(event.request);
		if (hit)
			return ranged(event.request, hit);
	}
	return networkFirst(event);
}

async function networkFirst(event) {
	const req = event.request;
	const range = req.headers.get("range");
	let resp;
	try {
		resp = await fetch(req);
	} catch (e) {
		const hit = await caches.match(req);
		return hit ? ranged(req, hit) : offline(event, req.url);
	}
	if (resp.status === 200 && !range)
		event.waitUntil(store(req, resp.clone()));
	return resp;
}

// Into this deploy's data cache, unless the origin's storage is nearly full
// (the Debian files are tens of MB): then the file is simply not kept.
async function store(req, resp) {
	try {
		const size = Number(resp.headers.get("content-length")) || 0;
		if (navigator.storage && navigator.storage.estimate) {
			const { usage, quota } = await navigator.storage.estimate();
			if (quota && usage + size > quota * 0.8)
				return;
		}
		const shell = await caches.open(SHELL);
		if (await shell.match(req))
			return;
		await (await caches.open(DATA)).put(req, resp);
	} catch (e) {
		// QuotaExceededError and the like: not kept, nothing else changes.
	}
}

// A Range request answered from a cached full response: the bytes asked for
// (v86 fetches parts of large images this way).
async function ranged(req, resp) {
	const range = req.headers.get("range");
	const m = range && /^bytes=(\d*)-(\d*)$/.exec(range.trim());
	if (!m)
		return resp;
	const buf = await resp.arrayBuffer();
	const start = m[1] === "" ? Math.max(0, buf.byteLength - Number(m[2])) : Number(m[1]);
	const end = m[1] !== "" && m[2] !== "" ? Math.min(Number(m[2]), buf.byteLength - 1) : buf.byteLength - 1;
	if (start > end || start >= buf.byteLength)
		return new Response(null, { status: 416, headers: { "Content-Range": "bytes */" + buf.byteLength } });
	return new Response(buf.slice(start, end + 1), {
		status: 206,
		headers: {
			"Content-Range": "bytes " + start + "-" + end + "/" + buf.byteLength,
			"Content-Length": String(end - start + 1),
			"Content-Type": resp.headers.get("content-type") || "application/octet-stream",
		},
	});
}

// Offline and not cached: a 503 the page recognizes (X-MKB-Offline), and a
// message so it can say which file is missing.
async function offline(event, url) {
	const path = url.startsWith(self.registration.scope) ? url.slice(self.registration.scope.length).split("?")[0] : url;
	const client = event.clientId ? await self.clients.get(event.clientId) : null;
	// version.json offline is no news (the page only compares deploys).
	if (client && path !== "version.json")
		client.postMessage({ type: "offline-miss", path });
	return new Response("Offline: " + path + " is not available offline yet.", {
		status: 503,
		statusText: "Offline",
		headers: { "Content-Type": "text/plain", "X-MKB-Offline": "1" },
	});
}
