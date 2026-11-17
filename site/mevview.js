"use strict";

// The Events tab's view of the memory events (memevents.js, events.jsx
// draws it): filters, grouping, the rows (MEV_VIEW, spread into MEV).

import { api, state, tab } from "./page.js";
import { dbgFrames } from "./dbginfo.js";
import { mevHex, pciClassName } from "./mevregions.js";

const MEV_VIEW = {
	// Recording for the list from the start with ?memevents=1; the tab follows
	// the store's `tab`.
	panel() {
		if (new URLSearchParams(location.search).get("memevents") === "1")
			this.record(true);
		state.subscribe(() => this.shown(tab === "events"), ["tab"]);
	},

	// The Record box.
	choose(on) {
		this.listUser = on;
		this.record(on);
	},

	// A filter chip, the search, "group repeats".
	setFilter(key, v) {
		this.filter[key] = v;
		this.render();
	},

	setGrouping(on) {
		this.grouping = on;
		this.render();
	},

	record(on) {
		this.need("list", on);
		this.drawn = "";
		if (this.tabShown)
			this.render();
		else
			this.changed(); // the Record box
	},

	// The Events tab shown: the table is drawn while it is, and opening it
	// starts recording unless the user cleared Record. Recording goes on
	// while other tabs are shown.
	shown(on) {
		this.tabShown = on;
		clearInterval(this.listTimer);
		this.listTimer = on ? setInterval(() => this.render(), 500) : null;
		if (on && this.listUser !== false && !this.needs.has("list"))
			this.record(true);
		else if (on)
			this.render();
	},

	// What a standard is, for the popover of a region's "named by".
	stdText(std) {
		const t = [
			["IBM PC memory map", "The address layout of the IBM PC (1981) that every PC keeps below 1 MiB: RAM up to 640 KiB, video memory at 0xA0000-0xBFFFF, ROMs from 0xC0000, the system BIOS at 0xF0000-0xFFFFF."],
			["E820", "The BIOS's memory map: the OS calls INT 15h with EAX=E820h and gets one address range per call with its type (1 usable RAM, 2 reserved, 3 and 4 ACPI). The page saw these answers."],
			["PCI configuration", "PCI configuration mechanism #1: the OS reads each device's configuration space through ports 0xCF8 (address) and 0xCFC (data). A Base Address Register says where the device's registers are; writing all ones to it and reading it back gives their size."],
			["MultiProcessor", "Intel MultiProcessor Specification: a table the BIOS leaves in memory, found by its _MP_ signature, listing the processors and I/O APICs with their addresses."],
			["ACPI", "ACPI tables the firmware leaves in memory: the MADT lists the interrupt controllers (local APIC, I/O APICs), the HPET table the timer, each with its address."],
			["IA32_APIC_BASE", "The model-specific register that holds the local APIC's address; at power-up it is 0xFEE00000 (Intel SDM, volume 3)."],
		].find(([k]) => std.startsWith(k));
		return t ? t[1] : "No standard the guest used names this address yet.";
	},

	// The newest groups of consecutive events with the same key (one event
	// each without grouping) that the filters let through, newest first.
	groups(limit) {
		const f = this.filter, grouping = this.grouping;
		const q = {};
		if (f.who)
			q.initiator = f.who;
		if (f.rw)
			q.write = f.rw === "write";
		if (f.region !== "")
			q.region = Number(f.region);
		const idx = this.query(q), m = this.cap - 1, out = [];
		for (let j = idx.length - 1; j >= 0 && out.length < limit; ) {
			const k0 = idx[j] & m, key = grouping ? this.groupKey(k0) : null, ev = [idx[j]];
			for (j--; grouping && j >= 0 && this.groupKey(idx[j] & m) === key; j--)
				ev.push(idx[j]);
			const g = this.groupInfo(ev);
			if (f.text && !g.search.includes(f.text))
				continue;
			out.push(g);
		}
		return { out, events: idx.length };
	},

	// newest first: ev
	groupInfo(ev) {
		const e = this.event(ev[0]), r = this.nameOf(e.region);
		const g = { ev, e, n: ev.length, r, from: this.cols.pos[ev[ev.length - 1] & (this.cap - 1)] };
		let lo = e.addr, hi = e.addr + Math.max(1, e.size);
		for (const i of ev) {
			const k = i & (this.cap - 1);
			lo = Math.min(lo, this.cols.addr[k]);
			hi = Math.max(hi, this.cols.addr[k] + Math.max(1, this.cols.size[k]));
		}
		g.lo = lo;
		g.hi = hi;
		g.fn = "";
		if (e.initiator === "cpu" && this.st && e.eipPhys >= 0) {
			const s = this.st.symbolOf(e.eipPhys);
			g.fn = s >= 0 ? this.st.symName[s] : "";
		}
		// IBM PC text memory: even bytes are characters, odd ones colours
		if (e.write && e.bytes === undefined && r.std === "IBM PC memory map" && / text /.test(r.name)) {
			let s = "";
			let next = -1;
			for (let j = ev.length - 1; j >= 0; j--) {
				const k = ev[j] & (this.cap - 1), a = this.cols.addr[k], v = this.cols.val[k];
				if (next >= 0 && (a < next || a > next + 1) && s)
					s += " ⏎ "; // another place on the screen: a new line
				next = a + this.cols.size[k];
				for (let b = 0; b < Math.min(4, this.cols.size[k]); b++)
					if (!((a + b) & 1)) {
						const ch = (v >>> (8 * b)) & 0xff;
						s += ch >= 0x20 && ch < 0x7f ? String.fromCharCode(ch) : "·";
					}
			}
			g.blank = !s.trim();
			g.text = s.length > 160 ? s.slice(0, 160) + "..." : s;
		}
		g.who = e.initiator === "cpu" ? "CPU" : e.initiator === "device" ? "Device " + e.source : "Emulator: " + e.source;
		g.search = [g.who, r.name, r.std || "", g.fn, mevHex(lo), mevHex(hi - 1), g.text || ""].join(" ").toLowerCase();
		return g;
	},

	// The tab's view from the log (while it is shown), drawn when it differs.
	render() {
		if (!this.tabShown)
			return;
		const num = (n) => n.toLocaleString("en-US");
		const f = this.filter;
		const v = { status: "", rows: [], more: "", regions: [], facts: [], chips: this.regionChips() };
		if (this.missing || !this.cols) {
			v.status = this.missing ? "This v86 build reports no memory events." : "Boot a kernel to record its memory events.";
			this.drawn = "";
			return this.draw(v);
		}
		const lost = this.first();
		v.status = (this.on ? "Recording" : "Not recording (tick Record)") + ": " + num(this.n - lost) + " events" +
			(lost ? " kept, the oldest " + num(lost) + " dropped" : "") + "; " + num(this.counts[0]) + " by the CPU, " +
			num(this.counts[1]) + " by devices, " + num(this.counts[2]) + " by the emulator" +
			(this.check ? "; check every " + num(this.check.every) + " instructions: " + this.check.checks + " done, " +
				this.check.failures.length + " pages changed without an author" : "") + ".";
		const sig = [this.gen, this.n, f.who, f.rw, f.region, f.text, this.grouping, this.active.length, this.facts.length].join("|");
		if (sig === this.drawn && !this.linesPending) {
			if (this.view && this.view.status !== v.status)
				this.draw({ ...this.view, status: v.status, chips: v.chips });
			return;
		}
		this.drawn = sig;
		this.linesPending = false;
		const { out, events } = this.groups(100);
		v.rows = out.map((g) => this.rowOf(g));
		v.more = !out.length ? (events ? "No event matches the search." : "No events match these filters yet.") :
			"Showing the newest " + num(out.length) + " rows of " + num(events) + " events that match; filter or search to see others.";
		v.regions = this.active.slice().sort((a, b) => a.lo - b.lo || a.hi - b.hi);
		v.facts = this.facts.slice(-50);
		return this.draw(v);
	},

	draw(v) {
		this.view = v;
		this.changed();
	},

	// The region chips: the regions in the log, in address order.
	regionChips() {
		const seen = new Set();
		if (this.cols)
			for (let i = this.first(); i < this.n; i++)
				seen.add(this.cols.region[i & (this.cap - 1)]);
		const ids = [...seen].sort((a, b) => this.nameOf(a).lo - this.nameOf(b).lo);
		return ids.map((id) => ({ id: String(id), r: this.nameOf(id) }));
	},

	// One row of the table: what EventsPanel draws of a group.
	rowOf(g) {
		const e = g.e, row = { g, e, rewind: !!(this.st && e.pos < this.st.steps), code: null };
		if (e.initiator !== "cpu") {
			// a PCI device by its class, as the guest read it
			const d = e.pci !== null && e.pci !== undefined && this.pci ? this.pci.get(e.pci) : null;
			row.whoSub = (e.source || "") + (d ? ", " + pciClassName(d.cfg[0xb], d.cfg[0xa]) : "");
		} else {
			const fr = e.eipPhys >= 0 ? dbgFrames(e.eipPhys) : null;
			if (fr && fr.state === "loading")
				this.linesPending = true;
			row.code = {
				fn: g.fn,
				file: fr && fr.state === "ok" && fr.frames[0].file ? fr.frames[0].file : "",
				line: fr && fr.state === "ok" ? fr.frames[0].line : 0,
				inRegion: !g.fn && e.eipPhys >= 0 ? this.nameOf(this.regionOf(e.eipPhys >>> 0)).name : "",
			};
		}
		row.ram = this.cpu ? this.cpu.memory_size[0] >>> 0 : 0;
		return row;
	},

	showOnMap(addr) {
		api.show.showTab("memory");
		const box = document.getElementById("mm");
		if (!box.open)
			box.open = true;
		// a map just opened lays its regions out in its first frame
		const go = (k) => {
			if (api.memory.MMAP.segs.length) {
				api.memory.MMAP.showCell(api.memory.MMAP.cellOf(addr));
				document.getElementById("mm_map").scrollIntoView({ block: "center" });
			} else if (k < 60) {
				requestAnimationFrame(() => go(k + 1));
			}
		};
		go(0);
	},
};

export { MEV_VIEW };
