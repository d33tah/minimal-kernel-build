// The tabs' static markup (R3): element id of index.src.html -> the
// component (JSX through html.js) whose HTML build.mjs writes into that
// element of index.html. The page's scripts then find the controls by id,
// as in a hand-written index.html; React mounts its components (shell.jsx)
// into other elements, some of them inside this markup (the Source tab's
// Rebuild box, the Source panel's Rebuild button).

import { MachinePanel } from "./machinepanel.jsx";
import { MemoryPanel } from "./memorypanel.jsx";
import { SourceTabPanel } from "./sourcepanel.jsx";
import { AboutPanel } from "./about.jsx";
import { SourceAside } from "./sourceaside.jsx";
import { RunBadgeMenu } from "./runbadge.jsx";

const STATIC = { panel_machine: MachinePanel, panel_memory: MemoryPanel,
	panel_source: SourceTabPanel, panel_about: AboutPanel,
	src_panel: SourceAside, runbadge: RunBadgeMenu };

// id -> the HTML of its component.
function staticHtml() {
	return Object.fromEntries(Object.entries(STATIC).map(([id, C]) => [id, C().html]));
}

export { staticHtml };
