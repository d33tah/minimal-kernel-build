/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_TRACEPOINT_H
#define _LINUX_TRACEPOINT_H

/*
 * Kernel Tracepoint API.
 *
 * See Documentation/trace/tracepoints.rst.
 *
 * Copyright (C) 2008-2014 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Heavily inspired from the Linux Kernel Markers.
 */

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

extern struct srcu_struct tracepoint_srcu;

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
extern void
for_each_kernel_tracepoint(void (*fct)(struct tracepoint *tp, void *priv),
		void *priv);

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

/*
 * tracepoint_synchronize_unregister must be called between the last tracepoint
 * probe unregistration and the end of module exit to make sure there is no
 * caller executing a probe when it is freed.
 */
static inline void tracepoint_synchronize_unregister(void)
{ }

extern int syscall_regfunc(void);
extern void syscall_unregfunc(void);

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

#endif /* _LINUX_TRACEPOINT_H */

/*
 * Note: we keep the TRACE_EVENT and DECLARE_TRACE outside the include
 *  file ifdef protection.
 *  This is due to the way trace events work. If a file includes two
 *  trace event headers under one "CREATE_TRACE_POINTS" the first include
 *  will override the TRACE_EVENT and break the second include.
 */

#ifndef DECLARE_TRACE

#define TP_PROTO(args...)	args
#define TP_ARGS(args...)	args
#define TP_CONDITION(args...)	args

/*
 * Individual subsystem my have a separate configuration to
 * enable their tracepoints. By default, this file will create
 * the tracepoints if CONFIG_TRACEPOINT is defined. If a subsystem
 * wants to be able to disable its tracepoints from being created
 * it can define NOTRACE before including the tracepoint headers.
 */

#ifdef TRACEPOINTS_ENABLED

