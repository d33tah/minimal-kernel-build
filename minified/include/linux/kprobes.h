#ifndef _LINUX_KPROBES_H
#define _LINUX_KPROBES_H
#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/bug.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/freelist.h>
#include <linux/rethook.h>
#include <asm/kprobes.h>

#include <asm-generic/kprobes.h>
typedef int kprobe_opcode_t;
struct arch_specific_insn {
	int dummy;
};

struct kprobe;
struct pt_regs;
struct kretprobe;
struct kretprobe_instance;
typedef int (*kprobe_pre_handler_t) (struct kprobe *, struct pt_regs *);
typedef void (*kprobe_post_handler_t) (struct kprobe *, struct pt_regs *,
				       unsigned long flags);
typedef int (*kretprobe_handler_t) (struct kretprobe_instance *,
				    struct pt_regs *);

struct kprobe {
	struct hlist_node hlist;

	 
	struct list_head list;

	 
	unsigned long nmissed;

	 
	kprobe_opcode_t *addr;

	 
	const char *symbol_name;

	 
	unsigned int offset;

	 
	kprobe_pre_handler_t pre_handler;

	 
	kprobe_post_handler_t post_handler;

	 
	kprobe_opcode_t opcode;

	 
	struct arch_specific_insn ainsn;

	 
	u32 flags;
};

#define KPROBE_FLAG_GONE	1
#define KPROBE_FLAG_DISABLED	2
#define KPROBE_FLAG_OPTIMIZED	4
#define KPROBE_FLAG_FTRACE	8

/* kprobe_gone, kprobe_disabled, kprobe_optimized, kprobe_ftrace removed - unused */

struct kretprobe_holder {
	struct kretprobe	*rp;
	refcount_t		ref;
};

struct kretprobe {
	struct kprobe kp;
	kretprobe_handler_t handler;
	kretprobe_handler_t entry_handler;
	int maxactive;
	int nmissed;
	size_t data_size;
	struct freelist_head freelist;
	struct kretprobe_holder *rph;
};

#define KRETPROBE_MAX_DATA_SIZE	4096

struct kretprobe_instance {
	union {
		struct freelist_node freelist;
		struct rcu_head rcu;
	};
	struct llist_node llist;
	struct kretprobe_holder *rph;
	kprobe_opcode_t *ret_addr;
	void *fp;
	char data[];
};

struct kretprobe_blackpoint {
	const char *name;
	void *addr;
};

struct kprobe_blacklist_entry {
	struct list_head list;
	unsigned long start_addr;
	unsigned long end_addr;
};


static inline int kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	return 0;
}
/* get_kprobe removed - unused */
static inline struct kprobe *kprobe_running(void)
{
	return NULL;
}
#define kprobe_busy_begin()	do {} while (0)
#define kprobe_busy_end()	do {} while (0)

/* register_kprobe, register_kprobes, unregister_kprobe, unregister_kprobes removed - unused */
/* register_kretprobe, register_kretprobes, unregister_kretprobe, unregister_kretprobes removed - unused */

static inline void kprobe_flush_task(struct task_struct *tk)
{
}
static inline void kprobe_free_init_mem(void)
{
}

/* disable_kprobe, enable_kprobe, within_kprobe_blacklist, kprobe_get_kallsym removed - unused */
/* disable_kretprobe, enable_kretprobe removed - unused */

static inline bool is_kprobe_insn_slot(unsigned long addr)
{
	return false;
}

static inline bool is_kprobe_optinsn_slot(unsigned long addr)
{
	return false;
}

/* is_kretprobe_trampoline, kretprobe_find_ret_addr removed - unused */

static nokprobe_inline bool kprobe_page_fault(struct pt_regs *regs,
					      unsigned int trap)
{
	if (!IS_ENABLED(CONFIG_KPROBES))
		return false;
	if (user_mode(regs))
		return false;
	 
	if (preemptible())
		return false;
	if (!kprobe_running())
		return false;
	return kprobe_fault_handler(regs, trap);
}

#endif  
