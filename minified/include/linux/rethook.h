#ifndef _LINUX_RETHOOK_H
#define _LINUX_RETHOOK_H

#include <linux/compiler.h>
#include <linux/freelist.h>
#include <linux/kallsyms.h>
#include <linux/llist.h>
#include <linux/rcupdate.h>
#include <linux/refcount.h>

struct rethook_node;

typedef void (*rethook_handler_t) (struct rethook_node *, void *, struct pt_regs *);

struct rethook {
	void			*data;
	rethook_handler_t	handler;
	struct freelist_head	pool;
	refcount_t		ref;
	struct rcu_head		rcu;
};

struct rethook_node {
	union {
		struct freelist_node freelist;
		struct rcu_head      rcu;
	};
	struct llist_node	llist;
	struct rethook		*rethook;
	unsigned long		ret_addr;
	unsigned long		frame;
};

/* All rethook functions removed - CONFIG_RETHOOK is disabled */
void arch_rethook_trampoline(void);

static inline bool is_rethook_trampoline(unsigned long addr)
{
	return addr == (unsigned long)dereference_symbol_descriptor(arch_rethook_trampoline);
}

#define rethook_flush_task(tsk)	do { } while (0)

#endif

