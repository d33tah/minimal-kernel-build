/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ftrace header.  For implementation details beyond the random comments
 * scattered below, see: Documentation/trace/ftrace-design.rst
 */

#ifndef _LINUX_FTRACE_H
#define _LINUX_FTRACE_H

#include <linux/trace_recursion.h>
#include <linux/trace_clock.h>
#include <linux/jump_label.h>
#include <linux/kallsyms.h>
#include <linux/linkage.h>
#include <linux/bitops.h>
#include <linux/ptrace.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/fs.h>

#include <asm/ftrace.h>

/*
 * If the arch supports passing the variable contents of
 * function_trace_op as the third parameter back from the
 * mcount call, then the arch should define this as 1.
 */
#ifndef ARCH_SUPPORTS_FTRACE_OPS
#define ARCH_SUPPORTS_FTRACE_OPS 0
#endif

static inline void ftrace_boot_snapshot(void) { }


/* Main tracing buffer and events set up */
static inline void trace_init(void) { }
static inline void early_trace_init(void) { }

struct module;
struct ftrace_hash;
struct ftrace_direct_func;

#if defined(CONFIG_FUNCTION_TRACER) && defined(CONFIG_MODULES) && \
	defined(CONFIG_DYNAMIC_FTRACE)
const char *
ftrace_mod_address_lookup(unsigned long addr, unsigned long *size,
		   unsigned long *off, char **modname, char *sym);
#else
static inline const char *
ftrace_mod_address_lookup(unsigned long addr, unsigned long *size,
		   unsigned long *off, char **modname, char *sym)
{
	return NULL;
}
#endif

static inline int ftrace_mod_get_kallsym(unsigned int symnum, unsigned long *value,
					 char *type, char *name,
					 char *module_name, int *exported)
{
	return -1;
}

/*
 * (un)register_ftrace_function must be a macro since the ops parameter
 * must not be evaluated.
 */
#define register_ftrace_function(ops) ({ 0; })
#define unregister_ftrace_function(ops) ({ 0; })
static inline void ftrace_kill(void) { }
static inline void ftrace_free_init_mem(void) { }
static inline void ftrace_free_mem(struct module *mod, void *start, void *end) { }
static inline int ftrace_lookup_symbols(const char **sorted_syms, size_t cnt, unsigned long *addrs)
{
	return -EOPNOTSUPP;
}

struct ftrace_func_entry {
	struct hlist_node hlist;
	unsigned long ip;
	unsigned long direct; /* for direct lookup only */
};

struct dyn_ftrace;

struct ftrace_ops;
# define ftrace_direct_func_count 0
static inline int register_ftrace_direct(unsigned long ip, unsigned long addr)
{
	return -ENOTSUPP;
}
static inline int unregister_ftrace_direct(unsigned long ip, unsigned long addr)
{
	return -ENOTSUPP;
}
static inline int modify_ftrace_direct(unsigned long ip,
				       unsigned long old_addr, unsigned long new_addr)
{
	return -ENOTSUPP;
}
static inline struct ftrace_direct_func *ftrace_find_direct_func(unsigned long addr)
{
	return NULL;
}
static inline int ftrace_modify_direct_caller(struct ftrace_func_entry *entry,
					      struct dyn_ftrace *rec,
					      unsigned long old_addr,
					      unsigned long new_addr)
{
	return -ENODEV;
}
static inline unsigned long ftrace_find_rec_direct(unsigned long ip)
{
	return 0;
}
static inline int register_ftrace_direct_multi(struct ftrace_ops *ops, unsigned long addr)
{
	return -ENODEV;
}
static inline int unregister_ftrace_direct_multi(struct ftrace_ops *ops, unsigned long addr)
{
	return -ENODEV;
}
static inline int modify_ftrace_direct_multi(struct ftrace_ops *ops, unsigned long addr)
{
	return -ENODEV;
}


static inline void stack_tracer_disable(void) { }
static inline void stack_tracer_enable(void) { }

static inline int skip_trace(unsigned long ip) { return 0; }
static inline void ftrace_disable_daemon(void) { }
static inline void ftrace_enable_daemon(void) { }
static inline void ftrace_module_init(struct module *mod) { }
static inline void ftrace_module_enable(struct module *mod) { }
static inline void ftrace_release_mod(struct module *mod) { }
static inline int ftrace_text_reserved(const void *start, const void *end)
{
	return 0;
}
static inline unsigned long ftrace_location(unsigned long ip)
{
	return 0;
}

/*
 * Again users of functions that have ftrace_ops may not
 * have them defined when ftrace is not enabled, but these
 * functions may still be called. Use a macro instead of inline.
 */
