"use strict";

// Popovers (.tip, .tipmark): hover, click, key and long press.

import { realNow } from "./clock.js";
import { $ } from "./util.js";
import { provide } from "./page.js";

// ---- popovers --------------------------------------------------------------

// Title attributes never show on touch screens, so explanations are in
// data-tip and one popover (#tip_pop) shows them. Two kinds:
// - marks (the "?" markers, .tip, and in the listings .tipmark: ⊘, the
//   EIP mark, the gutter stubs, the tint key): a click, tap, Enter or
//   Space toggles the popover, hover shows it on devices that hover;
// - other elements with data-tip (buttons that go somewhere or open a
//   list on a tap: ↑ ↓ ↗, ←n, file:line): hover, keyboard focus and a
//   long press show it, and a tap still acts.
// A tap elsewhere or Esc closes it. The listings are drawn again while
// the CPU steps: the listeners are delegated, and an open popover moves
// to the same mark in the new drawing (tipReanchor).
let tipOpen = null;    // element whose popover is shown
let tipPinned = false; // opened by click/key/long press (stays until closed)
let tipKey = "";       // tipOpen's identity across drawings
let tipLong = null;    // { el, at }: shown by a long press, swallow the click that may follow
const TIP_MARK = ".tip, .tipmark";
const TIP_LONG_MS = 500;

function placeTip(el) {
	const pop = $("tip_pop");
	pop.textContent = el.dataset.tip;
	pop.hidden = false;
	const r = el.getBoundingClientRect();
	const w = Math.min(320, window.innerWidth - 16);
	pop.style.width = w + "px";
	let x = Math.min(Math.max(8, r.left + r.width / 2 - w / 2), window.innerWidth - w - 8);
	let y = r.bottom + 6;
	pop.style.left = x + "px";
	pop.style.top = y + "px";
	const h = pop.offsetHeight;
	if (y + h > window.innerHeight - 8 && r.top - h - 6 > 8)
		pop.style.top = (r.top - h - 6) + "px";
}

// What finds el again after its box is drawn anew: the box, its class and
// the address data it carries.
function tipKeyOf(el) {
	const box = el.closest("[id]");
	const d = el.dataset;
	return [box ? box.id : "", el.getAttribute("class"), d.addr, d.pa, d.go, d.dir, d.way, d.file, d.line].join("|");
}

function showTip(el, pinned) {
	if (!el.dataset.tip)
		return;
	if (tipOpen && tipOpen !== el)
		tipOpen.setAttribute("aria-expanded", "false");
	tipOpen = el;
	tipPinned = pinned;
	tipKey = tipKeyOf(el);
	el.setAttribute("aria-expanded", "true");
	placeTip(el);
}

function hideTip() {
	if (tipOpen)
		tipOpen.setAttribute("aria-expanded", "false");
	tipOpen = null;
	tipPinned = false;
	tipKey = "";
	$("tip_pop").hidden = true;
}

function toggleTip(el) {
	if (tipOpen === el && tipPinned)
		hideTip();
	else
		showTip(el, true);
}

// A box with an open popover's mark was drawn again: show it at the same
// mark in the new drawing (with its new text), or close it.
function tipReanchor() {
	if (!tipOpen || tipOpen.isConnected)
		return;
	const box = $(tipKey.split("|")[0]);
	const el = box ? [...box.querySelectorAll("[data-tip]")].find((e) => tipKeyOf(e) === tipKey) : null;
	if (!el) {
		hideTip();
		return;
	}
	tipOpen = el;
	el.setAttribute("aria-expanded", "true");
	placeTip(el);
}

// The element under ev whose popover a tap toggles: a mark, unless a
// button inside it was hit.
function tipMarkOf(t) {
	const mark = t.closest ? t.closest(TIP_MARK) : null;
	if (!mark)
		return null;
	const act = t.closest("button, a, input, select");
	return act && act !== mark && mark.contains(act) ? null : mark;
}

