/** @jsxRuntime classic */
/** @jsx h */
/** @jsxFrag Fragment */
// The Snapshots tab (R3): save, list, restore, download and diff snapshots
// (snapshots.js), the input log and replay (replay.js). The component is
// the tab's markup and renders once; snapshots.js fills the table, the
// store line and the diff, replay.js the run box, through their ids.

import { h, Fragment } from "./html.js";

function SnapshotsPanel() {
	return (
		<>
			<p className="note">
				{"A snapshot is v86 "}
				<code>save_state()</code>
				{": CPU, devices, VGA memory and all guest RAM, compressed with LZ4 level 1 in a Worker, so saving does not stall the page. After the first snapshot of a kernel, a snapshot keeps only the 4 KiB pages of RAM that changed since the last one saved or restored (a delta, found by hashing every page) and Restore puts them on top of that one's RAM. A download is the full state as an LZ4 frame that "}
				<code>lz4 -d</code>
				{" reads. Snapshots are stored in this browser (IndexedDB) and kept across reloads, each for the kernel it was taken with (the bzImage's SHA-256); tap a name to rename it."}
			</p>
			{" "}
			<div className="scan">
				<button id="snap_save" type="button" disabled>Save snapshot</button>
				{" "}
				<span id="snap_status" role="status" />
			</div>
			{" "}
			<p id="snap_store" className="note" />
			{" "}
			<div id="run_box">
				<h3>
					{"Input log and replay "}
					<span className="tip" tabIndex="0" role="button" title="On the virtual clock a run repeats exactly; the keyboard is the one input that comes at a moment of wall time. So a key typed on the virtual clock waits for the next boundary between two JIT slices or two stepping slices and goes into the guest there, recorded with the instruction count and the virtual time. A run is the boot settings (kernel, RAM, command line, clock, speed, the RTC start date and the random seed) and that log. Replay boots the same kernel with those settings, puts each key in at its recorded count, goes on past the stops the run went on past (the initramfs prompt), stops where the run was taken and compares the instruction count, EIP, RAM and screen. Snapshots and position bookmarks keep the run up to them; a run downloads as JSON. Keys typed during a replay are ignored.">
						?
					</span>
				</h3>
				{" "}
				<p id="run_text" className="note" />
				{" "}
				<div className="scan">
					<button id="run_dl" type="button" disabled>Download run</button>
					{" "}
					<button id="run_replay" type="button" disabled>Replay</button>
					{" "}
					<label className="runfile">
						{"Replay a file "}
						<input id="run_file" type="file" accept=".json,application/json" />
					</label>
					{" "}
					<span id="run_status" role="status" />
				</div>
			</div>
			{" "}
			<div className="table-wrap">
				<table id="snapshots" hidden>
					<thead>
						<tr>
							<th>name</th>
							<th>taken</th>
							<th>at</th>
							<th>kernel, RAM</th>
							<th>size</th>
							<th>raw bytes</th>
							<th>ratio</th>
							<th>compression</th>
							<th />
						</tr>
					</thead>
					<tbody />
				</table>
			</div>
			{" "}
			<button id="snap_delall" type="button" hidden>Delete all</button>
			{" "}
			<div id="snap_diffbox" hidden>
				<h3>
					{"Diff "}
					<span className="tip" tabIndex="0" role="button" title="Compares two snapshots of this kernel, or one and the running machine (now): the registers, the instruction count (the one on the Machine tab), EIP's symbol, the VGA text lines and guest RAM page by page. The snapshots are decompressed and compared in a Worker, so the page stays responsive. Yellow bytes in the hexdump differ between the two sides; the Page tab button opens the page with the snapshot's bytes, Back returns here.">
						?
					</span>
				</h3>
				{" "}
				<div className="scan">
					<label>
						{"From "}
						<select id="snap_a" />
					</label>
					{" "}
					<label>
						{"to "}
						<select id="snap_b" />
					</label>
					{" "}
					<button id="snap_diff" type="button" disabled>Diff</button>
					{" "}
					<span id="snap_diff_status" role="status" />
				</div>
				{" "}
				<div id="snap_diff_out" hidden>
					<p id="snap_diff_sum" />
					{" "}
					<h4>Registers</h4>
					{" "}
					<div className="table-wrap">
						<table id="snap_regs" className="mono">
							<tbody />
						</table>
					</div>
					{" "}
					<p id="snap_regs_same" className="note" />
					{" "}
					<h4>Screen</h4>
					{" "}
					<div id="snap_screen" />
					{" "}
					<h4>Memory</h4>
					{" "}
					<div id="snap_pages" />
					{" "}
					<div id="snap_hex" hidden />
				</div>
			</div>
		</>
	);
}

export { SnapshotsPanel };
