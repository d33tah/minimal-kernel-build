 
#ifndef _LINUX_COREDUMP_H
#define _LINUX_COREDUMP_H

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <asm/siginfo.h>

static inline void do_coredump(const kernel_siginfo_t *siginfo) {}

static inline void validate_coredump_safety(void) {}

#endif  
