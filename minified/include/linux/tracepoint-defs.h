#ifndef TRACEPOINT_DEFS_H
#define TRACEPOINT_DEFS_H 1
#include <linux/atomic.h>
#include <linux/jump_label.h>
struct static_call_key;
struct tracepoint_func { void *func; void *data; int prio; };
struct tracepoint {
	const char *name;
	struct static_key key;
	struct static_call_key *static_call_key;
	void *static_call_tramp;
	void *iterator;
	int (*regfunc)(void);
	void (*unregfunc)(void);
	struct tracepoint_func __rcu *funcs;
};
#define DECLARE_TRACEPOINT(tp) extern struct tracepoint __tracepoint_##tp
#define tracepoint_enabled(tracepoint) false
#endif
