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

static inline bool kprobe_gone(struct kprobe *p)
{
	return p->flags & KPROBE_FLAG_GONE;
}

static inline bool kprobe_disabled(struct kprobe *p)
{
	return p->flags & (KPROBE_FLAG_DISABLED | KPROBE_FLAG_GONE);
}

static inline bool kprobe_optimized(struct kprobe *p)
{
	return p->flags & KPROBE_FLAG_OPTIMIZED;
}

static inline bool kprobe_ftrace(struct kprobe *p)
{
	return p->flags & KPROBE_FLAG_FTRACE;
}

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
static inline struct kprobe *get_kprobe(void *addr)
{
	return NULL;
}
static inline struct kprobe *kprobe_running(void)
{
	return NULL;
}
#define kprobe_busy_begin()	do {} while (0)
#define kprobe_busy_end()	do {} while (0)

static inline int register_kprobe(struct kprobe *p)
{
	return -EOPNOTSUPP;
}
static inline int register_kprobes(struct kprobe **kps, int num)
{
	return -EOPNOTSUPP;
}
static inline void unregister_kprobe(struct kprobe *p)
{
}
static inline void unregister_kprobes(struct kprobe **kps, int num)
{
}
static inline int register_kretprobe(struct kretprobe *rp)
{
	return -EOPNOTSUPP;
}
static inline int register_kretprobes(struct kretprobe **rps, int num)
{
	return -EOPNOTSUPP;
}
static inline void unregister_kretprobe(struct kretprobe *rp)
{
}
static inline void unregister_kretprobes(struct kretprobe **rps, int num)
{
}
static inline void kprobe_flush_task(struct task_struct *tk)
{
}
static inline void kprobe_free_init_mem(void)
{
}
static inline int disable_kprobe(struct kprobe *kp)
{
	return -EOPNOTSUPP;
}
static inline int enable_kprobe(struct kprobe *kp)
{
	return -EOPNOTSUPP;
}

static inline bool within_kprobe_blacklist(unsigned long addr)
{
	return true;
}
static inline int kprobe_get_kallsym(unsigned int symnum, unsigned long *value,
				     char *type, char *sym)
{
	return -ERANGE;
}

static inline int disable_kretprobe(struct kretprobe *rp)
{
	return disable_kprobe(&rp->kp);
}
static inline int enable_kretprobe(struct kretprobe *rp)
{
	return enable_kprobe(&rp->kp);
}

static inline bool is_kprobe_insn_slot(unsigned long addr)
{
	return false;
}

static inline bool is_kprobe_optinsn_slot(unsigned long addr)
{
	return false;
}

static nokprobe_inline bool is_kretprobe_trampoline(unsigned long addr)
{
	return false;
}

static nokprobe_inline
unsigned long kretprobe_find_ret_addr(struct task_struct *tsk, void *fp,
				      struct llist_node **cur)
{
	return 0;
}

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
