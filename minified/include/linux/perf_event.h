#ifndef _LINUX_PERF_EVENT_H
#define _LINUX_PERF_EVENT_H

/* Minimal perf_event.h - perf events disabled, all functions are empty stubs */
#include <uapi/linux/perf_event.h>
#include <asm/perf_event.h>
#include <asm/hw_breakpoint.h>

struct task_struct;
struct pt_regs;
struct vm_area_struct;

/* Minimal structs - never actually used */
struct perf_sample_data;
typedef void (*perf_overflow_handler_t)(struct perf_event *,
					 struct perf_sample_data *,
					 struct pt_regs *regs);

struct perf_event { int state; };
struct perf_event_context { int dummy; };

/* All perf_event functions are empty stubs - no callers remain */
static inline void perf_sw_event(u32 event_id, u64 nr, struct pt_regs *regs, u64 addr) { }

#endif  
