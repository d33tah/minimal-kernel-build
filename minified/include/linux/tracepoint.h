 
#ifndef _LINUX_TRACEPOINT_H
#define _LINUX_TRACEPOINT_H

 

#include <linux/smp.h>
#include <linux/srcu.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/cpumask.h>
#include <linux/rcupdate.h>
#include <linux/tracepoint-defs.h>
#include <linux/static_call.h>

struct module;
struct tracepoint;
struct notifier_block;

struct trace_eval_map {
	const char		*system;
	const char		*eval_string;
	unsigned long		eval_value;
};

#define TRACEPOINT_DEFAULT_PRIO	10

/* tracepoint_srcu removed - unused */

extern int
tracepoint_probe_register(struct tracepoint *tp, void *probe, void *data);
extern int
tracepoint_probe_register_prio(struct tracepoint *tp, void *probe, void *data,
			       int prio);
extern int
tracepoint_probe_register_prio_may_exist(struct tracepoint *tp, void *probe, void *data,
					 int prio);
extern int
tracepoint_probe_unregister(struct tracepoint *tp, void *probe, void *data);
static inline int
tracepoint_probe_register_may_exist(struct tracepoint *tp, void *probe,
				    void *data)
{
	return tracepoint_probe_register_prio_may_exist(tp, probe, data,
							TRACEPOINT_DEFAULT_PRIO);
}
/* for_each_kernel_tracepoint removed - unused */

static inline bool trace_module_has_bad_taint(struct module *mod)
{
	return false;
}
static inline
int register_tracepoint_module_notifier(struct notifier_block *nb)
{
	return 0;
}
static inline
int unregister_tracepoint_module_notifier(struct notifier_block *nb)
{
	return 0;
}

 
static inline void tracepoint_synchronize_unregister(void)
{ }

/* syscall_regfunc/syscall_unregfunc removed - unused */

#ifndef PARAMS
#define PARAMS(args...) args
#endif

#define TRACE_DEFINE_ENUM(x)
#define TRACE_DEFINE_SIZEOF(x)

static inline struct tracepoint *tracepoint_ptr_deref(tracepoint_ptr_t *p)
{
	return offset_to_ptr(p);
}

#define __TRACEPOINT_ENTRY(name)					\
	asm("	.section \"__tracepoints_ptrs\", \"a\"		\n"	\
	    "	.balign 4					\n"	\
	    "	.long 	__tracepoint_" #name " - .		\n"	\
	    "	.previous					\n")

#endif  

 

#ifndef DECLARE_TRACE

#define TP_PROTO(args...)	args
#define TP_ARGS(args...)	args
#define TP_CONDITION(args...)	args

 

/* TRACEPOINTS_ENABLED is not defined - use stub implementations */
#define __DECLARE_TRACE(name, proto, args, cond, data_proto)		\
	static inline void trace_##name(proto)				\
	{ }								\
	static inline void trace_##name##_rcuidle(proto)		\
	{ }								\
	static inline int						\
	register_trace_##name(void (*probe)(data_proto),		\
			      void *data)				\
	{								\
		return -ENOSYS;						\
	}								\
	static inline int						\
	unregister_trace_##name(void (*probe)(data_proto),		\
				void *data)				\
	{								\
		return -ENOSYS;						\
	}								\
	static inline void check_trace_callback_type_##name(void (*cb)(data_proto)) \
	{								\
	}								\
	static inline bool						\
	trace_##name##_enabled(void)					\
	{								\
		return false;						\
	}

#define DEFINE_TRACE_FN(name, reg, unreg, proto, args)
#define DEFINE_TRACE(name, proto, args)

 
# define tracepoint_string(str) str
# define __tracepoint_string

#define DECLARE_TRACE(name, proto, args)				\
	__DECLARE_TRACE(name, PARAMS(proto), PARAMS(args),		\
			cpu_online(raw_smp_processor_id()),		\
			PARAMS(void *__data, proto))

#define DECLARE_TRACE_CONDITION(name, proto, args, cond)		\
	__DECLARE_TRACE(name, PARAMS(proto), PARAMS(args),		\
			cpu_online(raw_smp_processor_id()) && (PARAMS(cond)), \
			PARAMS(void *__data, proto))

#define TRACE_EVENT_FLAGS(event, flag)

#define TRACE_EVENT_PERF_PERM(event, expr...)

#endif  

#ifndef TRACE_EVENT
 

#define DECLARE_EVENT_CLASS(name, proto, args, tstruct, assign, print)
#define DEFINE_EVENT(template, name, proto, args)		\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))
#define DEFINE_EVENT_FN(template, name, proto, args, reg, unreg)\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)	\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))
#define DEFINE_EVENT_CONDITION(template, name, proto,		\
			       args, cond)			\
	DECLARE_TRACE_CONDITION(name, PARAMS(proto),		\
				PARAMS(args), PARAMS(cond))

#define TRACE_EVENT(name, proto, args, struct, assign, print)	\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))
#define TRACE_EVENT_FN(name, proto, args, struct,		\
		assign, print, reg, unreg)			\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))
