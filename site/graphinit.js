"use strict";

// The Calls tab's controls: initGraph wires the buttons, the canvas and
// the keys to the views (graph.js and the modules below it).

import { realNow } from "./clock.js";
import { $, num } from "./util.js";
import { tab, image, api, state } from "./page.js";
import { G, MINI, runStart, recTo, taskNames, taskInfo } from "./graphdata.js";
import { selectedNode, timelineFrameAt, graphOpen } from "./graphnav.js";
import { renderGraphInfo, frameTitle, graphTick, timelineArrow, download, graphData, graphDot,
	graphShow } from "./graph.js";

function initGraph() {
	const c = $("graph_canvas");
	const local = (ev) => {
		const r = c.getBoundingClientRect();
		return [ev.clientX - r.left, ev.clientY - r.top];
	};
	// Position along the time axis and its length (transposed: vertical).
	const axis = (x, y) => (G.transpose ? [y - MINI - 6, c.clientHeight - MINI - 24] : [x, c.clientWidth]);
	const zoomAt = (x, y, f) => {
		if (G.view !== "timeline")
			return;
		const [px, w] = axis(x, y);
		const t = G.x0 + px / w * (G.x1 - G.x0);
		const span = Math.max(20, Math.min((G.x1 - G.x0) / f, 1e12));
		G.x0 = t - (px / w) * span;
		G.x1 = G.x0 + span;
		G.userMoved = true;
		G.dirty = true;
	};
	c.addEventListener("pointerdown", (ev) => {
		c.setPointerCapture(ev.pointerId);
		G.pointers.set(ev.pointerId, local(ev));
		G.press = G.pointers.size === 1 ? { at: local(ev), moved: false } : null;
	});
	c.addEventListener("pointermove", (ev) => {
		if (!G.pointers.has(ev.pointerId))
			return;
		const [x, y] = local(ev);
		const [px, py] = G.pointers.get(ev.pointerId);
		if (G.pointers.size === 1 && G.view === "timeline") {
			if (G.press && G.press.at[1] < MINI) { // drag in the minimap: move the window there
				const st = G.stepper, t0 = runStart(), t1 = Math.max(t0 + 1, recTo(st));
				const span = G.x1 - G.x0, t = t0 + x / c.clientWidth * (t1 - t0);
				G.x0 = t - span / 2;
				G.x1 = t + span / 2;
			} else if (G.transpose) {
				const dt = (y - py) / (c.clientHeight - MINI - 24) * (G.x1 - G.x0);
				G.x0 -= dt;
				G.x1 -= dt;
				G.xOff = Math.max(0, G.xOff - (x - px));
			} else {
				const dt = (x - px) / c.clientWidth * (G.x1 - G.x0);
				G.x0 -= dt;
				G.x1 -= dt;
				G.yOff = Math.max(0, G.yOff + (G.flame ? 1 : -1) * (y - py));
			}
			if (G.press && Math.hypot(x - G.press.at[0], y - G.press.at[1]) > 5) {
				G.press.moved = true;
				if (G.follow) { // a manual pan stops following until "follow" is ticked again
					G.follow = false;
					$("graph_follow").checked = false;
				}
			}
			G.userMoved = true;
			G.dirty = true;
		} else if (G.pointers.size === 2) {
			const [other] = [...G.pointers].filter(([k]) => k !== ev.pointerId).map(([, v]) => v);
			const i = G.transpose ? 1 : 0, cur = [x, y], prev = [px, py];
			const d0 = Math.abs(prev[i] - other[i]), d1 = Math.abs(cur[i] - other[i]);
			if (d0 > 10)
				zoomAt((x + other[0]) / 2, (y + other[1]) / 2, d1 / d0);
		}
		G.pointers.set(ev.pointerId, [x, y]);
	});
	const up = (ev) => {
		if (G.press && !G.press.moved && G.pointers.size === 1) {
			const [x, y] = local(ev);
			const sw = G.view === "timeline" ? (G.switchHits || []).find(([, bx, by, bw, bh]) => x >= bx && x <= bx + bw && y >= by && y <= by + bh) : null;
			G.selSwitch = sw ? sw[0] : -1;
			if (sw)
				G.selected = -1;
			else if (G.view === "timeline") {
				G.selected = timelineFrameAt(x, y);
				const m = G.selected < 0 ? (G.mergedHits || []).find(([bx, by, bw, bh]) => x >= bx - 1 && x <= bx + bw + 1 && y >= by && y <= by + bh) : null;
				if (m) {
					// N calls narrower than a pixel: zoom into their range.
					const pad = (m[6] - m[5]) * 0.05;
					G.x0 = m[5] - pad;
					G.x1 = m[6] + pad;
					G.userMoved = true;
					G.mergedInfo = num(m[4]) + " calls in this range (" + num(m[5] - runStart()) + "\u2013" + num(m[6] - runStart()) + " from the entry), zoomed in";
				}
			}
			else if (G.view === "top") {
				const hit = G.boxes.find(([, bx, by, bw, bh]) => x >= bx && x <= bx + bw && y >= by && y <= by + bh);
				G.selected = hit ? hit[0] : -1;
			}
			G.dirty = true;
			renderGraphInfo();
			// A second press on the same box soon after: a double click or a
			// double tap (touch has no dblclick event everywhere). It opens
			// after this press's own events are dispatched to the canvas.
			const node = G.selSwitch < 0 ? selectedNode() : -1, t = realNow();
			const last = G.lastPress;
			if (node >= 0 && last && last.node === node && last.view === G.view && t - last.t < 500 && Math.hypot(x - last.x, y - last.y) < 24) {
				G.lastPress = null;
				G.openTimer = setTimeout(() => {
					G.openTimer = 0;
					graphOpen(node);
				}, 0);
			} else
				G.lastPress = { node, view: G.view, t, x, y };
		}
		G.pointers.delete(ev.pointerId);
		G.press = null;
	};
	c.addEventListener("pointerup", up);
	c.addEventListener("mousemove", (ev) => {
		const st = G.stepper;
		if (!st || G.view !== "timeline" || !G.switchHits)
			return;
		const [x, y] = local(ev);
		const sw = G.switchHits.find(([, bx, by, bw, bh]) => x >= bx && x <= bx + bw && y >= by && y <= by + bh);
		const m = sw ? null : (G.mergedHits || []).find(([bx, by, bw, bh]) => x >= bx - 1 && x <= bx + bw + 1 && y >= by && y <= by + bh);
		const f = sw || m ? -1 : timelineFrameAt(x, y);
		c.title = sw ? "Task switch to task " + st.swTask[sw[0]] + ": " + taskNames(st)[st.swTask[sw[0]]] + "\n" + taskInfo(st)[st.swTask[sw[0]]].how :
			m && timelineFrameAt(x, y) < 0 ? num(m[4]) + " calls in this range (tap to zoom in)" :
			f >= 0 ? frameTitle(st, f) + (st.fr.node(f) < st.symName.length ? "\nDouble-click (double tap) to open it in the Page tab and the Source panel." +
				(api.show.bmFnNames(st.symStart[st.fr.node(f)]) ? "\n\u2605 " + api.show.bmTip(api.show.bmFnNames(st.symStart[st.fr.node(f)])) : "") : "") : "";
	});
	c.addEventListener("pointercancel", (ev) => { G.pointers.delete(ev.pointerId); G.press = null; });
	c.addEventListener("wheel", (ev) => {
		if (G.view !== "timeline")
			return;
		ev.preventDefault();
		zoomAt(...local(ev), Math.exp(-ev.deltaY * 0.002));
	}, { passive: false });
	for (const v of ["timeline", "tree", "top"])
		$("gview_" + v).addEventListener("click", () => {
			G.view = v;
			G.selected = -1;
			G.dirty = true;
			renderGraphInfo();
			graphTick();
		});
	$("graph_fit").addEventListener("click", () => { G.userMoved = false; G.yOff = 0; G.xOff = 0; G.dirty = true; graphTick(); });
	// Double click: open the box's function (G1); "Focus" re-roots.
	c.addEventListener("dblclick", (ev) => {
		const [x, y] = local(ev);
		if (G.view === "timeline") {
			const k = timelineFrameAt(x, y);
			if (k >= 0)
				graphOpen(G.stepper.fr.node(k));
		} else if (G.view === "top") {
			const hit = G.boxes.find(([, bx, by, bw, bh]) => x >= bx && x <= bx + bw && y >= by && y <= by + bh);
			if (hit)
				graphOpen(hit[0]);
		}
	});
	c.addEventListener("keydown", (ev) => {
		if (ev.altKey || ev.ctrlKey || ev.metaKey)
			return;
		if (ev.key === "Enter") {
			if (graphOpen(selectedNode()))
				ev.preventDefault();
		} else if (G.view === "timeline" && ["ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight"].includes(ev.key)) {
			if (timelineArrow(ev.key))
				ev.preventDefault();
		}
	});
	const toggle = (id, key) => $(id).addEventListener("click", () => {
		G[key] = !G[key];
		$(id).setAttribute("aria-pressed", G[key]);
		G.userMoved = false;
		G.yOff = 0;
		G.xOff = 0;
		G.dirty = true;
		graphTick();
	});
	toggle("graph_transpose", "transpose");
	toggle("graph_flame", "flame");
	$("graph_chains").addEventListener("change", () => { G.chains = $("graph_chains").checked; G.dirty = true; graphTick(); });
	$("graph_depth").addEventListener("input", () => {
		G.maxDepth = Number($("graph_depth").value);
		$("graph_depth_v").textContent = G.maxDepth ? String(G.maxDepth) : "all";
		G.dirty = true;
		graphTick();
	});
	$("graph_follow").addEventListener("change", () => { G.follow = $("graph_follow").checked; });
	$("graph_search").addEventListener("input", () => { G.dirty = true; graphTick(); });
	$("graph_json").addEventListener("click", () => {
		if (G.stepper)
			download((image ? image.name : "kernel") + "-calls.json", JSON.stringify(graphData()), "application/json");
	});
	$("graph_dot").addEventListener("click", () => {
		if (G.stepper)
			download((image ? image.name : "kernel") + "-symbol-graph.dot", graphDot(), "text/vnd.graphviz");
	});
	window.addEventListener("resize", () => { G.dirty = true; });
	$("graph_fold").checked = G.fold;
	$("graph_fold").addEventListener("change", () => {
		G.fold = $("graph_fold").checked;
		G.selected = -1;
		G.dirty = true;
		graphTick();
		renderGraphInfo();
	});
	// Drawn (about 10 frames a second) while the Calls tab is shown (showTab).
	state.subscribe(() => graphShow(tab === "calls"), ["tabShows"]);
}

export { initGraph };
