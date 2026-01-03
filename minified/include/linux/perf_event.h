#ifndef _LINUX_PERF_EVENT_H
#define _LINUX_PERF_EVENT_H

/* Minimal perf_event.h - perf events disabled, all functions are empty stubs */
#include <uapi/linux/perf_event.h>
#include <asm/perf_event.h>
#include <asm/hw_breakpoint.h>

struct task_struct;
struct pt_regs;
struct vm_area_struct;

/* perf_sample_data, perf_overflow_handler_t removed - never used */

struct perf_event { int state; };
struct perf_event_context { int dummy; };

/* perf_sw_event removed - never called */

#endif  
