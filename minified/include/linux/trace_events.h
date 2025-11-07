#ifndef _LINUX_TRACE_EVENT_H
#define _LINUX_TRACE_EVENT_H

/* Stubbed trace events for minimal kernel - tracing not needed */

/* Empty stubs for all trace event macros and functions - only define if not already defined */
#ifndef TRACE_EVENT
#define TRACE_EVENT(name, proto, args, tstruct, assign, print) /* stubbed */
#define TRACE_EVENT_FN(name, proto, args, tstruct, assign, print, reg, unreg) /* stubbed */
#define TRACE_EVENT_FLAGS(name, value) /* stubbed */
#define TRACE_EVENT_FN_FLAGS(name, value) /* stubbed */
#endif
#ifndef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(name, proto, args, tstruct, assign, print) /* stubbed */
#endif
#ifndef DEFINE_EVENT
#define DEFINE_EVENT(template, name, proto, args) /* stubbed */
#endif
#ifndef TRACE_EVENT_CONDITION
#define TRACE_EVENT_CONDITION(name, proto, args, cond, tstruct, assign, print) /* stubbed */
#define TRACE_EVENT_CONDITION_FN(name, proto, args, cond, tstruct, assign, print, reg, unreg) /* stubbed */
#endif

/* Basic type definitions needed for compilation */
struct trace_entry {
	unsigned short		type;
	unsigned char		flags;
	unsigned char		preempt_count;
	int			pid;
};

struct trace_iterator;
struct trace_event;
struct trace_seq;

enum print_line_t {
	TRACE_TYPE_PARTIAL_LINE	= 0,
	TRACE_TYPE_HANDLED	= 1,
	TRACE_TYPE_UNHANDLED	= 2,
	TRACE_TYPE_NO_CONSUME	= 3
};

/* Empty function stubs */
static inline const char *trace_print_flags_seq(struct trace_seq *p, const char *delim,
				  unsigned long flags,
				  const struct trace_print_flags *flag_array)
{ return NULL; }

static inline const char *trace_print_symbols_seq(struct trace_seq *p, unsigned long val,
				    const struct trace_print_flags *symbol_array)
{ return NULL; }

static inline const char *trace_print_bitmask_seq(struct trace_seq *p, void *bitmask_ptr,
				    unsigned int bitmask_size)
{ return NULL; }

static inline const char *trace_print_hex_seq(struct trace_seq *p,
				const unsigned char *buf, int len,
				bool concatenate)
{ return NULL; }

static inline const char *trace_print_array_seq(struct trace_seq *p,
				   const void *buf, int count,
				   size_t el_size)
{ return NULL; }

static inline const char *
trace_print_hex_dump_seq(struct trace_seq *p, const char *prefix_str,
			 int prefix_type, int rowsize, int groupsize,
			 const void *buf, size_t len, bool ascii)
{ return NULL; }

static inline int trace_raw_output_prep(struct trace_iterator *iter,
			  struct trace_event *event)
{ return 0; }

static inline void trace_event_printf(struct trace_iterator *iter, const char *fmt, ...)
{ }

static inline int register_trace_event(struct trace_event *event)
{ return 0; }

static inline int unregister_trace_event(struct trace_event *event)
{ return 0; }

static inline enum print_line_t trace_handle_return(struct trace_seq *s)
{ return TRACE_TYPE_HANDLED; }

static inline void tracing_generic_entry_update(struct trace_entry *entry,
						unsigned short type,
						unsigned int trace_ctx)
{ }

static inline void tracing_record_taskinfo(struct trace_iterator *iter)
{ }

static inline void tracing_record_taskinfo_sched_switch(struct trace_iterator *iter,
						      struct task_struct *prev,
						      struct task_struct *next)
{ }

static inline void tracing_record_cmdline(struct task_struct *task)
{ }

static inline void tracing_record_tgid(struct task_struct *task)
{ }

#endif /* _LINUX_TRACE_EVENT_H */