#define TRACE_EVENT_FN_COND(name, proto, args, cond, struct,		\
		assign, print, reg, unreg)			\
	DECLARE_TRACE_CONDITION(name, PARAMS(proto),	\
			PARAMS(args), PARAMS(cond))
#define TRACE_EVENT_CONDITION(name, proto, args, cond,		\
			      struct, assign, print)		\
	DECLARE_TRACE_CONDITION(name, PARAMS(proto),		\
				PARAMS(args), PARAMS(cond))

#define TRACE_EVENT_FLAGS(event, flag)

#define TRACE_EVENT_PERF_PERM(event, expr...)

#define DECLARE_EVENT_NOP(name, proto, args)				\
	static inline void trace_##name(proto)				\
	{ }								\
	static inline bool trace_##name##_enabled(void)			\
	{								\
		return false;						\
	}

#define TRACE_EVENT_NOP(name, proto, args, struct, assign, print)	\
	DECLARE_EVENT_NOP(name, PARAMS(proto), PARAMS(args))

#define DECLARE_EVENT_CLASS_NOP(name, proto, args, tstruct, assign, print)
#define DEFINE_EVENT_NOP(template, name, proto, args)			\
	DECLARE_EVENT_NOP(name, PARAMS(proto), PARAMS(args))

#endif  

 
static inline void trace_sys_exit(struct pt_regs *regs, long ret) {}
static inline void trace_alarmtimer_fired(void *alarm, ktime_t now) {}
static inline void trace_alarmtimer_start(void *alarm, ktime_t now) {}
static inline void trace_alarmtimer_cancel(void *alarm, ktime_t now) {}
static inline void trace_mm_shrink_slab_start(void *shrinker, void *shrinkctl, long nr, long nr_to_scan) {}
static inline void trace_mm_shrink_slab_end(void *shrinker, int nid, unsigned long freed, long nr, long new_nr, int total_scan) {}
static inline void trace_mm_vmscan_throttled(int nid, unsigned long timeout) {}
static inline void trace_mm_vmscan_write_folio(void *folio) {}
static inline void trace_mm_vmscan_lru_isolate(int reclaim_idx, unsigned int order, unsigned long nr_to_scan, unsigned long nr_scanned, unsigned long nr_taken, int lru) {}
static inline void trace_mm_vmscan_lru_shrink_inactive(int nid, unsigned long nr_scanned, unsigned long nr_reclaimed, int priority, int file) {}
static inline void trace_mm_vmscan_lru_shrink_active(int nid, unsigned long nr_taken, unsigned long nr_activate, unsigned long nr_deactivate, unsigned long nr_rotated, int priority, int file) {}
static inline void trace_mm_vmscan_direct_reclaim_begin(unsigned int order, gfp_t gfp_mask) {}
static inline void trace_mm_vmscan_direct_reclaim_end(unsigned long nr_reclaimed) {}
static inline void trace_mm_vmscan_kswapd_sleep(int nid) {}
static inline void trace_mm_vmscan_kswapd_wake(int nid, int zid, int order) {}
static inline void trace_mm_vmscan_wakeup_kswapd(int nid, int zid, int order, unsigned int gfp_flags) {}
static inline void trace_global_dirty_state(unsigned long bg_thresh, unsigned long thresh) {}
static inline void trace_bdi_dirty_ratelimit(struct bdi_writeback *wb, unsigned long dirty_rate, unsigned long task_ratelimit) {}
static inline void trace_balance_dirty_pages(struct bdi_writeback *wb, unsigned long thresh, unsigned long bg_thresh, unsigned long dirty, unsigned long wb_thresh, unsigned long wb_dirty, unsigned long dirty_ratelimit, unsigned long task_ratelimit, unsigned long pages_dirtied, unsigned long period, long pause, unsigned long start_time) {}
static inline void trace_wbc_writepage(struct writeback_control *wbc, struct backing_dev_info *bdi) {}
static inline void trace_writeback_dirty_folio(struct folio *folio, struct address_space *mapping) {}
static inline void trace_folio_wait_writeback(struct folio *folio, struct address_space *mapping) {}
static inline void trace_writeback_lazytime_iput(struct inode *inode) {}
static inline void trace_mm_lru_activate(struct folio *folio) {}
static inline void trace_mm_lru_insertion(struct folio *folio) {}
static inline void trace_mm_page_free(struct page *page, unsigned int order) {}
static inline void trace_mm_page_pcpu_drain(struct page *page, unsigned int order, int migratetype) {}
static inline void trace_mm_page_alloc_zone_locked(struct page *page, unsigned int order, int migratetype, bool pcp) {}
static inline void trace_mm_page_alloc_extfrag(struct page *page, unsigned int order, int current_order, int migratetype, int alloc_migratetype) {}
static inline void trace_mm_page_free_batched(struct page *page) {}
static inline void trace_mm_page_alloc(struct page *page, unsigned int order, gfp_t gfp_flags, int migratetype) {}
static inline void trace_writeback_bdi_register(struct backing_dev_info *bdi) {}
