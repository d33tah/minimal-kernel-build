// The tabs' static markup (R3): element id of index.src.html -> the
// component (JSX through html.js) whose HTML build.mjs writes into that
// element of index.html. The page's scripts then find the controls by id,
// as in a hand-written index.html; React mounts its components (shell.jsx)
// into other elements.

import { MemoryPanel } from "./memorypanel.jsx";
import { AboutPanel } from "./about.jsx";
import { RunBadgeMenu } from "./runbadge.jsx";

const STATIC = { panel_memory: MemoryPanel, panel_about: AboutPanel,
	runbadge: RunBadgeMenu };

// id -> the HTML of its component.
function staticHtml() {
	return Object.fromEntries(Object.entries(STATIC).map(([id, C]) => [id, C().html]));
}

export { staticHtml };
