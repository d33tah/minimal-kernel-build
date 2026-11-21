// The Snapshots tab (A1): save, list, restore, download and diff snapshots
// (snapshots.js, snapdiff.js), the input log and replay (replay.js). A
// component on their controllers (SNAP, DIFF, RUN: ui.jsx's controller()),
// mounted when the tab is first shown (shell.jsx); the modules keep the
// state and change it, this draws it. Element ids and data attributes are
// the ones the tests and the modules' callers read.

import { useLayoutEffect, useRef } from "react";
import { h8, PAGE, kib } from "./util.js";
import { emulator } from "./page.js";
import { useStore, useController, Tip } from "./ui.jsx";
import { snapshots, SNAP_LZ4_LEVEL } from "./snaprecord.js";
import { SNAP, snapKernelKey, saveSnapshot, restoreSnapshot, snapRow, snapStoreView, snapPicker, snapEditName, snapEditEnd, snapDeleteAll,
	snapDelete, snapSwitch, downloadSnapshot } from "./snapshots.js";
import { DIFF, snapDiff, snapRunDiff, snapDiffView, snapPageGroups, snapSideTag, snapShowHex, snapHexAll, snapHexView,
	snapOpenPage } from "./snapdiff.js";
import { RUN, replayRun, runStatus, downloadRun, replayNow, replayFile } from "./replay.js";

// A button of the tab's tables (a title only when it has one).
function SnapButton({ text, cls, title, onClick }) {
	return <button type="button" className={cls || undefined} title={title || undefined} onClick={onClick}>{text}</button>;
}

const SAVE_TITLE = "save_state(), then page hashes and LZ4 level " + SNAP_LZ4_LEVEL + " (lz4-wasm 0.9.2) in a Worker: " +
	"after the first snapshot only the pages changed since the last one saved or restored are kept (a delta); " +
	"a download is the full state as an LZ4 frame (.v86state.lz4) that lz4 -d reads.";

// A snapshot's name being edited: Enter or leaving the field saves, Esc
// cancels; the keys stay here (not the VM's, not the page's shortcuts).
function SnapNameEdit({ s }) {
	const ref = useRef(null);
	const done = useRef(false);
	useLayoutEffect(() => {
		ref.current.focus();
		ref.current.select();
	}, []);
	const finish = (save) => {
		if (done.current)
			return;
		done.current = true;
		snapEditEnd(s, save ? ref.current.value : null);
	};
	return <input ref={ref} className="snapedit" defaultValue={s.name} aria-label={"Name of snapshot #" + s.seq}
		onKeyDown={(ev) => {
			ev.stopPropagation();
			if (ev.key === "Enter")
				finish(true);
			else if (ev.key === "Escape")
				finish(false);
		}}
		onKeyUp={(ev) => ev.stopPropagation()} onBlur={() => finish(true)} />;
}

function SnapRow({ s }) {
	const r = snapRow(s), m = r.match;
	const run = s.meta && s.meta.run;
	return (
		<tr className={m === "other" || m === "build" ? "snapother" : undefined} data-id={r.data.id} data-raw={r.data.raw}
			data-full={r.data.full} data-stored={r.data.stored} data-kernel={r.data.kernel} data-where={r.data.where}
			data-codec={r.data.codec} data-base={r.data.base} data-depth={r.data.depth} data-pages={r.data.pages}>
			<td className="snapcell-name">
				{SNAP.editing === s.id ? <SnapNameEdit s={s} /> :
					<SnapButton text={s.name} cls="snapname" title="Rename (Enter saves, Esc cancels)" onClick={() => snapEditName(s)} />}
			</td>
			{r.cells.map((v, i) => <td key={i} className={i === 5 ? "ratio" : undefined}>{v}</td>)}
			<td className="snapacts">
				{m === "other" && <><SnapButton text={"Switch to " + s.kernel} cls="snapswitch" title={"Load " + s.kernel + " with " +
					kib(s.ram) + " of RAM; then this snapshot can be restored"} onClick={() => snapSwitch(s)} />{" "}</>}
				{m === "match" && <><SnapButton text="Restore" cls="restore" onClick={() => restoreSnapshot(s)} />{" "}</>}
				{run && <><SnapButton text="Replay" cls="snapreplay" title={"Boot " + run.kernel + " again and replay this snapshot's run " +
					"(the same keys at the same instruction counts) to " + run.end.at.toLocaleString("en") +
					" instructions: the same state without restoring it"}
				onClick={() => replayRun(run).catch((e) => runStatus("Replay failed: " + e, "error"))} />{" "}</>}
				<SnapButton text="Download" cls="snapdl" onClick={() => downloadSnapshot(s)} />{" "}
				<SnapButton text="Delete" cls="snapdel" title={"Delete this snapshot" + (s.where === "stored" ? " from this browser" : "")}
					onClick={() => snapDelete([s])} />
			</td>
		</tr>
	);
}

