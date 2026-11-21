"use strict";

// The Source panel's view state (A1): what source.js, srcswitch.js and
// srcedit.js set and sourceaside.jsx draws.
//   SRCV: the panel: open (shown beside the tab), range (the title's
//     lines), info (the line under the text), and the elements the modules
//     write through their refs: the file's text (bodyEl: thousands of lines
//     of HTML, drawn by source.js as the Page listing is), the editor
//     (taEl, its gutter gutterEl, its diff pdiffEl) and the buttons the
//     focus goes back to (filesEl, editEl).
//   SWV: the file switcher (its rows are srcswitch.js's srcSw; qEl its
//     filter, an uncontrolled input; listEl its list).
//   EDSTATE: the editor bar's line (where the caret is, saved or not):
//     its own small component, drawn at every key typed.

import { controller } from "./ui.jsx";

const SRCV = controller({ open: false, range: "", info: "", bodyEl: null, taEl: null, gutterEl: null, pdiffEl: null, filesEl: null,
	editEl: null });
const SWV = controller({ open: false, qEl: null, listEl: null });
const EDSTATE = controller({ text: "" });

// The panel is open (shown beside the tab).
const srcOpen = () => SRCV.open;

function srcShow(open) {
	SRCV.open = open;
	SRCV.changed();
}

export { SRCV, SWV, EDSTATE, srcOpen, srcShow };
