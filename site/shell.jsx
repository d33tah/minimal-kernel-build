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

function mountShell() {
	mount($("tabrow"), <TabBar />);
	mount($("panel_settings"), <SettingsPanel />);
	mount($("panel_quests"), <QuestsPanel />);
	mount($("panel_events"), <EventsPanel />);
}

export { mountShell };