function RunBox() {
	useController(RUN);
	return (
		<div id="run_box">
			<h3>
				{"Input log and replay "}
				<Tip text="On the virtual clock a run repeats exactly; the keyboard is the one input that comes at a moment of wall time. So a key typed on the virtual clock waits for the next boundary between two JIT slices or two stepping slices and goes into the guest there, recorded with the instruction count and the virtual time. A run is the boot settings (kernel, RAM, command line, clock, speed, the RTC start date and the random seed) and that log. Replay boots the same kernel with those settings, puts each key in at its recorded count, goes on past the stops the run went on past (the initramfs prompt), stops where the run was taken and compares the instruction count, EIP, RAM and screen. Snapshots and position bookmarks keep the run up to them; a run downloads as JSON. Keys typed during a replay are ignored." />
			</h3>
			{" "}
			<p id="run_text" className="note">{RUN.text}</p>
			{" "}
			<div className="scan">
				<button id="run_dl" type="button" disabled={!RUN.nowable} onClick={downloadRun}>Download run</button>
				{" "}
				<button id="run_replay" type="button" disabled={!RUN.nowable} onClick={replayNow}>Replay</button>
				{" "}
				<label className="runfile">
					{"Replay a file "}
					<input id="run_file" type="file" accept=".json,application/json" onChange={(ev) => replayFile(ev.target)} />
				</label>
				{" "}
				{/* key: each status line is a new element, as each was written anew (a test may have cleared the one before). */}
				<span key={RUN.said} id="run_status" role="status" data-replay={RUN.replay}>{RUN.status}</span>
			</div>
		</div>
	);
}

// The changed pages of the diff, grouped by region, each with its hexdump
// and Page tab buttons.
function SnapPages() {
	const d = snapDiff;
	const tagB = snapSideTag(d.b), tagA = snapSideTag(d.a);
	return (
		<div id="snap_pages" key={DIFF.n}>
			{!d.pages.length ? "No page of RAM changed." : [...snapPageGroups()].map(([g, list]) => (
				<details key={g} open>
					<summary>{g + ": " + list.length + " page" + (list.length === 1 ? "" : "s") + ", " +
						list.reduce((n, x) => n + x[1], 0).toLocaleString("en") + " bytes changed"}</summary>
					{list.map(([p, n, syms]) => (
						<div key={p} className="snappg" data-page={p} data-bytes={n}>
							<code>{"0x" + h8(p * PAGE)}</code>
							{" " + n.toLocaleString("en") + " byte" + (n === 1 ? "" : "s") +
								(syms.length ? " · " + syms.slice(0, 3).map((s) => s.name).join(", ") + (syms.length > 3 ? " +" + (syms.length - 3) : "") : "") + " "}
							<SnapButton text="hexdump" cls="snaphex" title="Both sides of this page, byte by byte" onClick={() => snapShowHex(p)} />
							{" "}
							<SnapButton text={"Page tab: " + tagB} cls="snapopen" title={"Open this page in the Page tab as " +
								(d.b.id === "now" ? "it is now" : "“" + d.b.name + "” had it") + "; yellow bytes differ from " +
								(d.a.id === "now" ? "now" : "“" + d.a.name + "”")} onClick={() => snapOpenPage(p, "b")} />
							{" "}
							<SnapButton text={"Page tab: " + tagA} cls="snapopena" title={"Open this page in the Page tab as " +
								(d.a.id === "now" ? "it is now" : "“" + d.a.name + "” had it")} onClick={() => snapOpenPage(p, "a")} />
						</div>
					))}
				</details>
			))}
		</div>
	);
}

// Both sides of one page, byte by byte (yellow: they differ).
function SnapHex() {
	const h = snapDiff.hex;
	const per = window.matchMedia("(max-width: 640px)").matches ? 8 : 16;
	const v = h && snapHexView(per);
	return (
		<div id="snap_hex" hidden={!h} data-page={h ? h.p : undefined}>
			{h && <><p className="note">
				{v.label + (h.all ? "." : "; rows without a change are left out. ")}
				{!h.all && <SnapButton text="all rows" title={"Show all " + PAGE / per + " rows"} onClick={snapHexAll} />}
			</p>
			<div className="snaphexgrid">
				{v.cols.map((c) => (
					<div key={c.side} className={"snapcol snapcol-" + c.side}>
						<b>{c.title}</b>
						{/* The rows' HTML: hex digits, <mark> and escaped ASCII (snapHexView), one string for 4 KiB. */}
						<pre dangerouslySetInnerHTML={{ __html: c.html }} />
					</div>
				))}
			</div></>}
		</div>
	);
}

function SnapDiffOut() {
	const d = snapDiff;
	const st = { "data-state": DIFF.state || undefined, "data-compared": DIFF.state ? (DIFF.compared >= 0 ? DIFF.compared : "all") : undefined };
	const v = d && snapDiffView();
	return (
		<div id="snap_diff_out" hidden={!d} {...st} data-icount-a={d ? d.a.meta.icount : undefined} data-icount-b={d ? d.b.meta.icount : undefined}
			data-count-a={d ? v.nA : undefined} data-count-b={d ? v.nB : undefined}>
			{d && <SnapDiffBody v={v} />}
		</div>
	);
}

