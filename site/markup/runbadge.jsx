/** @jsxRuntime classic */
/** @jsx h */
/** @jsxFrag Fragment */
// The menu toggle (R3, H1/K1): the small fixed button at the bottom right
// of every tab (its face: whether the VM runs and how fast) and its menu:
// the run state, Pause or Continue, the new-version notice, the bookmarks
// (bookmarks.js) and Back and Forward (nav.js). The component is the
// markup and renders once; debugui.js draws the face and the state when
// the store's `machine` changes (renderRunBadge), bookmarks.js and nav.js
// fill their parts by id.

import { h, Fragment } from "./html.js";

function RunBadgeMenu() {
	return (
		<>
			<div id="runbadge_menu" className="rbmenu" hidden>
				<button id="runbadge_state" type="button" className="rbwords" title="Open the Machine tab: the screen and the run controls">no machine</button>
				{" "}
				<div className="rbrow">
					<button id="runbadge_pause" type="button" disabled>Pause</button>
				</div>
				{" "}
				<div id="runbadge_pwa" className="rbpwa" hidden />
				{" "}
				{/* K1: the bookmarks, here because the menu is reachable from every tab, next to Back and Forward. */}
				{" "}
				<button id="bm_open" type="button" className="bmopen" aria-expanded="false" aria-controls="bm_box">
					{"★ Bookmarks "}
					<span id="bm_count" />
				</button>
				{" "}
				<div id="bm_box" className="bmbox" hidden>
					<div className="bmtools">
						<button id="bm_here" type="button" title="Bookmark the machine's position: EIP and the instruction count (with a snapshot if you like)">
							{"☆ This position"}
						</button>
						{" "}
						<input id="bm_filter" type="search" placeholder="filter by name" aria-label="filter the bookmarks by name" />
					</div>
					{" "}
					<ul id="bm_list" className="bmlist" aria-label="Bookmarks" />
					{" "}
					<p id="bm_store" className="note" hidden />
					{" "}
					<div className="bmtools">
						<button id="bm_export" type="button" title="Save every bookmark (all kernels) as a JSON file">Export</button>
						{" "}
						<button id="bm_import" type="button" title="Add the bookmarks of an exported JSON file">Import</button>
						<input id="bm_import_file" type="file" accept=".json,application/json" hidden />
					</div>
					{" "}
					<p id="bm_status" className="note" role="status" />
				</div>
				{" "}
				<nav id="nav_hist" className="navhist" aria-label="Jump history">
					<button id="nav_back" type="button">{"‹ Back"}</button>
					<button id="nav_fwd" type="button">{"Forward ›"}</button>
				</nav>
			</div>
			{" "}
			<button id="runbadge_toggle" className="rbtog" type="button" data-state="off" aria-expanded="false" aria-controls="runbadge_menu" aria-label="Menu: no machine. Back, Forward, Pause" title="Menu: no machine. Back, Forward, Pause">
				{"\n    "}
				<span className="rbtop" aria-hidden="true">
					<span className="rbbars" />
					<span className="rbdot" />
					<span className="rbnews" hidden />
				</span>
				{"\n    "}
				<span id="runbadge_speed" className="rbspeed" aria-hidden="true">off</span>
				{"\n  "}
			</button>
		</>
	);
}

export { RunBadgeMenu };
