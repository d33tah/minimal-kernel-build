#ifndef _LINUX_HW_BREAKPOINT_H
#define _LINUX_HW_BREAKPOINT_H

#include <linux/perf_event.h>

/* From uapi/linux/hw_breakpoint.h - inlined */
enum {
	HW_BREAKPOINT_LEN_1 = 1,
	HW_BREAKPOINT_LEN_2 = 2,
	HW_BREAKPOINT_LEN_3 = 3,
	HW_BREAKPOINT_LEN_4 = 4,
	HW_BREAKPOINT_LEN_5 = 5,
	HW_BREAKPOINT_LEN_6 = 6,
	HW_BREAKPOINT_LEN_7 = 7,
	HW_BREAKPOINT_LEN_8 = 8,
};

enum {
	HW_BREAKPOINT_EMPTY	= 0,
	HW_BREAKPOINT_R		= 1,
	HW_BREAKPOINT_W		= 2,
	HW_BREAKPOINT_RW	= HW_BREAKPOINT_R | HW_BREAKPOINT_W,
	HW_BREAKPOINT_X		= 4,
	HW_BREAKPOINT_INVALID   = HW_BREAKPOINT_RW | HW_BREAKPOINT_X,
};

enum bp_type_idx {
	TYPE_INST 	= 0,
	TYPE_DATA	= 0,
	TYPE_MAX
};


extern int __init init_hw_breakpoint(void);

static inline void hw_breakpoint_init(struct perf_event_attr *attr)
{
	memset(attr, 0, sizeof(*attr));

	attr->type = PERF_TYPE_BREAKPOINT;
	attr->size = sizeof(*attr);
	 
	attr->pinned = 1;
	attr->sample_period = 1;
}

static inline void ptrace_breakpoint_init(struct perf_event_attr *attr)
{
	hw_breakpoint_init(attr);
	attr->exclude_kernel = 1;
}

static inline unsigned long hw_breakpoint_addr(struct perf_event *bp)
{
	return 0;
}

static inline int hw_breakpoint_type(struct perf_event *bp)
{
	return 0;
}

static inline unsigned long hw_breakpoint_len(struct perf_event *bp)
{
	return 0;
}

extern struct perf_event *
register_user_hw_breakpoint(struct perf_event_attr *attr,
			    perf_overflow_handler_t triggered,
			    void *context,
			    struct task_struct *tsk);

extern int
modify_user_hw_breakpoint(struct perf_event *bp, struct perf_event_attr *attr);
extern int
modify_user_hw_breakpoint_check(struct perf_event *bp, struct perf_event_attr *attr,
				bool check);

extern struct perf_event *
register_wide_hw_breakpoint_cpu(struct perf_event_attr *attr,
				perf_overflow_handler_t	triggered,
				void *context,
				int cpu);

extern struct perf_event * __percpu *
register_wide_hw_breakpoint(struct perf_event_attr *attr,
			    perf_overflow_handler_t triggered,
			    void *context);

extern int register_perf_hw_breakpoint(struct perf_event *bp);
extern void unregister_hw_breakpoint(struct perf_event *bp);
extern void unregister_wide_hw_breakpoint(struct perf_event * __percpu *cpu_events);

extern int dbg_reserve_bp_slot(struct perf_event *bp);
extern int dbg_release_bp_slot(struct perf_event *bp);
extern int reserve_bp_slot(struct perf_event *bp);
extern void release_bp_slot(struct perf_event *bp);
int hw_breakpoint_weight(struct perf_event *bp);
int arch_reserve_bp_slot(struct perf_event *bp);
void arch_release_bp_slot(struct perf_event *bp);
void arch_unregister_hw_breakpoint(struct perf_event *bp);

extern void flush_ptrace_hw_breakpoint(struct task_struct *tsk);

static inline struct arch_hw_breakpoint *counter_arch_bp(struct perf_event *bp)
{
	return NULL;
}

#endif  
