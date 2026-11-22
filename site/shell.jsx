// The React shell (R3): which component takes over which element of
// index.html. app.js mounts it before init() looks up the controls; each
// view moves here as it becomes a component, on page.js's store (README.md,
// "React shell").
//
// The static markup of the Memory and About tabs and of the menu toggle is
// in index.html already (markup/, written by build.mjs); the React
// components go into other elements. The Machine tab (shown at load; its
// screen is index.html's) is rendered at build time too (prerender.jsx)
// and hydrated at load; the Source panel (beside every tab) is mounted at
// load. The Settings, Quests, Events, Snapshots, Page, Calls, Debugger and
// Source tabs are mounted when they are first shown: React's first
// render of them is work the boot does not need (they are hidden at load
// unless the URL opens one).

import { $ } from "./util.js";
import { state } from "./page.js";
import { mount, hydrate } from "./ui.jsx";
import { TabBar } from "./tabs.jsx";
import { SettingsPanel } from "./settings.jsx";
import { QuestsPanel } from "./quests.jsx";
import { EventsPanel } from "./events.jsx";
import { SnapshotsPanel } from "./snapshotspanel.jsx";
import { PagePanel } from "./pagepanel.jsx";
import { CallsPanel } from "./callspanel.jsx";
import { DebuggerPanel } from "./debuggerpanel.jsx";
import { SourceTabPanel } from "./sourcepanel.jsx";
import { SourceAside } from "./sourceaside.jsx";
import { MACHINE_PARTS } from "./machinepanel.jsx";

// Mount a tab's component the first time the store's `tab` names it
// (mountShell runs before init() reads the URL's tab: the Machine tab is
// the one shown then).
function mountOnTab(name, id, element) {
	const off = state.subscribe(() => {
		if (state.get("tab") !== name)
			return;
		off();
		mount($(id), element);
	}, ["tab"]);
}

function mountShell() {
	mount($("tabrow"), <TabBar />);
	// The Machine tab at once: it is the tab shown at load (its screen,
	// which v86 draws into, is index.html's); its parts' markup is in
	// index.html already (build.mjs, prerender.jsx).
	for (const [id, Part] of Object.entries(MACHINE_PARTS))
		hydrate($(id), <Part />);
	mountOnTab("settings", "panel_settings", <SettingsPanel />);
	mountOnTab("quests", "panel_quests", <QuestsPanel />);
	mountOnTab("events", "panel_events", <EventsPanel />);
	mountOnTab("snapshots", "panel_snapshots", <SnapshotsPanel />);
	mountOnTab("page", "panel_page", <PagePanel />);
	mountOnTab("calls", "panel_calls", <CallsPanel />);
	mountOnTab("debugger", "panel_debugger", <DebuggerPanel />);
	mountOnTab("source", "panel_source", <SourceTabPanel />);
	// The Source panel at once: the links of every view open it, and its
	// editor's textarea is wired when it mounts.
	mount($("src_panel"), <SourceAside box={$("src_panel")} />);
}

export { mountShell };
