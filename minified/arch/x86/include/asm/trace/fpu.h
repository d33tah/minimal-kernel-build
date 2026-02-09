 
#undef TRACE_SYSTEM
#define TRACE_SYSTEM x86_fpu

#if !defined(_TRACE_FPU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FPU_H

/* --- Inlined linux/tracepoint.h --- */

#include <linux/smp.h>
#include <linux/srcu.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/cpumask.h>
#include <linux/rcupdate.h>
#include <linux/tracepoint-defs.h>
#include <linux/types.h>
#include <linux/cpu.h>
#include <linux/static_call_types.h>

struct tracepoint;

#ifndef PARAMS
#define PARAMS(args...) args

#endif

#ifndef DECLARE_TRACE

#define TP_PROTO(args...)	args
#define TP_ARGS(args...)	args

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

/* tracepoint_string, __tracepoint_string removed - unused */

#define DECLARE_TRACE(name, proto, args)				\
	__DECLARE_TRACE(name, PARAMS(proto), PARAMS(args),		\
			cpu_online(raw_smp_processor_id()),		\
			PARAMS(void *__data, proto))

#endif

#ifndef TRACE_EVENT

#define DECLARE_EVENT_CLASS(name, proto, args, tstruct, assign, print)
#define DEFINE_EVENT(template, name, proto, args)		\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))

#define TRACE_EVENT(name, proto, args, struct, assign, print)	\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))

#endif

DECLARE_EVENT_CLASS(x86_fpu,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu),

	TP_STRUCT__entry(
		__field(struct fpu *, fpu)
		__field(bool, load_fpu)
		__field(u64, xfeatures)
		__field(u64, xcomp_bv)
		),

	TP_fast_assign(
		__entry->fpu		= fpu;
		__entry->load_fpu	= test_thread_flag(TIF_NEED_FPU_LOAD);
		if (boot_cpu_has(X86_FEATURE_OSXSAVE)) {
			__entry->xfeatures = fpu->fpstate->regs.xsave.header.xfeatures;
			__entry->xcomp_bv  = fpu->fpstate->regs.xsave.header.xcomp_bv;
		}
	),
	TP_printk("x86/fpu: %p load: %d xfeatures: %llx xcomp_bv: %llx",
			__entry->fpu,
			__entry->load_fpu,
			__entry->xfeatures,
			__entry->xcomp_bv
	)
);

DEFINE_EVENT(x86_fpu, x86_fpu_before_save,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_after_save,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_before_restore,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_after_restore,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_regs_activated,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_regs_deactivated,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_init_state,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_dropped,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_copy_src,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_copy_dst,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_xstate_check_failed,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH asm/trace/
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE fpu
#endif  

 

