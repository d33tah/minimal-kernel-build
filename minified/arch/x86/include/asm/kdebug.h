 
#ifndef _ASM_X86_KDEBUG_H
#define _ASM_X86_KDEBUG_H

/* notifier.h inlined */
#include <linux/rwsem.h>
#include <linux/srcu.h>
#ifndef _LINUX_NOTIFIER_H
#define _LINUX_NOTIFIER_H
struct notifier_block;
typedef	int (*notifier_fn_t)(struct notifier_block *nb,
			unsigned long action, void *data);
struct notifier_block {
	notifier_fn_t notifier_call;
	struct notifier_block __rcu *next;
	int priority;
};
#define NOTIFY_DONE		0x0000
#endif

struct pt_regs;

enum die_val {
	DIE_OOPS = 1,
	DIE_INT3,
	DIE_DEBUG,
	DIE_TRAP = 8,
	DIE_GPF,
};

enum show_regs_mode {
	SHOW_REGS_ALL = 2
};

extern void die(const char *, struct pt_regs *,long);
void die_addr(const char *str, struct pt_regs *regs, long err, long gp_addr);
extern int __must_check __die(const char *, struct pt_regs *, long);
extern void show_stack_regs(struct pt_regs *regs);
extern void __show_regs(struct pt_regs *regs, enum show_regs_mode,
			const char *log_lvl);
extern unsigned long oops_begin(void);
extern void oops_end(unsigned long, struct pt_regs *, int signr);

#endif  
