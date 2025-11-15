 
#ifndef _LINUX_PERF_EVENT_H
#define _LINUX_PERF_EVENT_H

#include <uapi/linux/perf_event.h>
#include <uapi/linux/bpf_perf_event.h>
#include <asm/perf_event.h>
#include <asm/local64.h>
#include <asm/hw_breakpoint.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/atomic.h>

struct task_struct;
struct pt_regs;
struct perf_event;
struct perf_event_context;
struct vm_area_struct;

 
struct perf_callchain_entry {
	__u64 nr;
	__u64 ip[];
};

struct perf_raw_record {
	u32 size;
};

struct perf_sample_data {
	u64 addr;
	struct perf_raw_record *raw;
};

typedef void (*perf_overflow_handler_t)(struct perf_event *,
					 struct perf_sample_data *,
					 struct pt_regs *regs);

struct hw_perf_event {
	u64 config;
	union {
		struct {  
			struct arch_hw_breakpoint info;
		};
	};
};

struct perf_event {
	struct list_head event_entry;
	int state;
	atomic_t refcount;
	struct task_struct *owner;
	struct mutex child_mutex;
	struct list_head child_list;
	struct perf_event_attr attr;
	struct hw_perf_event hw;
};

struct perf_event_context {
	struct mutex mutex;
	struct list_head event_list;
};

 
extern void perf_event_task_tick(void);
extern void perf_event_fork(struct task_struct *tsk);
extern void perf_event_init(void);
extern void perf_event_exit_task(struct task_struct *child);
extern void perf_event_delayed_put(struct task_struct *tsk);
extern int perf_event_init_task(struct task_struct *child, u64 flags);
extern void perf_event_free_task(struct task_struct *task);
extern void perf_event_namespaces(struct task_struct *task);
extern int perf_event_task_disable(void);
extern int perf_event_task_enable(void);
extern int perf_event_refresh(struct perf_event *event, int refresh);
extern int perf_event_release_kernel(struct perf_event *event);
extern void perf_event_mmap(struct vm_area_struct *vma);
extern void perf_event_exec(void);
extern void perf_event_comm(struct task_struct *tsk, bool exec);
extern void perf_bp_event(struct perf_event *event, void *data);
extern void perf_event_text_poke(const void *addr, const void *old_bytes,
				 size_t old_len, const void *new_bytes, size_t new_len);

extern void __perf_sw_event(u32 event_id, u64 nr, struct pt_regs *regs, u64 addr);
extern void perf_tp_event(u64 addr, u64 count, void *record, int entry_size,
			  struct pt_regs *regs, void *head, int rctx, void *task_ctx);

 
static inline void perf_event_print_debug(void) { }
static inline int perf_register_guest_info_callbacks(void *cbs) { return 0; }
static inline int perf_unregister_guest_info_callbacks(void *cbs) { return 0; }
static inline void perf_event_task_sched_in(struct task_struct *prev,
					    struct task_struct *task) { }
static inline void perf_event_task_sched_out(struct task_struct *prev,
					     struct task_struct *next) { }
static inline void perf_sw_event(u32 event_id, u64 nr, struct pt_regs *regs, u64 addr) {
	__perf_sw_event(event_id, nr, regs, addr);
}

#endif  