// The diff's summary, registers, screen and pages.
function SnapDiffBody({ v }) {
	return (
		<>
			<p id="snap_diff_sum">{v.sum}</p>
			{" "}
			<h4>Registers</h4>
			{" "}
			<div className="table-wrap">
				<table id="snap_regs" className="mono" hidden={!v.regs.length}>
					<tbody>
						{v.regs.map((x) => (
							<tr key={x.n} data-reg={x.n} data-old={x.old} data-new={x.new}>
								<td>{x.n}</td>
								<td>{x.oldText}</td>
								<td>→</td>
								<td className="snapnew">{x.newText}</td>
							</tr>
						))}
					</tbody>
				</table>
			</div>
			{" "}
			<p id="snap_regs_same" className="note">{v.regs.length ? "Unchanged: " + v.same.join(" ") + "." : "No register changed."}</p>
			{" "}
			<h4>Screen</h4>
			{" "}
			<div id="snap_screen">
				{v.screen.length ? v.screen.map((x) => (
					<div key={x.i} className="snapscr" data-line={x.i}>
						<span className="snapln">{"line " + (x.i + 1)}</span>
						<pre>{"- " + x.o}</pre>
						<pre className="snapnew">{"+ " + x.v}</pre>
					</div>
				)) : "No line of the VGA text screen changed."}
			</div>
			{" "}
			<h4>Memory</h4>
			{" "}
			<SnapPages />
			{" "}
			<SnapHex />
		</>
	);
}

// The pickers follow this kernel's snapshots (SNAP, the parent) and the
// user's picks (DIFF).
function SnapDiffBox() {
	useController(DIFF);
	const pick = snapPicker(DIFF.pickA, DIFF.pickB);
	return (
		<div id="snap_diffbox" hidden={!pick.mine.length}>
			<h3>
				{"Diff "}
				<Tip text="Compares two snapshots of this kernel, or one and the running machine (now): the registers, the instruction count (the one on the Machine tab), EIP's symbol, the VGA text lines and guest RAM page by page. The snapshots are decompressed and compared in a Worker, so the page stays responsive. Yellow bytes in the hexdump differ between the two sides; the Page tab button opens the page with the snapshot's bytes, Back returns here." />
			</h3>
			{" "}
			<div className="scan">
				<label>
					{"From "}
					<SnapPick id="snap_a" mine={pick.mine} value={pick.a} onPick={(v) => { DIFF.pickA = v; DIFF.changed(); }} />
				</label>
				{" "}
				<label>
					{"to "}
					<SnapPick id="snap_b" mine={pick.mine} value={pick.b} onPick={(v) => { DIFF.pickB = v; DIFF.changed(); }} />
				</label>
				{" "}
				<button id="snap_diff" type="button" disabled={!pick.mine.length || !emulator || DIFF.busy}
					onClick={() => snapRunDiff(pick.a, pick.b)}>Diff</button>
				{" "}
				<span id="snap_diff_status" role="status">{DIFF.status}</span>
			</div>
			{" "}
			<SnapDiffOut />
		</div>
	);
}

function SnapPick({ id, mine, value, onPick }) {
	return (
		<select id={id} value={value} onChange={(ev) => onPick(ev.target.value)}>
			{mine.map((s) => <option key={s.id} value={s.id}>{"#" + s.seq + " " + s.name}</option>)}
			<option value="now">now (the running machine)</option>
		</select>
	);
}

function SnapshotsPanel() {
	useController(SNAP);
	// The Diff button follows the machine (now) too.
	useStore("emulator");
	const store = snapStoreView();
	const n = snapshots.length;
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
				<button id="snap_save" type="button" disabled={!SNAP.ready || SNAP.saving} title={SAVE_TITLE} onClick={saveSnapshot}>
					Save snapshot</button>
				{" "}
				<span key={SNAP.said} id="snap_status" role="status" data-restored={SNAP.restored || undefined}>{SNAP.status}</span>
			</div>
			{" "}
			<p id="snap_store" className="note" data-storage={store.storage}>{store.text}</p>
			{" "}
			<RunBox />
			{" "}
			<div className="table-wrap">
				<table id="snapshots" hidden={!n} data-kernel-key={snapKernelKey() || ""}>
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
					<tbody>
						{snapshots.map((s) => <SnapRow key={s.id} s={s} />)}
					</tbody>
				</table>
			</div>
			{" "}
			<button id="snap_delall" type="button" hidden={!n} data-armed={SNAP.delArmed ? "1" : undefined} onClick={snapDeleteAll}>
				{SNAP.delArmed ? "Delete all " + n + "? Press again" : "Delete all"}</button>
			{" "}
			<SnapDiffBox />
		</>
	);
}

export { SnapshotsPanel };
