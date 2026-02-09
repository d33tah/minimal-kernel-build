/* Minimal compat.h - COMPAT_32BIT enabled but most code not used */
#ifndef _LINUX_COMPAT_H
#define _LINUX_COMPAT_H
#include <linux/types.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/param.h>
#include <linux/uio.h>
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <asm/unistd.h>
/* asm/compat.h inlined - just provides includes */
#include <linux/sched/task_stack.h>
#include <asm/processor.h>
#include <asm/unistd.h>
#include <asm-generic/siginfo.h>
#include <asm/signal.h>
#include <asm/syscall_wrapper.h>
/* __SC_DELOUSE, compat_sigset_t typedef, _COMPAT_NSIG_WORDS removed - unused */
#endif
