// The React shell (R3): which component takes over which element of
// index.html. app.js mounts it before init() looks up the controls; each
// view moves here as it becomes a component, on page.js's store (README.md,
// "React shell").
//
// The tabs' static markup is in index.html already (markup/, written by
// build.mjs); the React components go into its elements, some of them
// inside that markup (the Source tab's Rebuild box, the Source panel's
// Rebuild button). The Settings, Quests, Events, Snapshots, Page, Calls and
// Debugger tabs are mounted when they are first shown: React's first
// render of them is work the boot does not need (they are hidden at load
// unless the URL opens one).

import { $ } from "./util.js";
import { state } from "./page.js";
import { mount } from "./ui.jsx";
import { TabBar } from "./tabs.jsx";
import { SettingsPanel } from "./settings.jsx";
import { QuestsPanel } from "./quests.jsx";
import { EventsPanel } from "./events.jsx";
import { SnapshotsPanel } from "./snapshotspanel.jsx";
import { PagePanel } from "./pagepanel.jsx";
import { CallsPanel } from "./callspanel.jsx";
import { DebuggerPanel } from "./debuggerpanel.jsx";
import { RebuildPanel, RebuildPanelButton } from "./rebuild.jsx";
import { HotPatchPanel, HotPatchButton } from "./hotpatch.jsx";

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
	mountOnTab("settings", "panel_settings", <SettingsPanel />);
	mountOnTab("quests", "panel_quests", <QuestsPanel />);
	mountOnTab("events", "panel_events", <EventsPanel />);
	mountOnTab("snapshots", "panel_snapshots", <SnapshotsPanel />);
	mountOnTab("page", "panel_page", <PagePanel />);
	mountOnTab("calls", "panel_calls", <CallsPanel />);
	mountOnTab("debugger", "panel_debugger", <DebuggerPanel />);
	mount($("rb_box"), <RebuildPanel />);
	mount($("rb_go_slot"), <RebuildPanelButton />);
	mount($("hp_box"), <HotPatchPanel />);
	mount($("hp_go_slot"), <HotPatchButton />);
}

export { mountShell };
