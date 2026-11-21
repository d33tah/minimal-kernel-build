"use strict";

// The bookmarks' controls (bookmarks.js): initBookmarks wires the stars,
// the menu, the dialog, the keys and the long press.

import { realNow } from "./clock.js";
import { $ } from "./util.js";
import { vmHasKeys, state } from "./page.js";
import { srcPanel } from "./source.js";
import { dbgOnChange } from "./dbginfo.js";
import { bmLoad, bmExport, bmDialogSave, bmDialogClose, bmMenuFit, bmPendingTick, bmRender, bmAsk, bmDraftHere,
	bmDraftPos, bmListOpen, BM, bmImport, bmDraftAt, bmPressTarget, BM_LONG_MS, BM_KEY,
	bmChanged } from "./bookmarks.js";

// ---- init -------------------------------------------------------------------------

function initBookmarks() {
	bmLoad();
	// Every add action: a ☆ button, a right click or long press, the B key.
	$("bm_add_eip").addEventListener("click", () => bmAsk(bmDraftHere("eip")));
	$("bm_add_mem").addEventListener("click", () => bmAsk(bmDraftHere("mem")));
	$("bm_add_src").addEventListener("click", () => bmAsk(bmDraftHere("src")));
	$("bm_here").addEventListener("click", () => bmAsk(bmDraftPos()));
	$("bm_open").addEventListener("click", () => bmListOpen($("bm_box").hidden));
	$("bm_filter").addEventListener("input", () => {
		BM.q = $("bm_filter").value.trim();
		bmRender();
	});
	$("bm_export").addEventListener("click", bmExport);
	$("bm_import").addEventListener("click", () => $("bm_import_file").click());
	$("bm_import_file").addEventListener("change", () => {
		const f = $("bm_import_file").files[0];
		if (f)
			bmImport(f).finally(() => { $("bm_import_file").value = ""; });
	});
	$("bm_save").addEventListener("click", bmDialogSave);
	$("bm_cancel").addEventListener("click", bmDialogClose);
	// Before the menu's and the popovers' own key handlers (capture, added
	// first): Enter and Escape in the dialog and in a name being edited;
	// v86 takes every key on the window, so the list's buttons get their
	// Enter and Space here.
	document.addEventListener("keydown", (ev) => {
		const t = ev.target;
		if (!$("bm_dialog").hidden && (ev.key === "Escape" || (ev.key === "Enter" && t === $("bm_name")))) {
			ev.preventDefault();
			ev.stopImmediatePropagation();
			if (ev.key === "Enter")
				bmDialogSave();
			else
				bmDialogClose();
			return;
		}
		if (t && t.classList && t.classList.contains("bmedit") && (ev.key === "Enter" || ev.key === "Escape"))
			return; // its own handler
		const btn = t && t.closest ? t.closest("#bm_box button, #bm_dialog button, #bm_open") : null;
		if (btn && (ev.key === "Enter" || ev.key === " ")) {
			ev.preventDefault();
			ev.stopImmediatePropagation();
			btn.click();
			return;
		}
		// B: a bookmark for what is under the pointer, else what has the
		// focus, else the tab's place (as its ☆).
		if ((ev.key === "b" || ev.key === "B") && !ev.altKey && !ev.ctrlKey && !ev.metaKey && !ev.shiftKey && !vmHasKeys &&
			!(t && t.matches && t.matches("input, textarea, select")) && $("bm_dialog").hidden) {
			ev.preventDefault();
			ev.stopImmediatePropagation();
			const h = BM.hover;
			const d = (h && h.el.isConnected && bmDraftAt(h.el, h.x, h.y)) || bmDraftAt(document.activeElement, 0, 0) ||
				(srcPanel && !$("src_panel").hidden && $("src_panel").contains(document.activeElement) ? bmDraftHere("src") : bmDraftHere("tab"));
			bmAsk(d);
		}
	}, true);
	document.addEventListener("pointermove", (ev) => {
		if (ev.pointerType === "mouse")
			BM.hover = { el: ev.target, x: ev.clientX, y: ev.clientY };
	}, { passive: true });
	// A right click on something that can be bookmarked opens the dialog
	// (with Shift, the browser's own menu); so does a long press.
	document.addEventListener("contextmenu", (ev) => {
		if (realNow() - BM.longAt < 1500) {
			ev.preventDefault();
			return;
		}
		if (ev.shiftKey || !$("bm_dialog").hidden)
			return;
		const d = bmDraftAt(bmPressTarget(ev), ev.clientX, ev.clientY);
		if (d) {
			ev.preventDefault();
			bmAsk(d);
		}
	});
	let press = null;
	const unpress = () => {
		if (press)
			clearTimeout(press.timer);
		press = null;
	};
	document.addEventListener("pointerdown", (ev) => {
		unpress();
		// A new press: the events below that drop the end of the long press
		// (its mousedown and click) leave this one alone.
		BM.longAt = -Infinity;
		if (ev.pointerType === "mouse" || !$("bm_dialog").hidden)
			return;
		const x = ev.clientX, y = ev.clientY, el = bmPressTarget(ev);
		if (!bmDraftAt(el, x, y))
			return;
		press = { x, y, timer: setTimeout(() => {
			press = null;
			const d = bmDraftAt(el, x, y);
			if (d && $("bm_dialog").hidden) {
				BM.longAt = realNow();
				bmAsk(d);
			}
		}, BM_LONG_MS) };
	}, true);
	document.addEventListener("pointermove", (ev) => {
		if (press && Math.hypot(ev.clientX - press.x, ev.clientY - press.y) > 10)
			unpress();
	}, true);
	document.addEventListener("pointerup", unpress, true);
	document.addEventListener("pointercancel", unpress, true);
	// The press that opened the dialog: its release must not move the focus
	// out of the name field (mousedown) nor click (a page square would open
	// the Page tab).
	document.addEventListener("mousedown", (ev) => {
		if (realNow() - BM.longAt < 1500 && !(ev.target.closest && ev.target.closest("#bm_dialog")))
			ev.preventDefault();
	}, true);
	document.addEventListener("pointerup", () => {
		if (realNow() - BM.longAt < 1500 && !$("bm_dialog").hidden)
			setTimeout(() => $("bm_name").focus(), 0);
	}, true);
	// The click a long press may end with does nothing (a page square would open the Page tab).
	document.addEventListener("click", (ev) => {
		if (realNow() - BM.longAt < 1000 && !(ev.target.closest && ev.target.closest("#bm_dialog"))) {
			ev.preventDefault();
			ev.stopImmediatePropagation();
		}
	}, true);
	window.addEventListener("storage", (ev) => {
		if (ev.key === BM_KEY) {
			bmLoad();
			bmChanged();
		}
	});
	window.addEventListener("resize", bmMenuFit);
	// The menu opened: its list as of now.
	$("runbadge_toggle").addEventListener("click", () => queueMicrotask(() => {
		if (!$("runbadge_menu").hidden && !$("bm_box").hidden)
			bmRender();
	}));
	setInterval(bmPendingTick, 300);
	// The groups (this kernel's, another's) follow the loaded kernel: drawn
	// again when it changes, and once more when its identity (bmKey, a
	// SHA-256) is known. The Memory grid's stars asked for that identity
	// too, but the grid is drawn only when its tab is shown.
	state.subscribe(bmRender, ["image"]);
	// Another build's bookmarks are placed by its symbols, DWARF and source
	// text (E8), which come after the kernel: the open list follows them
	// (not while a name is being edited in it).
	const again = () => {
		if (!$("bm_box").hidden && !(document.activeElement && document.activeElement.classList.contains("bmedit")))
			bmRender();
	};
	state.subscribe(again, ["symbols"]);
	dbgOnChange(again);
	bmRender();
}

export { initBookmarks };
