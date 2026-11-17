"use strict";

// The kernel's tasks (vars.js): names and pids read from task_struct.

import { hex } from "./util.js";
import { emuRam } from "./page.js";
import { DWT } from "./dwarfread.js";
import { dwStrip } from "./dwarflocals.js";
import { dbgTypes, dbgHas } from "./dbginfo.js";
import { varsByName, varsMember, varsOffset, varsRead, varsEval, varsReady, varsNotStarted,
	VARS_NOT_STARTED } from "./varseval.js";

// ---- tasks: names and pids from task_struct ------------------------------------
//
// The stepper tells tasks apart by the kernel stack they switch away on
// (stepper.js: ESP >> 13, 8 KiB stacks); the kernel's task_struct.stack
// names that stack, so the two meet there. A task's name (comm) and pid
// are read from guest RAM with the offsets of task_struct's members from
// the DWARF: the running task through the per-CPU current_task; any other
// through the task_struct current_task named when it last switched away
// (stepper.onTaskLeave = varsTaskNow), read again while the task lives (a
// name changed by exec shows), else found in the task list (init_task.tasks)
// by its stack when the kernel has that list. A task that exited keeps
// what was read as it last switched away. graphdata.js taskNames uses this for
// the timeline, frames and call tree; the Debugger's "task" row shows the
// running one.

// Tasks read from the task list at most (a corrupt list ends there).
const TASK_LIST_MAX = 4096;

const varsU32 = (b) => (b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24) >>> 0;

// Offsets in struct task_struct (from current_task's type), or { why }.
function varsTaskLayout(T) {
	if (T.taskLayout)
		return T.taskLayout;
	const ct = (varsByName(T).get("current_task") || [])[0];
	const it = (varsByName(T).get("init_task") || [])[0];
	const p = ct ? dwStrip(T, ct[2]) : -1;
	const sid = p >= 0 && T.types[p][0] === DWT.ptr ? dwStrip(T, T.types[p][1]) : -1;
	const m = (name) => sid >= 0 ? varsMember(T, sid, name) : null;
	const comm = m("comm"), pid = m("pid"), stack = m("stack"), tasks = m("tasks");
	T.taskLayout = !ct ? { why: "this kernel's DWARF has no current_task variable" } :
		!comm || !pid ? { why: "struct task_struct has no comm or pid in this kernel's DWARF" } :
		{ comm: comm[0], pid: pid[0], stack: stack ? stack[0] : -1, tasks: tasks && it ? tasks[0] : -1, init: it ? it[1] : -1 };
	return T.taskLayout;
}

// The task_struct at virtual address va: { va, comm, pid, region } (region:
// its stack >> 13, -1 if task_struct has no stack member), or null.
function varsTaskRead(L, va) {
	const pa = va - varsOffset();
	if (!va || pa < 0 || pa >= emuRam)
		return null;
	const c = varsRead(pa + L.comm, 16), p = varsRead(pa + L.pid, 4), s = L.stack >= 0 ? varsRead(pa + L.stack, 4) : null;
	if (!c || !p)
		return null;
	let comm = "";
	for (const b of c) {
		if (!b)
			break;
		comm += b >= 32 && b < 127 ? String.fromCharCode(b) : "\\x" + b.toString(16).padStart(2, "0");
	}
	return { va, comm, pid: varsU32(p) | 0, region: s ? varsU32(s) >>> 13 : -1 };
}

// The running task through current_task: varsTaskRead's fields plus how,
// or { why }. Only for a kernel with a task_struct layout (varsTaskLayout
// without why: each caller checks).
function varsCurrentTask(T) {
	const L = varsTaskLayout(T);
	const e = varsEval(T, "current_task");
	const b = e.error ? null : varsRead(e.pa, 4);
	if (!b)
		return { why: e.error || "current_task is not in RAM" };
	const t = varsTaskRead(L, varsU32(b));
	return t ? Object.assign(t, { how: "current_task" + (e.notes.length ? " (" + e.notes.join("; ") + ")" : "") }) :
		{ why: "current_task (" + hex(varsU32(b)) + ") does not point at a task_struct in RAM" };
}

// stepper.onTaskLeave: the leaving task as current_task names it (it still
// does: __switch_to sets the next one after the stack switch the stepper
// sees), or null without types (starts loading them).
function varsTaskNow(task, step, dead) {
	if (!dbgHas())
		return null;
	const r = dbgTypes();
	if (r.state !== "ok" || varsTaskLayout(r.T).why)
		return null;
	const c = varsCurrentTask(r.T);
	return c.why ? null : Object.assign(c, { step, dead });
}

// stack region -> task_struct, from the task list (init_task and every
// task on init_task.tasks), or an empty map without that list.
function varsTaskList(L) {
	const out = new Map();
	if (L.tasks < 0 || L.stack < 0 || L.init < 0)
		return out;
	const off = varsOffset(), first = varsTaskRead(L, L.init + off);
	if (first)
		out.set(first.region, first);
	const head = L.init + off + L.tasks;
	let at = head;
	for (let n = 0; n < TASK_LIST_MAX; n++) {
		const b = varsRead(at - off, 4);
		if (!b || varsU32(b) === head)
			break;
		at = varsU32(b);
		const t = varsTaskRead(L, at - L.tasks);
		if (!t)
			break;
		out.set(t.region, t);
	}
	return out;
}

// Per stepper task: { comm, pid, va, how } read from task_struct, or
// { why } when it cannot be (guess: graphdata.js names it by its thread function).
function varsTasks(st) {
	const r = dbgHas() ? dbgTypes() : { state: "none", why: "this kernel has no DWARF (no task_struct layout)" };
	const L = r.state === "ok" ? varsTaskLayout(r.T) : { why: r.why };
	const out = [];
	const ready = !L.why && varsReady() && !varsNotStarted();
	const cur = ready ? varsCurrentTask(r.T) : null;
	let list = null;
	for (let t = 0; t < st.taskCount; t++) {
		const region = st.taskRegion[t], seen = st.taskSeen[t];
		const alive = t === st.task || (region >= 0 && st.taskOf.get(region) === t);
		const same = (x) => x && (x.region < 0 || region < 0 || x.region === region);
		let info = null;
		if (ready && alive) {
			if (t === st.task && same(cur))
				info = Object.assign({}, cur, { how: "running: read through " + cur.how });
			else {
				// The boot task is init_task (pid 0).
				const va = seen ? seen.va : t === 0 && L.init >= 0 ? L.init + varsOffset() : 0;
				const again = va ? varsTaskRead(L, va) : null;
				if (same(again))
					info = Object.assign(again, { how: "its task_struct at " + hex(va) + (seen ? " (current_task when it last switched away)" : " (init_task)") });
				else if (region >= 0) {
					list = list || varsTaskList(L);
					const x = list.get(region);
					if (x)
						info = Object.assign({}, x, { how: "its task_struct at " + hex(x.va) + ", found in the task list (init_task.tasks) by its stack" });
				}
			}
		}
		if (!info && seen)
			info = Object.assign({}, seen, { how: "as current_task named it when it last switched away" + (seen.dead ? ", then exited" : "") });
		out.push(info || { why: L.why || (!varsReady() ? "the kernel is not running" : varsNotStarted() ? VARS_NOT_STARTED :
			"its task_struct is not known: it last switched away before the types were loaded" + (L.tasks < 0 ? ", and this kernel's task_struct has no task list" : "")) });
	}
	return out;
}

export { varsTaskLayout, varsCurrentTask, varsTaskNow, varsTasks };
