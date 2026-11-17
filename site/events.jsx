// The Events tab (M4) as a React component (R3): the memory events of
// memevents.js as a table, its filters, the regions the guest's standards
// name and what the guest learned. MEV.render() makes the view from the log
// while the tab is shown (MEV.view) and the panel draws it.

import { useSyncExternalStore } from "react";
import { api } from "./page.js";
import { mevHex } from "./mevregions.js";
import { MEV, MEV_INITIATORS } from "./memevents.js";
import { SrcLink, Tip } from "./ui.jsx";
import { revStepBack } from "./reverse.js";

const num = (n) => n.toLocaleString("en-US");

const Sub = ({ text }) => <span className="mev-sub">{text}</span>;

// A button of a row: text, what it does (title), the action.
const Act = ({ text, tip, onClick, cls = "symlink", children }) =>
	<button type="button" className={cls} title={tip} onClick={onClick}>{children || text}</button>;

// An address range: the Page tab for RAM, and the map.
function AddrLinks({ lo, hi, ram }) {
	const text = hi - lo > 1 ? mevHex(lo) + "-" + mevHex(hi - 1) : mevHex(lo);
	return (
		<span className="mev-addr">
			{lo < ram ?
				<Act text={text} tip={"Open page " + mevHex(lo >>> 12 << 12) + " in the Page tab (hex and code)"}
					onClick={() => api.show.selectPage(lo >>> 12)} /> : text}
			{" "}<Act text="map" tip={"Show " + mevHex(lo) + " on the live memory map (Memory tab)"} onClick={() => MEV.showOnMap(lo)}
				cls="mev-act" />
		</span>
	);
}

// A region with its swatch (the swatch wraps with the name).
const RegionLink = ({ r }) =>
	<Act tip="Show this region on the live memory map (Memory tab)" onClick={() => MEV.showOnMap(r.lo || 0)}>
		<span className={"sw mm-sw mm-k-" + (r.kind || "none")} />{" " + r.name}
	</Act>;

const StdMark = ({ std }) =>
	<span className="mev-std tipmark" tabIndex={0} role="button" data-tip={MEV.stdText(std || "")}>{std || "no standard yet"}</span>;

const Td = ({ col, label, children, empty }) =>
	<td data-col={col} data-label={label} data-empty={empty ? "1" : undefined}>{children}</td>;

function EventRow({ row, ram }) {
	const { g, e, code } = row;
	return (
		<tr className="mev-row" data-who={e.initiator} data-n={String(g.n)}>
			<Td col="step" label="Step">
				<span>{num(e.pos)}{g.n > 1 && <Sub text={g.from === e.pos ? "one instruction" : "from " + num(g.from)} />}</span>
				{row.rewind && <>{" "}<Act text="rewind" cls="mev-act"
					tip={"Step back to instruction " + num(e.pos) + ", the one that did this (reverse history)"}
					onClick={() => revStepBack(MEV.st.steps - e.pos)} /></>}
			</Td>
			<Td col="who" label="Who">
				<span className="mev-who"><span className={"sw mm-sw mm-l" + MEV_INITIATORS.indexOf(e.initiator)} />{" "}
					<b>{e.initiator === "cpu" ? "CPU" : e.initiator === "device" ? "Device" : "Emulator"}</b>
					{e.initiator !== "cpu" && <Sub text={row.whoSub} />}</span>
			</Td>
			<Td col="access" label="Access">
				{/* a filled badge for a write (into memory), an outlined one for a read */}
				<span className="mev-rw" data-rw={e.write ? "write" : "read"}><span className="mev-rwb" aria-hidden="true">{e.write ? "→ W" : "← R"}</span>
					{(g.n > 1 ? num(g.n) + " " : "") + (e.write ? "write" : "read") + (g.n > 1 ? "s" : "")}</span>
			</Td>
			<Td col="address" label="Address"><AddrLinks lo={g.lo} hi={g.hi} ram={ram} /></Td>
			<Td col="size" label="Size">
				{e.bytes !== undefined ? num(e.bytes) + " bytes" : (g.n > 1 ? num(g.n) + " × " : "") + e.size + (e.size === 1 ? " byte" : " bytes")}
			</Td>
			<Td col="value" label="Value">
				<span className="mev-val">{g.text !== undefined && g.blank ? "blank (spaces)" : g.text !== undefined ?
					<><q className="mev-text">{g.text}</q>{g.n === 1 && <Sub text={mevHex(e.value, e.size * 2)} />}</> :
					e.bytes !== undefined ? (e.hash !== null ? <Sub text={"checksum " + mevHex(e.hash, 8).slice(2)} /> : "") :
						<>{e.size === 8 ? "0x" + e.value.toString(16) : mevHex(e.value, e.size * 2)}{g.n > 1 && <Sub text="the newest" />}</>}</span>
			</Td>
			<Td col="region" label="Region"><span><RegionLink r={g.r} />{" "}<StdMark std={g.r.std} /></span></Td>
			<Td col="code" label="Code" empty={!code}>
				{code && <span>
					{code.fn && <><b>{code.fn}</b>{" "}</>}
					{code.file && <><SrcLink file={code.file} line={code.line} pa={e.eipPhys} />{" "}</>}
					{code.inRegion && "code in " + code.inRegion + " "}
					<Sub text={"EIP " + mevHex(e.eip)} />
				</span>}
			</Td>
		</tr>
	);
}