#define ftrace_regex_open(ops, flag, inod, file) ({ -ENODEV; })
#define ftrace_set_early_filter(ops, buf, enable) do { } while (0)
#define ftrace_set_filter_ip(ops, ip, remove, reset) ({ -ENODEV; })
#define ftrace_set_filter_ips(ops, ips, cnt, remove, reset) ({ -ENODEV; })
#define ftrace_set_filter(ops, buf, len, reset) ({ -ENODEV; })
#define ftrace_set_notrace(ops, buf, len, reset) ({ -ENODEV; })
#define ftrace_free_filter(ops) do { } while (0)
#define ftrace_ops_set_global_filter(ops) do { } while (0)

static inline ssize_t ftrace_filter_write(struct file *file, const char __user *ubuf,
			    size_t cnt, loff_t *ppos) { return -ENODEV; }
static inline ssize_t ftrace_notrace_write(struct file *file, const char __user *ubuf,
			     size_t cnt, loff_t *ppos) { return -ENODEV; }
static inline int
ftrace_regex_release(struct inode *inode, struct file *file) { return -ENODEV; }

static inline bool is_ftrace_trampoline(unsigned long addr)
{
	return false;
}


/* totally disable ftrace - can not re-enable after this */
void ftrace_kill(void);

static inline void tracer_disable(void)
{
}

/*
 * Ftrace disable/restore without lock. Some synchronization mechanism
 * must be used to prevent ftrace_enabled to be changed between
 * disable/restore.
 */
static inline int __ftrace_enabled_save(void)
{
	return 0;
}

static inline void __ftrace_enabled_restore(int enabled)
{
}

/* All archs should have this, but we define it for consistency */
#ifndef ftrace_return_address0
# define ftrace_return_address0 __builtin_return_address(0)
#endif

/* Archs may use other ways for ADDR1 and beyond */
#ifndef ftrace_return_address
#  define ftrace_return_address(n) 0UL
#endif

#define CALLER_ADDR0 ((unsigned long)ftrace_return_address0)
#define CALLER_ADDR1 ((unsigned long)ftrace_return_address(1))
#define CALLER_ADDR2 ((unsigned long)ftrace_return_address(2))
#define CALLER_ADDR3 ((unsigned long)ftrace_return_address(3))
#define CALLER_ADDR4 ((unsigned long)ftrace_return_address(4))
#define CALLER_ADDR5 ((unsigned long)ftrace_return_address(5))
#define CALLER_ADDR6 ((unsigned long)ftrace_return_address(6))

static inline unsigned long get_lock_parent_ip(void)
{
	unsigned long addr = CALLER_ADDR0;

	if (!in_lock_functions(addr))
		return addr;
	addr = CALLER_ADDR1;
	if (!in_lock_functions(addr))
		return addr;
	return CALLER_ADDR2;
}

/*
 * Use defines instead of static inlines because some arches will make code out
 * of the CALLER_ADDR, when we really want these to be a real nop.
 */
# define trace_preempt_on(a0, a1) do { } while (0)
# define trace_preempt_off(a0, a1) do { } while (0)

static inline void ftrace_init(void) { }

/*
 * Structure that defines an entry function trace.
 * It's already packed but the attribute "packed" is needed
 * to remove extra padding at the end.
 */
struct ftrace_graph_ent {
	unsigned long func; /* Current function */
	int depth;
} __packed;

/*
 * Structure that defines a return function trace.
 * It's already packed but the attribute "packed" is needed
 * to remove extra padding at the end.
 */
struct ftrace_graph_ret {
	unsigned long func; /* Current function */
	int depth;
	/* Number of functions that overran the depth limit for current task */
	unsigned int overrun;
	unsigned long long calltime;
	unsigned long long rettime;
} __packed;

/* Type of the callback handlers for tracing function graph*/
typedef void (*trace_func_graph_ret_t)(struct ftrace_graph_ret *); /* return */
typedef int (*trace_func_graph_ent_t)(struct ftrace_graph_ent *); /* entry */

extern int ftrace_graph_entry_stub(struct ftrace_graph_ent *trace);


#define __notrace_funcgraph

static inline void ftrace_graph_init_task(struct task_struct *t) { }
static inline void ftrace_graph_exit_task(struct task_struct *t) { }
static inline void ftrace_graph_init_idle_task(struct task_struct *t, int cpu) { }

/* Define as macros as fgraph_ops may not be defined */
#define register_ftrace_graph(ops) ({ -1; })
#define unregister_ftrace_graph(ops) do { } while (0)

static inline unsigned long
ftrace_graph_ret_addr(struct task_struct *task, int *idx, unsigned long ret,
		      unsigned long *retp)
{
	return ret;
}

static inline void pause_graph_tracing(void) { }
static inline void unpause_graph_tracing(void) { }

static inline void  disable_trace_on_warning(void) { }


#endif /* _LINUX_FTRACE_H */
