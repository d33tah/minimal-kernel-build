/* Minimal compat.h - COMPAT_32BIT enabled but most code not used */
#ifndef _LINUX_COMPAT_H
#define _LINUX_COMPAT_H
#include <linux/types.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/param.h>
#include <linux/uio.h>
#include <linux/compiler.h>
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
#ifndef __SC_DELOUSE
#define __SC_DELOUSE(t,v) ((__force t)(unsigned long)(v))
#endif
struct compat_iovec;
struct compat_sigaltstack;
struct compat_tms;
struct compat_sigaction;
struct compat_siginfo;
struct compat_rlimit;
#ifndef compat_sigset_t
typedef struct { compat_sigset_word sig[_COMPAT_NSIG_WORDS]; } compat_sigset_t;
#endif
#define _COMPAT_NSIG_WORDS	(_COMPAT_NSIG / _COMPAT_NSIG_BPW)
/* compat_uid_t, compat_gid_t removed - unused */
#define in_compat_syscall in_compat_syscall
static inline bool in_compat_syscall(void) { return false; }
#endif