// A row of filter chips; the label between them changes nothing.
function Chips({ id, label, filter, chips }) {
	const on = MEV.filter[filter];
	return (
		<div className="mev-chips" id={id} role="group" aria-label={label}><span className="mev-chiplabel">{label}</span>
			{chips.map(([v, text, sw]) => <button key={v} type="button" className="mev-chip" data-v={v} aria-pressed={String(v === on)}
				onClick={() => MEV.setFilter(filter, v)}>{sw !== undefined && <><span className={"sw mm-sw " + sw} />{" "}</>}{text}</button>)}
		</div>
	);
}

const WHO_CHIPS = [["", "everyone"], ["cpu", "CPU (memory-mapped I/O)", "mm-l0"], ["device", "devices (DMA)", "mm-l1"],
	["emulator", "emulator", "mm-l2"]];
const RW_CHIPS = [["", "reads and writes"], ["write", "writes"], ["read", "reads"]];

function EventsPanel() {
	useSyncExternalStore(MEV.subscribe, () => MEV.rev);
	const v = MEV.view || { status: "", rows: [], more: "", regions: [], facts: [], chips: [] };
	const ram = MEV.cpu ? MEV.cpu.memory_size[0] >>> 0 : 0;
	return (
		<>
			<h2>Memory events</h2>
			<p className="mev-intro">Who touched memory outside the CPU's ordinary work in RAM: the <b>CPU</b> reaching a device's registers (memory-mapped I/O), a <b>device</b> reading or writing RAM by itself (DMA), the <b>emulator</b> placing files (the boot loader, a snapshot restore). Newest first. <Tip text="Each region is named only by a standard the guest itself used: point at or tap the standard's name under it for what that standard is. Repeated accesses by the same code (or device, or emulator task) to the same region, one after another, are one row with their count; text written into the IBM PC text memory is spelled out. An address opens its page in the Page tab (hex and code), map shows it on the live memory map in the Memory tab, rewind steps back to the instruction that did it (reverse history). Search finds a function, a region, a standard, an address or written text." /></p>
			<div className="mev-bar">
				<label><input type="checkbox" id="mev_on" checked={MEV.needs.has("list")} onChange={(e) => MEV.choose(e.target.checked)} /> Record<Tip text="Opening this tab starts recording; it goes on while other tabs are shown, until you clear this box. Recording costs a little speed (about 2% at full speed)." /></label>
				{" "}<label><input type="checkbox" id="mev_group" checked={MEV.grouping} onChange={(e) => MEV.setGrouping(e.target.checked)} /> group repeats</label>
				{" "}<label className="mev-search">Search <input type="search" id="mev_search" placeholder="function, region, 0xb8000..." autoComplete="off"
					onInput={(e) => MEV.setFilter("text", e.target.value.trim().toLowerCase())} /></label>
			</div>
			<Chips id="mev_who" label="Who" filter="who" chips={WHO_CHIPS} />
			<Chips id="mev_rw" label="Access" filter="rw" chips={RW_CHIPS} />
			<div className="mev-chips" role="group" aria-label="Region"><span className="mev-chiplabel">Region</span>
				<span id="mev_regionf" className="mev-chipset">
					{[["", "any region"], ...v.chips.map((c) => [c.id, c.r.name, "mm-k-" + (c.r.kind || "none")])].map(([id, text, sw]) =>
						<button key={id} type="button" className="mev-chip" data-v={id} aria-pressed={String(id === MEV.filter.region)}
							onClick={() => MEV.setFilter("region", id)}>{sw && <><span className={"sw mm-sw " + sw} />{" "}</>}{text}</button>)}
				</span>
			</div>
			<p className="note" id="mev_status" aria-live="polite">{v.status}</p>
			<div className="mev-wrap">
				<table className="mev-table" id="mev_table">
					<thead><tr><th scope="col">Step</th><th scope="col">Who</th><th scope="col">Access</th><th scope="col">Address</th><th scope="col">Size</th><th scope="col">Value</th><th scope="col">Region (named by)</th><th scope="col">Code</th></tr></thead>
					<tbody id="mev_rows">{v.rows.map((row) => <EventRow key={row.g.ev[0]} row={row} ram={ram} />)}</tbody>
				</table>
			</div>
			<p className="note" id="mev_more">{v.more}</p>
			<h3>Regions the guest's standards name</h3>
			<div className="mev-wrap">
				<table className="mev-table mev-regtable" id="mev_regtable">
					<thead><tr><th scope="col">Addresses</th><th scope="col">Region</th><th scope="col">Named by</th></tr></thead>
					<tbody id="mev_regions">{v.regions.map((r, k) => <tr key={k}>
						<Td col="addresses" label="Addresses"><AddrLinks lo={r.lo} hi={r.hi} ram={ram} /></Td>
						<Td col="region" label="Region"><span><RegionLink r={r} /></span></Td>
						<Td col="std" label="Named by"><StdMark std={r.std} /></Td>
					</tr>)}</tbody>
				</table>
			</div>
			<h3>What the guest learned</h3>
			<ol className="mev-facts" id="mev_facts">{v.facts.map((x, k) => <li key={k}><b>{num(x.pos)}</b>{" " + x.text}</li>)}</ol>
		</>
	);
}

export { EventsPanel };
