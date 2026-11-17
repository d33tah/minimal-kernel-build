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
import * as v86patch from "./v86patch.js";
import * as frames from "./frames.js";
import * as stepbase from "./stepbase.js";
import * as graph from "./graph.js";
import * as graphdata from "./graphdata.js";
import * as graphlayout from "./graphlayout.js";
import * as calltree from "./calltree.js";
import * as graphnav from "./graphnav.js";
import * as graphinit from "./graphinit.js";
import * as dwarf from "./dwarf.js";
import * as dwarfread from "./dwarfread.js";
import * as dwarftables from "./dwarftables.js";
import * as dwarffuncs from "./dwarffuncs.js";
import * as dwarflocals from "./dwarflocals.js";
import * as dwarftypes from "./dwarftypes.js";
import * as unwind from "./unwind.js";
import * as asmnames from "./asmnames.js";
import * as dbginfo from "./dbginfo.js";
import * as source from "./source.js";
import * as srctext from "./srctext.js";
import * as srclinks from "./srclinks.js";
import * as srcswitch from "./srcswitch.js";
import * as srcedit from "./srcedit.js";
import * as coverage from "./coverage.js";
import * as symimport from "./symimport.js";
import * as livedis from "./livedis.js";
import * as fastforward from "./fastforward.js";
import * as cpustate from "./cpustate.js";
import * as memevents from "./memevents.js";
import * as memmap from "./memmap.js";
import * as memmaplayout from "./memmaplayout.js";
import * as memmapdraw from "./memmapdraw.js";
import * as memmaplist from "./memmaplist.js";
import * as quests from "./quests.jsx";
import * as vars from "./vars.js";
import * as varseval from "./varseval.js";
import * as vartasks from "./vartasks.js";
import * as varlocals from "./varlocals.js";
import * as varops from "./varops.js";
import * as fnfacts from "./fnfacts.js";
import * as settings from "./settings.jsx";
import * as snapshots from "./snapshots.js";
import * as replay from "./replay.js";
import * as reverse from "./reverse.js";
import * as bookmarks from "./bookmarks.js";
import * as clang from "./clang.js";
import * as kit from "./kit.js";
import * as genimport from "./genimport.js";
import * as rebuild from "./rebuild.jsx";
import * as pwa from "./pwa.js";
import * as header from "./header.js";
import * as scan from "./scan.js";
import * as memview from "./memview.js";
import * as listing from "./listing.js";
import * as debugui from "./debugui.js";
import * as tabs from "./tabs.jsx";
import * as nav from "./nav.js";
import * as symview from "./symview.js";
import * as run from "./run.js";
import * as runto from "./runto.js";
import * as boot from "./boot.js";
import * as tips from "./tips.js";
import * as kernel from "./kernel.js";
import * as page from "./page.js";

// page.js (the state store and the page API: MKB.state, MKB.api) last.
const MODULES = [util, clock, jumps, stepper, v86patch, frames, stepbase, graph, graphdata, graphlayout, calltree,
	graphnav, graphinit, dwarf, dwarfread, dwarftables, dwarffuncs, dwarflocals, dwarftypes, unwind, asmnames,
	dbginfo, source, srctext, srclinks, srcswitch, srcedit, coverage, symimport, livedis, fastforward, cpustate,
	memevents, memmap, memmaplayout, memmapdraw, memmaplist, quests, vars, varseval, vartasks, varlocals, varops,
	fnfacts, settings, snapshots, replay, reverse, bookmarks, clang, kit, genimport, rebuild, pwa, header, scan,
	memview, listing, debugui, tabs, nav, symview, run, runto, boot, tips, kernel, page ];

function exposeMKB(app) {
	const mkb = {};
	for (const ns of [app, ...MODULES])
		for (const k of Object.keys(ns)) {
			// A name two modules export: the first one's (none since R2).
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
