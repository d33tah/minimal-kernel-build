// The React shell (R3): which component takes over which element of
// index.html. app.js mounts it first thing, before init() looks up the
// controls; each view moves here as it becomes a component, on page.js's
// store (README.md, "React shell").

import { $ } from "./util.js";
import { mount } from "./ui.jsx";
import { TabBar } from "./tabs.jsx";
import { SettingsPanel } from "./settings.jsx";
import { QuestsPanel } from "./quests.jsx";
import { EventsPanel } from "./events.jsx";
import { AboutPanel } from "./about.jsx";
import { SnapshotsPanel } from "./snapshotspanel.jsx";
import { PagePanel } from "./pagepanel.jsx";
import { MemoryPanel } from "./memorypanel.jsx";
import { DebuggerPanel } from "./debuggerpanel.jsx";
import { SourceTabPanel } from "./sourcepanel.jsx";
import { SourceAside } from "./sourceaside.jsx";
import { CallsPanel } from "./callspanel.jsx";
import { MachinePanel } from "./machinepanel.jsx";

function mountShell() {
	mount($("tabrow"), <TabBar />);
	mount($("panel_settings"), <SettingsPanel />);
	mount($("panel_quests"), <QuestsPanel />);
	mount($("panel_events"), <EventsPanel />);
	mount($("panel_about"), <AboutPanel />);
	mount($("panel_snapshots"), <SnapshotsPanel />);
	mount($("panel_page"), <PagePanel />);
	mount($("panel_memory"), <MemoryPanel />);
	mount($("panel_debugger"), <DebuggerPanel />);
	mount($("panel_source"), <SourceTabPanel />);
	mount($("src_panel"), <SourceAside />);
	mount($("panel_calls"), <CallsPanel />);
	mount($("panel_machine"), <MachinePanel />);
}

export { mountShell };
