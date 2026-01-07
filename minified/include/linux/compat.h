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
#include <linux/unistd.h>
#include <asm/compat.h>
#include <asm/siginfo.h>
#include <asm/signal.h>
#include <asm/syscall_wrapper.h>
/* __SC_DELOUSE removed - unused */
#ifndef compat_sigset_t
typedef struct { compat_sigset_word sig[_COMPAT_NSIG_WORDS]; } compat_sigset_t;
#endif
#define _COMPAT_NSIG_WORDS	(_COMPAT_NSIG / _COMPAT_NSIG_BPW)
#endif
