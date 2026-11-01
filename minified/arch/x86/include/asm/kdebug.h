#ifndef _ASM_X86_KDEBUG_H
#define _ASM_X86_KDEBUG_H

struct pt_regs;

extern void die(const char *, struct pt_regs *, long);
void die_addr(const char *str, struct pt_regs *regs, long err, long gp_addr);
extern unsigned long oops_begin(void);
extern void oops_end(unsigned long, struct pt_regs *, int signr);

#endif
