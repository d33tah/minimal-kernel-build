"use strict";

// The page's side of the web app (sw.js is the service worker): register
// it, say when a file is not available offline, and show a notice when a
// newer deploy is live, so old and new files are never mixed silently.
// README.md, "Offline and installing".

import { VERSION } from "./util.js";
import { api } from "./page.js";
import { MHEAD } from "./machtab.js";

const PWA = { reg: null, reloading: false, newer: null, misses: [] };
const PWA_CHECK_MS = 5 * 60 * 1000;

// The notes above the Machine tab's screen (machinepanel.jsx).
function pwaNotes() {
	MHEAD.pwaNewer = PWA.newer;
	MHEAD.pwaMisses = PWA.misses.slice();
	MHEAD.changed();
	// The menu toggle (debugui.js) shows the new version on every tab.
	api.show.renderRunBadgePwa();
}

// A file the service worker could not get (offline, never cached).
function pwaOfflineMiss(path) {
	if (!PWA.misses.includes(path))
		PWA.misses.push(path);
	pwaNotes();
}

// Ask a service worker which deploy it is (sw.js answers its V).
function pwaWorkerVersion(w) {
	return new Promise((resolve) => {
		const ch = new MessageChannel();
		const t = setTimeout(() => resolve(null), 3000);
		ch.port1.onmessage = (e) => { clearTimeout(t); resolve(e.data); };
		w.postMessage({ type: "version" }, [ch.port2]);
	});
}

function pwaNewer(what) {
	if (PWA.newer === what)
		return;
	PWA.newer = what;
	pwaNotes();
}

// version.json is never cached (sw.js: network first): another commit there
// is a newer deploy than this page (VERSION: the ?v= stamp of its scripts).
async function pwaCheck() {
	if (!VERSION || PWA.reloading)
		return;
	try {
		const resp = await fetch("version.json", { cache: "no-store" });
		const v = resp.ok ? await resp.json() : null;
		if (v && v.sha && v.sha.slice(0, VERSION.length) !== VERSION)
			pwaNewer("build " + v.sha.slice(0, 12));
	} catch (e) {
		// Offline: nothing to compare.
	}
	if (PWA.reg)
		PWA.reg.update().catch(() => null);
}

// A newly installed worker: the page's own deploy (this page came from the
// network, a step ahead of the old worker) takes over at once; another one
// waits for the notice's Reload.
async function pwaInstalled(w) {
	const v = await pwaWorkerVersion(w);
	if (!navigator.serviceWorker.controller || v === VERSION || (!VERSION && v === "dev"))
		w.postMessage({ type: "skip-waiting" });
	else if (v)
		pwaNewer("build " + v);
}

function pwaWatch(w) {
	if (!w)
		return;
	if (w.state === "installed")
		pwaInstalled(w);
	else
		w.addEventListener("statechange", () => { if (w.state === "installed") pwaInstalled(w); });
}

// Reload into the new deploy: its worker first (it drops the old caches),
// then the page.
async function pwaReload() {
	PWA.reloading = true;
	const reg = PWA.reg;
	if (reg) {
		await reg.update().catch(() => null);
		const w = reg.waiting || reg.installing;
		if (w) {
			const done = new Promise((resolve) => {
				navigator.serviceWorker.addEventListener("controllerchange", resolve, { once: true });
				setTimeout(resolve, 10000);
			});
			const installed = w.state === "installed" ? Promise.resolve() :
				new Promise((resolve) => w.addEventListener("statechange", () => { if (w.state === "installed" || w.state === "redundant") resolve(); }));
			await installed;
			w.postMessage({ type: "skip-waiting" });
			await done;
		}
	}
	location.reload();
}

async function pwaInit() {
	if (!("serviceWorker" in navigator))
		return;
	navigator.serviceWorker.addEventListener("message", (e) => {
		if (e.data && e.data.type === "offline-miss")
			pwaOfflineMiss(e.data.path);
	});
	// Another tab activated a newer worker under this page: say so.
	navigator.serviceWorker.addEventListener("controllerchange", async () => {
		const w = navigator.serviceWorker.controller;
		if (PWA.reloading || !w || !VERSION)
			return;
		const v = await pwaWorkerVersion(w);
		if (v && v !== VERSION)
			pwaNewer("build " + v);
	});
	try {
		// updateViaCache "none": Pages sends max-age=600; a new sw.js is seen at once.
		PWA.reg = await navigator.serviceWorker.register("sw.js", { updateViaCache: "none" });
	} catch (e) {
		console.warn("service worker:", e);
		return;
	}
	// A3f: a browser that blocks service workers (Playwright's
	// serviceWorkers: "block", some privacy settings) answers undefined;
	// the check of version.json for a newer deploy still runs.
	if (PWA.reg) {
		pwaWatch(PWA.reg.waiting);
		pwaWatch(PWA.reg.installing);
		PWA.reg.addEventListener("updatefound", () => pwaWatch(PWA.reg.installing));
	} else
		PWA.reg = null;
	window.addEventListener("online", pwaCheck);
	document.addEventListener("visibilitychange", () => { if (!document.hidden) pwaCheck(); });
	setInterval(pwaCheck, PWA_CHECK_MS);
	pwaCheck();
}

// After the page's own loads, so the precache does not compete with them
// (app.js imports this module: the page's load event is still to come).
window.addEventListener("load", pwaInit);

export { PWA, pwaNewer, pwaReload };