function initTips() {
	for (const el of document.querySelectorAll(".tip")) {
		if (el.title) {
			el.dataset.tip = el.title;
			el.removeAttribute("title");
		}
		el.setAttribute("aria-expanded", "false");
		el.setAttribute("aria-label", "Explain");
		el.setAttribute("aria-controls", "tip_pop");
	}
	// Before the listings' own handlers (capture): a tap on a mark only
	// toggles its popover (inside a label it must not activate the control).
	document.addEventListener("click", (ev) => {
		const t = ev.target;
		if (tipLong) {
			const long = tipLong;
			tipLong = null;
			const el = t.closest ? t.closest("[data-tip]") : null;
			if (el && (el === long.el || tipKeyOf(el) === long.key) && realNow() - long.at < 1500) {
				ev.preventDefault();
				ev.stopImmediatePropagation(); // jumps.js and source.js listen on document too
				return;
			}
		}
		const mark = tipMarkOf(t);
		if (mark) {
			ev.preventDefault();
			ev.stopImmediatePropagation(); // jumps.js and source.js listen on document too
			toggleTip(mark);
			return;
		}
		if (tipOpen && !$("tip_pop").contains(t))
			hideTip();
	}, true);
	// v86 takes every key on the window: Enter and Space on a mark here first.
	document.addEventListener("keydown", (ev) => {
		const mark = ev.key === "Enter" || ev.key === " " ? tipMarkOf(ev.target) : null;
		if (mark) {
			ev.preventDefault();
			ev.stopImmediatePropagation(); // jumps.js and source.js listen on document too
			toggleTip(mark);
		} else if (ev.key === "Escape" && tipOpen)
			hideTip();
	}, true);
	// Hover: a mouse on a device that hovers (a phone sends pointerover on
	// a tap, and mouse ones when the page changes under the last tap).
	const hover = window.matchMedia("(hover: hover)");
	document.addEventListener("pointerover", (ev) => {
		if (ev.pointerType !== "mouse" || !hover.matches || tipPinned)
			return;
		const el = ev.target.closest ? ev.target.closest("[data-tip]") : null;
		if (el && el !== tipOpen)
			showTip(el, false);
		else if (!el && tipOpen) // its mark may have been drawn again under the pointer
			hideTip();
	});
	document.addEventListener("pointerout", (ev) => {
		if (ev.pointerType !== "mouse" || !hover.matches || tipPinned || !tipOpen)
			return;
		const el = ev.target.closest ? ev.target.closest("[data-tip]") : null;
		if (el === tipOpen && !(ev.relatedTarget && el.contains(ev.relatedTarget)))
			hideTip();
	});
	// Keyboard focus (Tab) on a button with an explanation shows it; focus
	// moved by the page after a tap or click does not.
	let byKey = false;
	document.addEventListener("keydown", (ev) => { byKey = ev.key === "Tab"; }, true);
	document.addEventListener("pointerdown", () => { byKey = false; }, true);
	document.addEventListener("focusin", (ev) => {
		const el = ev.target;
		if (!byKey || tipPinned || !el.dataset || !el.dataset.tip || el.matches(TIP_MARK))
			return;
		showTip(el, false);
	});
	document.addEventListener("focusout", (ev) => {
		if (!tipPinned && tipOpen === ev.target)
			hideTip();
	});
	// A long press on a button with an explanation shows it; the click
	// that may follow does nothing.
	let press = null;
	const unpress = () => {
		if (press)
			clearTimeout(press.timer);
		press = null;
	};
	document.addEventListener("pointerdown", (ev) => {
		unpress();
		if (ev.pointerType === "mouse")
			return;
		const el = ev.target.closest ? ev.target.closest("[data-tip]") : null;
		if (!el || el.matches(TIP_MARK))
			return;
		press = { x: ev.clientX, y: ev.clientY, timer: setTimeout(() => {
			press = null;
			showTip(el, true);
			tipLong = { el, key: tipKeyOf(el), at: realNow() };
		}, TIP_LONG_MS) };
	}, true);
	document.addEventListener("pointermove", (ev) => {
		if (press && Math.hypot(ev.clientX - press.x, ev.clientY - press.y) > 10)
			unpress();
	}, true);
	document.addEventListener("pointerup", unpress, true);
	document.addEventListener("pointercancel", unpress, true);
	document.addEventListener("contextmenu", (ev) => {
		if (tipLong && ev.target.closest && ev.target.closest("[data-tip]"))
			ev.preventDefault();
	});
	// The listings are drawn again while the CPU runs.
	const mo = new MutationObserver(tipReanchor);
	for (const id of ["listing", "context", "cov_info", "eip_jumps", "cur_src", "src_body"])
		if ($(id))
			mo.observe($(id), { childList: true, subtree: true });
	const follow = () => {
		if (!tipOpen)
			return;
		// Hidden when its mark leaves the window or its scrolling listing.
		const r = tipOpen.getBoundingClientRect();
		const box = tipOpen.closest("#listing, #src_body");
		const b = box ? box.getBoundingClientRect() : { top: 0, bottom: window.innerHeight };
		if (r.bottom < Math.max(0, b.top) || r.top > Math.min(window.innerHeight, b.bottom))
			hideTip();
		else
			placeTip(tipOpen);
	};
	window.addEventListener("scroll", follow, { passive: true, capture: true });
	window.addEventListener("resize", follow);
}

// The page API's entries of this module (page.js).
provide({ hideTip });

export { initTips, hideTip };
