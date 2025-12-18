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


#ifndef PARAMS
#define PARAMS(args...) args
#endif

#define TRACE_DEFINE_ENUM(x)
#define TRACE_DEFINE_SIZEOF(x)

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

/* TRACE_EVENT_FLAGS, TRACE_EVENT_PERF_PERM removed - never used */

#endif

#ifndef TRACE_EVENT

#define DECLARE_EVENT_CLASS(name, proto, args, tstruct, assign, print)
#define DEFINE_EVENT(template, name, proto, args)		\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))
#define DEFINE_EVENT_FN(template, name, proto, args, reg, unreg)\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))
/* DEFINE_EVENT_PRINT, DEFINE_EVENT_CONDITION removed - never used */

#define TRACE_EVENT(name, proto, args, struct, assign, print)	\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))
#define TRACE_EVENT_FN(name, proto, args, struct,		\
		assign, print, reg, unreg)			\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))
/* TRACE_EVENT_FN_COND, TRACE_EVENT_CONDITION removed - never used */

/* TRACE_EVENT_FLAGS, TRACE_EVENT_PERF_PERM removed - never used */

/* DECLARE_EVENT_NOP, TRACE_EVENT_NOP, DECLARE_EVENT_CLASS_NOP,
   DEFINE_EVENT_NOP removed - never used */

#endif

/* Minimal trace stubs - only those actually called from code */
static inline void trace_sys_exit(struct pt_regs *regs, long ret) {}
static inline void trace_writeback_lazytime_iput(struct inode *inode) {}
