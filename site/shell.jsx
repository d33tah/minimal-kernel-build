// The React shell (R3): which component takes over which element of
// index.html. app.js mounts it first thing, before init() looks up the
// controls; each view moves here as it becomes a component, on page.js's
// store (README.md, "React shell").
//
// The tabs' static markup (JSX through html.js) goes in as HTML first;
// then the React components, some of them into elements of that markup
// (the Source tab's Rebuild box, the Source panel's Rebuild button).

import { $ } from "./util.js";
import { mount } from "./ui.jsx";
import { mountHtml } from "./html.js";
import { TabBar } from "./tabs.jsx";
import { SettingsPanel } from "./settings.jsx";
import { QuestsPanel } from "./quests.jsx";
import { EventsPanel } from "./events.jsx";
import { RebuildPanel, RebuildPanelButton } from "./rebuild.jsx";
import { AboutPanel } from "./about.jsx";
import { SnapshotsPanel } from "./snapshotspanel.jsx";
import { PagePanel } from "./pagepanel.jsx";
import { MemoryPanel } from "./memorypanel.jsx";
import { DebuggerPanel } from "./debuggerpanel.jsx";
import { SourceTabPanel } from "./sourcepanel.jsx";
import { SourceAside } from "./sourceaside.jsx";
import { CallsPanel } from "./callspanel.jsx";
import { MachinePanel } from "./machinepanel.jsx";
import { RunBadgeMenu } from "./runbadge.jsx";

// Element id -> the static component that fills it.
const STATIC = [["panel_machine", MachinePanel], ["panel_memory", MemoryPanel], ["panel_page", PagePanel],
	["panel_debugger", DebuggerPanel], ["panel_calls", CallsPanel], ["panel_source", SourceTabPanel],
	["panel_snapshots", SnapshotsPanel], ["panel_about", AboutPanel], ["src_panel", SourceAside], ["runbadge", RunBadgeMenu]];

function mountShell() {
	for (const [id, Static] of STATIC)
		mountHtml($(id), Static());
	mount($("tabrow"), <TabBar />);
	mount($("panel_settings"), <SettingsPanel />);
	mount($("panel_quests"), <QuestsPanel />);
	mount($("panel_events"), <EventsPanel />);
	mount($("rb_box"), <RebuildPanel />);
	mount($("rb_go_slot"), <RebuildPanelButton />);
}

export { mountShell };
