// Small helpers and constants every part of the page uses (moved out of
// app.js so the modules below it can import them).

// Where v86 0.5.471 puts a bzImage's 32-bit part (src/kernel.js,
// load_kernel): at 1 MiB whatever loadflags/code32_start say; app.js has
// the other addresses.
const V86_KERNEL_ADDR = 0x100000;
const PAGE = 4096;
// v86 0.5.471 src/kernel.js load_kernel: "Put the initrd at the 64 MB
// boundary" (INITRD_ADDRESS = 64 << 20), whatever the RAM size, and sets
// ramdisk_image/ramdisk_size in the header to it.
const V86_INITRD_ADDR = 64 << 20;

// The workflow stamps the script tags and the modules' imports with
// ?v=<commit>; data fetched by the page gets the same stamp, so a cached
// older deploy is never mixed in. (This module is imported as util.js?v=...)
const VERSION = new URL(import.meta.url).searchParams.get("v");
const withV = (url) => (VERSION ? url + (url.includes("?") ? "&" : "?") + "v=" + encodeURIComponent(VERSION) : url);

const $ = (id) => document.getElementById(id);
const hex = (n) => "0x" + n.toString(16);
const kib = (n) => (n < 1024 ? n + " bytes" :
	(n / 1024).toLocaleString("en", { maximumFractionDigits: 1 }) + " KiB");
const num = (n) => (n === null || n === undefined ? "-" : n.toLocaleString("en"));
const h8 = (n) => (n >>> 0).toString(16).padStart(8, "0");
const h4 = (n) => n.toString(16).padStart(4, "0");

const asciiOf = (b) => (b >= 0x20 && b < 0x7f ? String.fromCharCode(b) : ".");
const esc = (c) => (c === "<" ? "&lt;" : c === ">" ? "&gt;" : c === "&" ? "&amp;" : c);
const escHtml = (t) => t.replace(/[<>&"]/g, (c) => ({ "<": "&lt;", ">": "&gt;", "&": "&amp;", '"': "&quot;" })[c]);

// Why a fetch failed: sw.js answers 503 with X-MKB-Offline for a file it
// does not have while offline.
function httpWhy(resp) {
	return resp.headers.get("X-MKB-Offline") ? "not available offline yet (open it once while online)" : "HTTP " + resp.status;
}

export { V86_KERNEL_ADDR, PAGE, V86_INITRD_ADDR, VERSION, withV, $, hex, kib, num, h8, h4, asciiOf, esc,
	escHtml, httpWhy };
