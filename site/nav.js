"use strict";

// Jump history (Back/Forward through what the page showed).

import { realNow } from "./clock.js";
import { $, PAGE, hex } from "./util.js";
import { tab, selectedPage, symbols, navBuilt, setOpenFnWant, setSelectedPage, setNavBuilt, setVmHasKeys,
	vmHasKeys, provide } from "./page.js";
import { jumpHit, setJumpHit, setJumpHitScroll } from "./jumps.js";
import { jumpPopClose } from "./jumpview.js";
import { graphSelName, graphCapture } from "./graphnav.js";
import { graphRestore } from "./graph.js";
import { asmTargetName } from "./asmnames.js";
import { srcSwClose } from "./srcswitch.js";
import { srcPanel, setSrcPanel, renderSourcePanel } from "./source.js";
import { kernelSymLookup } from "./livedis.js";
import { pageSnapView, setPageSnapView } from "./snapdiff.js";
import { drawGrid } from "./memview.js";
import { navScrollWant, listingLineAt, setNavScrollWant, setLive, listingEl } from "./listing.js";
import { TAB_LOOK, navHashNow, setHashOwn, showTab, writeHash } from "./tabs.jsx";
import { SRCV, srcOpen, srcShow } from "./srcview.js";

// ---- jump history ---------------------------------------------------------

// Every jump the user makes (a link to code or to a file:line, a page of
// the grid, a list's "go there") is an entry of the browser's history:
// the Back and Forward buttons, Alt+Left/Right, the mouse's back button
// and a phone's back gesture all go through popstate and stay in this
// document, so the VM keeps running. navList holds the views (navCapture);
// the entry left is captured again when the user leaves it, so Forward
// returns to it as it was left. Following EIP while stepping only replaces
// the hash (writeHash) and adds nothing.
const NAV_ID = Math.random().toString(36).slice(2); // this load's entries, not an earlier load's
// A listing drawn again within this time after Back (the tab's listing
// gets its width and is drawn again on the next frame) keeps the
// restored scroll instead of scrolling to EIP.
const NAV_SCROLL_MS = 1000;
let navList = [];
let navAt = 0;
let navDepth = 0;          // inside navJump: nested jumps (jumpGo -> jumpTo) are one entry

// The instruction near the top third of the Page listing (where a jump
// puts its target), else -1.
function navListingAnchor() {
	const box = listingEl();
	const y = box.getBoundingClientRect().top + box.clientHeight / 3;
	let at = -1;
	for (const el of box.querySelectorAll(".ins[data-addr]")) {
		if (el.getBoundingClientRect().top > y)
			break;
		at = Number(el.dataset.addr);
	}
	return at;
}

// The Source panel opens or closes: on a wide screen the page's column
// changes width (E5a: the page keeps left of the panel) and the listing is
// drawn again. listingHold, before the change: what it shows (a jump's
// target still wanted, else the line near its top third and how far below
// the listing's top it is); listingKeep, after it (the panel drawn, which
// can take long on a slow host): the next drawing puts that line there.
function listingHold() {
	if (tab !== "page" || selectedPage < 0)
		return null;
	const w = navScrollWant;
	if (w && w.page === selectedPage && realNow() < w.until)
		return w;
	const at = navListingAnchor(), line = at >= 0 ? listingLineAt(at) : null;
	return line ? { page: selectedPage, addr: at, off: line.getBoundingClientRect().top - listingEl().getBoundingClientRect().top } : null;
}

function listingKeep(h) {
	if (h && h.page === selectedPage)
		setNavScrollWant({ ...h, until: realNow() + NAV_SCROLL_MS });
}