#define __DO_TRACE_CALL(name, args)					\
	do {								\
		struct tracepoint_func *it_func_ptr;			\
		void *__data;						\
		it_func_ptr =						\
			rcu_dereference_raw((&__tracepoint_##name)->funcs); \
		if (it_func_ptr) {					\
			__data = (it_func_ptr)->data;			\
			static_call(tp_func_##name)(__data, args);	\
		}							\
	} while (0)

/*
 * it_func[0] is never NULL because there is at least one element in the array
 * when the array itself is non NULL.
 */
#define __DO_TRACE(name, args, cond, rcuidle)				\
	do {								\
		int __maybe_unused __idx = 0;				\
									\
		if (!(cond))						\
			return;						\
									\
		/* srcu can't be used from NMI */			\
		WARN_ON_ONCE(rcuidle && in_nmi());			\
									\
		/* keep srcu and sched-rcu usage consistent */		\
		preempt_disable_notrace();				\
									\
		/*							\
		 * For rcuidle callers, use srcu since sched-rcu	\
		 * doesn't work from the idle path.			\
		 */							\
		if (rcuidle) {						\
			__idx = srcu_read_lock_notrace(&tracepoint_srcu);\
			rcu_irq_enter_irqson();				\
		}							\
									\
		__DO_TRACE_CALL(name, TP_ARGS(args));			\
									\
		if (rcuidle) {						\
			rcu_irq_exit_irqson();				\
			srcu_read_unlock_notrace(&tracepoint_srcu, __idx);\
		}							\
									\
		preempt_enable_notrace();				\
	} while (0)

#ifndef MODULE
#define __DECLARE_TRACE_RCU(name, proto, args, cond)			\
	static inline void trace_##name##_rcuidle(proto)		\
	{								\
		if (static_key_false(&__tracepoint_##name.key))		\
			__DO_TRACE(name,				\
				TP_ARGS(args),				\
				TP_CONDITION(cond), 1);			\
	}
#else
#define __DECLARE_TRACE_RCU(name, proto, args, cond)
#endif

/*
 * Make sure the alignment of the structure in the __tracepoints section will
 * not add unwanted padding between the beginning of the section and the
 * structure. Force alignment to the same alignment as the section start.
 *
 * When lockdep is enabled, we make sure to always do the RCU portions of
 * the tracepoint code, regardless of whether tracing is on. However,
 * don't check if the condition is false, due to interaction with idle
 * instrumentation. This lets us find RCU issues triggered with tracepoints
 * even when this tracepoint is off. This code has no purpose other than
 * poking RCU a bit.
 */
#define __DECLARE_TRACE(name, proto, args, cond, data_proto)		\
	extern int __traceiter_##name(data_proto);			\
	DECLARE_STATIC_CALL(tp_func_##name, __traceiter_##name);	\
	extern struct tracepoint __tracepoint_##name;			\
	static inline void trace_##name(proto)				\
	{								\
		if (static_key_false(&__tracepoint_##name.key))		\
			__DO_TRACE(name,				\
				TP_ARGS(args),				\
				TP_CONDITION(cond), 0);			\
		if (IS_ENABLED(CONFIG_LOCKDEP) && (cond)) {		\
			rcu_read_lock_sched_notrace();			\
			rcu_dereference_sched(__tracepoint_##name.funcs);\
			rcu_read_unlock_sched_notrace();		\
		}							\
	}								\
	__DECLARE_TRACE_RCU(name, PARAMS(proto), PARAMS(args),		\
			    PARAMS(cond))				\
	static inline int						\
	register_trace_##name(void (*probe)(data_proto), void *data)	\
	{								\
		return tracepoint_probe_register(&__tracepoint_##name,	\
						(void *)probe, data);	\
	}								\
	static inline int						\
	register_trace_prio_##name(void (*probe)(data_proto), void *data,\
				   int prio)				\
	{								\
		return tracepoint_probe_register_prio(&__tracepoint_##name, \
					      (void *)probe, data, prio); \
	}								\
	static inline int						\
	unregister_trace_##name(void (*probe)(data_proto), void *data)	\
	{								\
		return tracepoint_probe_unregister(&__tracepoint_##name,\
						(void *)probe, data);	\
	}								\
	static inline void						\
	check_trace_callback_type_##name(void (*cb)(data_proto))	\
	{								\
	}								\
	static inline bool						\
	trace_##name##_enabled(void)					\
	{								\
		return static_key_false(&__tracepoint_##name.key);	\
	}

/*
 * We have no guarantee that gcc and the linker won't up-align the tracepoint
 * structures, so we create an array of pointers that will be used for iteration
 * on the tracepoints.
 */
#define DEFINE_TRACE_FN(_name, _reg, _unreg, proto, args)		\
	static const char __tpstrtab_##_name[]				\
	__section("__tracepoints_strings") = #_name;			\
	extern struct static_call_key STATIC_CALL_KEY(tp_func_##_name);	\
	int __traceiter_##_name(void *__data, proto);			\
	struct tracepoint __tracepoint_##_name	__used			\
	__section("__tracepoints") = {					\
		.name = __tpstrtab_##_name,				\
		.key = STATIC_KEY_INIT_FALSE,				\
		.static_call_key = &STATIC_CALL_KEY(tp_func_##_name),	\
		.static_call_tramp = STATIC_CALL_TRAMP_ADDR(tp_func_##_name), \
		.iterator = &__traceiter_##_name,			\
		.regfunc = _reg,					\
		.unregfunc = _unreg,					\
		.funcs = NULL };					\
	__TRACEPOINT_ENTRY(_name);					\
	int __traceiter_##_name(void *__data, proto)			\
	{								\
		struct tracepoint_func *it_func_ptr;			\
		void *it_func;						\
									\
		it_func_ptr =						\
			rcu_dereference_raw((&__tracepoint_##_name)->funcs); \
		if (it_func_ptr) {					\
			do {						\
				it_func = READ_ONCE((it_func_ptr)->func); \
				__data = (it_func_ptr)->data;		\
				((void(*)(void *, proto))(it_func))(__data, args); \
			} while ((++it_func_ptr)->func);		\
		}							\
		return 0;						\
	}								\
	DEFINE_STATIC_CALL(tp_func_##_name, __traceiter_##_name);

#define DEFINE_TRACE(name, proto, args)		\
	DEFINE_TRACE_FN(name, NULL, NULL, PARAMS(proto), PARAMS(args));

	EXPORT_SYMBOL_GPL(__tracepoint_##name);				\
	EXPORT_SYMBOL_GPL(__traceiter_##name);				\
	EXPORT_STATIC_CALL_GPL(tp_func_##name)
	EXPORT_SYMBOL(__tracepoint_##name);				\
	EXPORT_SYMBOL(__traceiter_##name);				\
	EXPORT_STATIC_CALL(tp_func_##name)


#else /* !TRACEPOINTS_ENABLED */
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

#endif /* TRACEPOINTS_ENABLED */

/*
 * tracepoint_string() is used to save the string address for userspace
 * tracing tools. When tracing isn't configured, there's no need to save
 * anything.
 */
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

#endif /* DECLARE_TRACE */

#ifndef TRACE_EVENT
/*
 * For use with the TRACE_EVENT macro:
 *
 * We define a tracepoint, its arguments, its printk format
 * and its 'fast binary record' layout.
 *
 * Firstly, name your tracepoint via TRACE_EVENT(name : the
 * 'subsystem_event' notation is fine.
 *
 * Think about this whole construct as the
 * 'trace_sched_switch() function' from now on.
 *
 *
 *  TRACE_EVENT(sched_switch,
 *
 *	*
 *	* A function has a regular function arguments
 *	* prototype, declare it via TP_PROTO():
 *	*
 *
 *	TP_PROTO(struct rq *rq, struct task_struct *prev,
 *		 struct task_struct *next),
 *
 *	*
 *	* Define the call signature of the 'function'.
 *	* (Design sidenote: we use this instead of a
 *	*  TP_PROTO1/TP_PROTO2/TP_PROTO3 ugliness.)
 *	*
 *
 *	TP_ARGS(rq, prev, next),
 *
 *	*
 *	* Fast binary tracing: define the trace record via
 *	* TP_STRUCT__entry(). You can think about it like a
 *	* regular C structure local variable definition.
 *	*
 *	* This is how the trace record is structured and will
 *	* be saved into the ring buffer. These are the fields
 *	* that will be exposed to user-space in
 *	* /sys/kernel/debug/tracing/events/<*>/format.
 *	*
 *	* The declared 'local variable' is called '__entry'
 *	*
 *	* __field(pid_t, prev_pid) is equivalent to a standard declaration:
 *	*
 *	*	pid_t	prev_pid;
 *	*
 *	* __array(char, prev_comm, TASK_COMM_LEN) is equivalent to:
 *	*
 *	*	char	prev_comm[TASK_COMM_LEN];
 *	*
 *
 *	TP_STRUCT__entry(
 *		__array(	char,	prev_comm,	TASK_COMM_LEN	)
 *		__field(	pid_t,	prev_pid			)
 *		__field(	int,	prev_prio			)
 *		__array(	char,	next_comm,	TASK_COMM_LEN	)
 *		__field(	pid_t,	next_pid			)
 *		__field(	int,	next_prio			)
 *	),
 *
 *	*
 *	* Assign the entry into the trace record, by embedding
 *	* a full C statement block into TP_fast_assign(). You
 *	* can refer to the trace record as '__entry' -
 *	* otherwise you can put arbitrary C code in here.
 *	*
 *	* Note: this C code will execute every time a trace event
 *	* happens, on an active tracepoint.
 *	*
 *
 *	TP_fast_assign(
 *		memcpy(__entry->next_comm, next->comm, TASK_COMM_LEN);
 *		__entry->prev_pid	= prev->pid;
 *		__entry->prev_prio	= prev->prio;
 *		memcpy(__entry->prev_comm, prev->comm, TASK_COMM_LEN);
 *		__entry->next_pid	= next->pid;
 *		__entry->next_prio	= next->prio;
 *	),
 *
 *	*
 *	* Formatted output of a trace record via TP_printk().
 *	* This is how the tracepoint will appear under ftrace
 *	* plugins that make use of this tracepoint.
 *	*
 *	* (raw-binary tracing wont actually perform this step.)
 *	*
 *
 *	TP_printk("task %s:%d [%d] ==> %s:%d [%d]",
 *		__entry->prev_comm, __entry->prev_pid, __entry->prev_prio,
 *		__entry->next_comm, __entry->next_pid, __entry->next_prio),
 *
 * );
 *
 * This macro construct is thus used for the regular printk format
 * tracing setup, it is used to construct a function pointer based
 * tracepoint callback (this is used by programmatic plugins and
 * can also by used by generic instrumentation like SystemTap), and
 * it is also used to expose a structured trace record in
 * /sys/kernel/debug/tracing/events/.
 *
 * A set of (un)registration functions can be passed to the variant
 * TRACE_EVENT_FN to perform any (un)registration work.
 */

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

#endif /* ifdef TRACE_EVENT (see note above) */

/* Stub trace functions for minimal kernel - removed trace events */
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
