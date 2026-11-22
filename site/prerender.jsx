// The Machine tab's build-time render (A1): build.mjs runs this in node
// and writes each part's HTML into its empty element of index.html, so the
// browser lays the tab out while the bundle downloads; shell.jsx then
// hydrates the parts (React attaches to that markup instead of making it).
// The parts render what their controllers hold at load (machtab.js); the
// handlers they name are never called here (build.mjs stubs the modules
// they come from).

import { renderToString } from "react-dom/server";
import { MACHINE_PARTS } from "./machinepanel.jsx";

// id -> the HTML of its part.
function prerenderHtml() {
	return Object.fromEntries(Object.entries(MACHINE_PARTS).map(([id, Part]) => [id, renderToString(<Part />)]));
}

export { prerenderHtml };
