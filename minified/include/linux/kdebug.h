#ifndef _LINUX_KDEBUG_H
#define _LINUX_KDEBUG_H
#include <asm/kdebug.h>
/* struct die_args removed - never used */
int notify_die(enum die_val val, const char *str, struct pt_regs *regs, long err, int trap, int sig);
#endif  
