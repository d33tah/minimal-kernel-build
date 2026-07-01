 
#ifndef _ASM_X86_KDEBUG_H
#define _ASM_X86_KDEBUG_H

#include <linux/notifier.h>

struct pt_regs;

 
enum die_val {
	DIE_OOPS = 1,
	DIE_INT3,
	DIE_DEBUG,
	DIE_TRAP,
	DIE_GPF,
};

enum show_regs_mode {
	SHOW_REGS_ALL
};

extern void die(const char *, struct pt_regs *,long);
void die_addr(const char *str, struct pt_regs *regs, long err, long gp_addr);
extern void show_stack_regs(struct pt_regs *regs);
extern void __show_regs(struct pt_regs *regs, enum show_regs_mode,
			const char *log_lvl);
/* show_iret_regs removed - unused */
/* __die/oops_begin/oops_end removed - 0-caller after die/die_addr/page_fault_oops
 * anchor-stubbed (tick #323) */

#endif  
