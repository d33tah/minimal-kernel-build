// window.MKB: the page's one global of its own, a handle for the e2e tests
// (site/test_browser.py and the other test_*.py) and for a look from the
// browser's console. MKB.foo reads the export foo of app.js or of the
// module that has it, live (MKB.stepper is page.js's stepper as it is
// now); assigning MKB.foo calls that module's setFoo, and fails when there
// is none. Nothing in the page reads it.
import * as util from "./util.js";
import * as clock from "./clock.js";
import * as jumps from "./jumps.js";
import * as stepper from "./stepper.js";
import * as graph from "./graph.js";
import * as dwarf from "./dwarf.js";
import * as unwind from "./unwind.js";
import * as asmnames from "./asmnames.js";
import * as dbginfo from "./dbginfo.js";
import * as source from "./source.js";
import * as srcedit from "./srcedit.js";
import * as coverage from "./coverage.js";
import * as symimport from "./symimport.js";
import * as livedis from "./livedis.js";
import * as fastforward from "./fastforward.js";
import * as cpustate from "./cpustate.js";
import * as memevents from "./memevents.js";
import * as memmap from "./memmap.js";
import * as quests from "./quests.js";
import * as vars from "./vars.js";
import * as fnfacts from "./fnfacts.js";
import * as settings from "./settings.js";
import * as snapshots from "./snapshots.js";
import * as replay from "./replay.js";
import * as reverse from "./reverse.js";
import * as bookmarks from "./bookmarks.js";
import * as clang from "./clang.js";
import * as kit from "./kit.js";
import * as genimport from "./genimport.js";
import * as rebuild from "./rebuild.js";
import * as pwa from "./pwa.js";
import * as page from "./page.js";

// page.js (the state store and the page API: MKB.state, MKB.api) last.
const MODULES = [util, clock, jumps, stepper, graph, dwarf, unwind, asmnames, dbginfo, source, srcedit, coverage,
	symimport, livedis, fastforward, cpustate, memevents, memmap, quests, vars, fnfacts, settings, snapshots, replay,
	reverse, bookmarks, clang, kit, genimport, rebuild, pwa, page];

function exposeMKB(app) {
	const mkb = {};
	for (const ns of [app, ...MODULES])
		for (const k of Object.keys(ns)) {
			if (k in mkb)
				continue;
			const set = ns["set" + k[0].toUpperCase() + k.slice(1)];
			Object.defineProperty(mkb, k, {
				enumerable: true,
				get: () => ns[k],
				set: (v) => {
					if (typeof set !== "function")
						throw new TypeError("MKB." + k + " has no setter");
					set(v);
				},
			});
		}
	// eslint-disable-next-line no-restricted-syntax -- the one debug/test handle
	Object.defineProperty(window, "MKB", { value: Object.freeze(mkb), enumerable: false });
}

export { exposeMKB };
