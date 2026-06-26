#ifndef TRACEPOINT_DEFS_H
#define TRACEPOINT_DEFS_H 1


#include <linux/atomic.h>
#include <linux/jump_label.h>

struct trace_print_flags {
	unsigned long		mask;
	const char		*name;
};

#endif
