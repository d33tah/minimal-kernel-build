
#ifndef _ASM_X86_KDEBUG_H
#define _ASM_X86_KDEBUG_H

#include <linux/rwsem.h>

struct pt_regs;

enum show_regs_mode { SHOW_REGS_ALL = 2 };

extern void die(const char *, struct pt_regs *, long);
extern int __die(const char *, struct pt_regs *, long);
extern unsigned long oops_begin(void);
extern void oops_end(unsigned long, struct pt_regs *, int signr);

#endif
