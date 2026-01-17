 
#ifndef _ASM_X86_KDEBUG_H
#define _ASM_X86_KDEBUG_H

#include <linux/notifier.h>

struct pt_regs;

/* Reduced die_val enum - unused values removed */
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
/* show_iret_regs removed - unused */
extern unsigned long oops_begin(void);
extern void oops_end(unsigned long, struct pt_regs *, int signr);

#endif  
