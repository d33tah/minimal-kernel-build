"use strict";

// The Debugger tab's view state (A1): what the other modules set in it
// while it may not be mounted (debuggerpanel.jsx draws it when it is).
//   DBGSTATUS: the status line (#dbg_status): why the CPU stopped, a Run
//     to's progress, a reverse step's result (step, ms: its data-rev and
//     data-rev-ms, which the tests wait on; rev is the controller's own).
//   DBGIN: the Step count and the Run to field. Both inputs are
//     uncontrolled (a test or a user may set their value directly): the
//     Run to text is the element's while the tab is mounted, else addr
//     (Run to from the Source panel and a build's move of the target set
//     it before the tab is shown); the count is read by the tab's buttons.
//   DBGCODE: Code at EIP and around it: the <pre> and the paths line drawn
//     through their refs (debugui.js drawContext, jumpview.js), the symbol
//     rows (symview.js) and the call frames' variables (vars.js) as the DOM
//     nodes their helpers build.

import { controller } from "./ui.jsx";

// note: a text, or an array of texts and nodes (a file:line link).
// said counts the lines written: each is drawn anew (its data attributes
// too, which a test may have removed to wait for the next).
const DBGSTATUS = controller({ note: "", said: 0, step: undefined, ms: undefined });

// rev: a reverse step's state ("busy", "done", "error", "none").
function dbgStatus(note, rev, ms) {
	const s = DBGSTATUS;
	s.note = note;
	s.step = rev;
	s.ms = ms === undefined ? undefined : Math.round(ms);
	s.said++;
	s.changed();
}

// The status line as text (what #dbg_status's textContent was).
function dbgStatusText() {
	const n = DBGSTATUS.note;
	return Array.isArray(n) ? n.map((x) => (typeof x === "string" ? x : x.textContent)).join("") : String(n || "");
}

const DBGIN = { addr: "0x100000", addrEl: null, nEl: null };

const runToText = () => (DBGIN.addrEl ? DBGIN.addrEl.value : DBGIN.addr).trim();

function setRunToText(v) {
	DBGIN.addr = v;
	if (DBGIN.addrEl)
		DBGIN.addrEl.value = v;
}

// The inputs, as the tab's component mounts them (ref callbacks).
const nMount = (el) => { DBGIN.nEl = el; };
const addrMount = (el) => { DBGIN.addrEl = el; };

// The Step and Step back count (100 when the field is no number).
const stepCount = () => Math.max(1, Math.floor(Number(DBGIN.nEl.value) || 100));

const DBGCODE = controller({ ctxEl: null, jumpsEl: null, sym: [], locals: [], localsStatus: "" });

function codeMount(ctx, jumps) {
	DBGCODE.ctxEl = ctx;
	DBGCODE.jumpsEl = jumps;
}

// Code at EIP is there to draw (the tab was shown once).
const dbgMounted = () => !!DBGCODE.ctxEl;

export { DBGSTATUS, dbgStatus, dbgStatusText, DBGIN, nMount, addrMount, runToText, setRunToText, stepCount, DBGCODE, codeMount,
	dbgMounted };