function navLabel(v) {
	let where = "the " + TAB_LOOK[v.tab][1] + " tab";
	if (v.tab === "page") {
		const at = v.hit >= 0 && Math.floor(v.hit / PAGE) === v.page ? v.hit : navListingAnchor();
		const name = at >= 0 && symbols ? asmTargetName(at, kernelSymLookup()) : "";
		where = name || (at >= 0 ? hex(at) : v.page >= 0 ? "page " + hex(v.page * PAGE) : where);
	}
	if (v.snap)
		where += ", snapshot " + v.snap.name;
	if (v.tab === "calls" && v.graph && graphSelName(v.graph))
		where += ", " + graphSelName(v.graph);
	if (v.src)
		where += ", " + v.src.path.replace(/^.*\//, "") + (v.src.pick || v.src.line ? ":" + (v.src.pick || v.src.line) : "");
	return where;
}

function navCapture() {
	const open = !!srcPanel && srcOpen();
	const v = { tab, page: selectedPage, top: listingEl() ? listingEl().scrollTop : 0, hit: jumpHit, y: window.scrollY,
		src: open ? { pa: srcPanel.pa, fn: srcPanel.fn, path: srcPanel.path, line: srcPanel.line, pick: srcPanel.pick, whole: srcPanel.whole } : null,
		srcTop: open ? SRCV.bodyEl.scrollTop : 0, graph: tab === "calls" ? graphCapture() : null,
		snap: tab === "page" && pageSnapView && pageSnapView.page === selectedPage ? pageSnapView : null };
	v.label = navLabel(v);
	return v;
}

function navSame(a, b) {
	const src = (s) => (s ? [s.path, s.line, s.pick, s.whole, s.pa].join("\0") : "");
	return a.tab === b.tab && a.page === b.page && a.hit === b.hit && src(a.src) === src(b.src) && a.snap === b.snap &&
		Math.abs(a.top - b.top) < 2 && Math.abs(a.srcTop - b.srcTop) < 2;
}

// Run a jump the user asked for; a new entry when the view changed.
function navJump(jump) {
	if (navDepth || !navBuilt)
		return jump();
	const before = navCapture(), url = location.href;
	navDepth++;
	try {
		return jump();
	} finally {
		navDepth--;
		const after = navCapture();
		if (!navSame(before, after)) {
			// The first jump also turns the entry the page was loaded in
			// into a guard: going back past the first view lands there
			// and shows the first view again instead of leaving the page.
			if (!navList.length) {
				history.replaceState({ nav: -1, id: NAV_ID }, "", url);
				history.pushState({ nav: 0, id: NAV_ID }, "", url);
			} else
				history.replaceState({ nav: navAt, id: NAV_ID }, "", url); // the hash it had: the jump replaced it
			navList.length = navAt + 1;
			navList[navAt] = before;
			navList.push(after);
			navAt++;
			history.pushState({ nav: navAt, id: NAV_ID }, "", navHashNow());
			setHashOwn(location.hash);
			navRender();
		}
	}
}

function navRestore(v) {
	jumpPopClose(false);
	setPageSnapView(v.snap || null);
	setJumpHit(v.hit);
	setJumpHitScroll(false);
	if (v.page !== selectedPage) {
		setSelectedPage(v.page);
		drawGrid();
		setLive();
	}
	setNavScrollWant(v.tab === "page" ? { page: v.page, top: v.top, until: realNow() + NAV_SCROLL_MS } : null);
	setOpenFnWant(null);
	if (v.graph)
		graphRestore(v.graph);
	showTab(v.tab);
	if (v.tab === "page")
		listingEl().scrollTop = v.top;
	srcSwClose(false);
	if (v.src) {
		// Its scroll once the file is drawn (now, or when the file or DWARF arrives).
		setSrcPanel({ ...v.src, scrollPx: v.srcTop });
		srcShow(true);
		renderSourcePanel();
	} else if (srcOpen()) {
		setSrcPanel(null);
		srcShow(false);
	}
	writeHash(); // showTab wrote it before the panel was restored
	window.scrollTo(window.scrollX, v.y);
}

function navOnPop(ev) {
	const s = ev.state;
	// An entry of an earlier load of the page: its views are gone.
	if (!s || s.id !== NAV_ID || !navList.length)
		return;
	const to = Math.min(Math.max(s.nav, 0), navList.length - 1);
	setHashOwn(location.hash); // the entry's own hash: navRestore writes the view's
	navList[navAt] = navCapture();
	navAt = to;
	navRestore(navList[to]);
	navRender();
}

function navRender() {
	const box = $("nav_hist"), back = $("nav_back"), fwd = $("nav_fwd");
	box.dataset.count = navList.length;
	box.dataset.at = navAt;
	back.disabled = navAt <= 0;
	fwd.disabled = navAt >= navList.length - 1;
	const say = (b, dir, v, key) => {
		const t = v ? dir + " to " + v.label + " (" + key + ")" : dir + ": no " + (dir === "Back" ? "earlier" : "later") + " view";
		b.title = t;
		b.setAttribute("aria-label", t);
	};
	say(back, "Back", navList[navAt - 1], "Alt+Left");
	say(fwd, "Forward", navList[navAt + 1], "Alt+Right");
}

function initNav() {
	setNavBuilt(true);
	// navRestore sets the window's scroll with the rest of the view; the
	// browser's own restore would come after it.
	history.scrollRestoration = "manual";
	window.addEventListener("popstate", navOnPop);
	$("nav_back").addEventListener("click", () => navAt > 0 && history.back());
	$("nav_fwd").addEventListener("click", () => navAt < navList.length - 1 && history.forward());
	// v86 takes every key on the window for the VM (preventDefault), Alt+Left
	// too: these go to the history unless the VM screen was clicked last
	// (or the focus is in a text field). Enter and Space click the buttons.
	const vmFocus = (ev) => {
		setVmHasKeys(!!ev.target.closest && !!ev.target.closest("#screen_container"));
	};
	document.addEventListener("pointerdown", vmFocus, true);
	document.addEventListener("focusin", vmFocus, true);
	// Tab moves the focus through the page unless the VM screen has the keys.
	for (const type of ["keydown", "keyup"])
		document.addEventListener(type, (ev) => {
			if (ev.key === "Tab" && !vmHasKeys && !ev.altKey && !ev.ctrlKey && !ev.metaKey)
				ev.stopPropagation();
		});
	document.addEventListener("keydown", (ev) => {
		const nav = ev.target.closest ? ev.target.closest("#nav_hist button") : null;
		if (nav && (ev.key === "Enter" || ev.key === " ")) {
			ev.preventDefault();
			ev.stopPropagation();
			nav.click();
			return;
		}
		if (!ev.altKey || ev.ctrlKey || ev.metaKey || ev.shiftKey || (ev.key !== "ArrowLeft" && ev.key !== "ArrowRight"))
			return;
		const a = document.activeElement;
		if (vmHasKeys || (a && a.matches("input, textarea, select")))
			return;
		ev.preventDefault();
		ev.stopPropagation();
		$(ev.key === "ArrowLeft" ? "nav_back" : "nav_fwd").click();
	}, true);
	navRender();
}

// Other modules change them through these (an imported binding is read-only).
const setNavDepth = (v) => { navDepth = v; };

// The page API's entries of this module (page.js).
provide({ navJump, listingHold, listingKeep, navListingAnchor });

export { navJump, NAV_SCROLL_MS, navAt, navDepth, initNav, navList, navListingAnchor, setNavDepth };
