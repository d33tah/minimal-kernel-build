/* Minimal compat.h - COMPAT_32BIT enabled but most code not used */
#ifndef _LINUX_COMPAT_H
#define _LINUX_COMPAT_H

#include <linux/types.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/param.h>
#include <linux/uio.h>


struct file;
struct pid;
struct cred;

#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>
#include <asm/compat.h>
#include <asm/siginfo.h>
#include <asm/signal.h>
#include <asm/syscall_wrapper.h>


/* Forward declarations (compat_* 0-ref fwd-decls removed) */
/* _COMPAT_NSIG_WORDS removed - 0-ref tree-wide */

#endif /* _LINUX_COMPAT_H */
