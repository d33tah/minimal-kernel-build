"use strict";

// The Source panel's view state (A1): what source.js, srcswitch.js and
// srcedit.js set and sourceaside.jsx draws.
//   SRCV: the panel: open (shown beside the tab), shown (source.js's
//     srcPanel last drawn, kept while the panel is closed), range (the
//     title's lines), info (the line under the text), and the elements the modules
//     write through their refs: the file's text (bodyEl: thousands of lines
//     of HTML, drawn by source.js as the Page listing is), the editor's
//     line numbers and diff (gutterEl, pdiffEl; its textarea is srcedit.js's
//     own node) and the buttons the focus goes back to (filesEl, editEl).
//   SWV: the file switcher (its rows are srcswitch.js's srcSw; qEl its
//     filter, an uncontrolled input; listEl its list).
//   EDBAR: the Edit button and the editor bar (srcedit.js's ED: editing,
//     the diff shown, a revert armed), and the panel's editing marks: their
//     own components, so a save or a file opened does not draw the panel
//     again for them.
//   EDSTATE: the editor bar's line (where the caret is, saved or not):
//     its own small component, drawn at every key typed.

import { controller } from "./ui.jsx";

const SRCV = controller({ open: false, shown: null, range: "", info: "", bodyEl: null, gutterEl: null, pdiffEl: null, filesEl: null,
	editEl: null });
const SWV = controller({ open: false, qEl: null, listEl: null });
const EDBAR = controller({});
const EDSTATE = controller({ text: "" });

// The panel is open (shown beside the tab).
const srcOpen = () => SRCV.open;

// Open: the caller draws the panel next (renderSourcePanel).
function srcShow(open) {
	SRCV.open = open;
	if (!open)
		SRCV.changed();
}

export { SRCV, SWV, EDBAR, EDSTATE, srcOpen, srcShow };
