/** @jsxRuntime classic */
/** @jsx h */
/** @jsxFrag Fragment */
// The Memory tab (R3): the boot header's memory map, the page grid
// (memview.js, a canvas) and the live memory map (memmap.js, canvases;
// #mm moves to the Quests tab beside its cards and back). The component
// is the tab's markup and renders once; the modules draw into it through
// their ids and follow the store's tabShows (the grid is drawn, the map
// placed, each time a tab is shown).

import { h, Fragment } from "./html.js";

function MemoryPanel() {
	return (
		<>
			<h3>
				{"First MiB "}
				<span className="tip" tabIndex="0" role="button" title="Regions follow from the boot header and from where v86 puts things: real-mode setup at 0x80000, command line at 0x8e000, the 32-bit part always at 1 MiB. SeaBIOS builds the e820 map from the RAM size: usable RAM with the 0x9fc00-0x100000 hole reserved.">
					?
				</span>
			</h3>
			{" "}
			<div className="bar" id="map_low" />
			{" "}
			<h3>
				All RAM (
				<span id="ram_label" />
				)
			</h3>
			{" "}
			<div className="bar" id="map_all" />
			{" "}
			<ul className="legend" id="legend" />
			{" "}
			<div className="scan">
				<button id="scan" type="button" disabled>Scan memory</button>
				{" "}
				<button id="bm_add_mem" type="button" className="bmadd" title="Bookmark the selected page (outlined), else EIP's page; or right-click (long-press) a square, or point at it and press B">
					{"☆ Bookmark"}
				</button>
				{" "}
				<label>
					<input type="checkbox" id="autoscan" />
					{" auto"}
					<span className="tip" tabIndex="0" role="button" title="Rescan all pages every second">?</span>
				</label>
				{" "}
				<span id="footprint" />
				{" "}
				<span className="tip" tabIndex="0" role="button" title="Each square is a 4 KiB page of guest RAM, coloured by region (boot header) and, when the kernel has a symbols.json, by ELF section. Filled squares hold data, hollow ones were all zero at the last scan; a dot marks pages that changed between the last two scans (while slow-stepping: since the previous frame). The page of the next instruction (EIP) has a blue box, the stack's page (ESP) a dashed one. Click a page to open it in the Page tab.">
					?
				</span>
			</div>
			{" "}
			<p className="note mono" id="mem_eip" />
			{" "}
			<div id="pages_wrap">
				<div id="pages_box">
					<canvas id="pages" width="448" height="14" />
					<div id="pages_esp" className="memesp" hidden />
					<div id="pages_eip" className="memeip" hidden />
					<span id="pages_esp_tag" className="memtag esp tipmark" tabIndex="0" role="button" aria-label="ESP: the stack's page" hidden data-tip="ESP: the stack. The dashed box is the page ESP points into, at its physical address (SS base + ESP, through the page tables once paging is on).">
						ESP
					</span>
					<span id="pages_eip_tag" className="memtag tipmark" tabIndex="0" role="button" aria-label="EIP: the next instruction's page" hidden data-tip="EIP: the page of the next instruction the CPU runs. The blue box is that page's square, at the physical address (CS base + EIP, through the page tables once paging is on), the same one the Debugger shows; the line above the grid opens it in the Page tab. It follows EIP while stepping and when paused; at full speed (JIT) it is drawn again when the CPU stops.">
						EIP
					</span>
				</div>
			</div>
			{" "}
			<ul className="legend" id="grid_legend" />
			{" "}
			<p className="note mono" id="focus_info" aria-live="polite" />
			{" "}
			<p className="note" id="hover_info" aria-live="polite" />
			{" "}
			<div id="mm_home">
				<details id="mm" className="mm">
					<summary>
						{"Live memory map "}
						<span className="tip" tabIndex="0" role="button" title="The whole 32-bit physical address space on a Hilbert curve: addresses next to each other stay next to each other on the square. It is cut at every region a standard the guest used names (IBM PC memory map, the BIOS's E820 answers, PCI BARs, ACPI or MultiProcessor tables, IA32_APIC_BASE) and at the end of RAM; each region gets cells by its size, at least 16 so the small ones show, and stretches no standard names get a few hatched cells. While this is open the page records memory events (as the Events tab does) and lights their cells in the initiator's colour, then they fade: the CPU outside RAM (memory-mapped I/O) with a pulse from the cell of the code that did it (the ring), a device in RAM (DMA) from its PCI region or the edge, the emulator as a drop poured along the curve (the loader places the kernel before the first instruction). The CPU's writes to RAM are sampled per frame by page (a write needs a TLB entry made for writing; the map empties the TLB each frame). Beside it, the colour text memory at 0xB8000 as the guest writes it. The slider moves through the log event by event, Replay plays it, Live follows the machine again; a step back cuts the log and the map follows. Click a cell, or focus the map and use the arrow keys, PageUp/PageDown (regions) and Enter, for its events: go there steps back to the instruction, file:line opens the Source panel. Only first touch: each region lights once per initiator and the story lists who touched it first. Closed, nothing is recorded or drawn.">
							?
						</span>
					</summary>
					{" "}
					<div className="mm-bar">
						<button id="mm_play" type="button" aria-pressed="false">Replay</button>
						{" "}
						<button id="mm_live" type="button">Live</button>
						{" "}
						<label>
							{"speed "}
							<select id="mm_speed" defaultValue="1">
								<option value="0.25">0.25x</option>
								<option value="1">1x</option>
								<option value="4">4x</option>
							</select>
						</label>
						{" "}
						<label>
							<input type="checkbox" id="mm_first" />
							{" only first touch"}
						</label>
						{" "}
						<input type="range" id="mm_time" min="0" max="0" defaultValue="0" step="1" aria-label="Position in the memory event log" />
					</div>
					{" "}
					<p className="note" id="mm_status" aria-live="off" />
					{" "}
					<div className="mm-view">
						<canvas id="mm_map" className="mm-map" width="512" height="512" tabIndex="0" role="img" aria-label="Memory map of the physical address space; arrow keys move a cursor, PageUp and PageDown go by region, Enter lists the cell's events" />
						{" "}
						<figure className="mm-textfig">
							<canvas id="mm_text" className="mm-text" width="640" height="400" role="img" aria-label="The colour text memory as the guest wrote it" />
							<figcaption className="note" id="mm_text_cap" />
						</figure>
					</div>
					{" "}
					<p className="note mono" id="mm_cursor" aria-live="polite" />
					{" "}
					<ul className="legend" id="mm_key" />
					{" "}
					<div id="mm_info" className="mm-info" aria-live="polite" />
					{" "}
					<h4>Who touched each region first</h4>
					{" "}
					<ol className="mev-list mm-story" id="mm_story" />
					{" "}
					<h4>Regions along the curve</h4>
					{" "}
					<ul className="mev-list mono" id="mm_regions" />
				</details>
			</div>
		</>
	);
}

export { MemoryPanel };
