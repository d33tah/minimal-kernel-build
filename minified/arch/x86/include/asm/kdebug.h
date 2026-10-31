
#ifndef _ASM_X86_KDEBUG_H
#define _ASM_X86_KDEBUG_H

struct pt_regs;

extern void die(const char *, struct pt_regs *, long);
void die_addr(const char *str, struct pt_regs *regs, long err, long gp_addr);
/* show_iret_regs removed - unused */
/* __die/oops_begin/oops_end removed - 0-caller after die/die_addr/page_fault_oops
 * anchor-stubbed (tick #323) */

#endif
