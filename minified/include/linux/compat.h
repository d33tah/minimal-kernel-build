/* Minimal compat.h - COMPAT_32BIT enabled but most code not used */
#ifndef _LINUX_COMPAT_H
#define _LINUX_COMPAT_H

#include <linux/types.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/param.h>
#include <linux/sem.h>
#include <linux/socket.h>
#include <linux/fs.h>
#include <linux/aio_abi.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>
#include <asm/compat.h>
#include <asm/siginfo.h>
#include <asm/signal.h>
#include <asm/syscall_wrapper.h>

#ifndef COMPAT_USE_64BIT_TIME
#define COMPAT_USE_64BIT_TIME 0
#endif

#ifndef __SC_DELOUSE
#define __SC_DELOUSE(t,v) ((__force t)(unsigned long)(v))
#endif

/* Forward declarations */
struct compat_iovec;
struct compat_sigaltstack;
struct compat_tms;
struct compat_sigaction;
struct compat_siginfo;
struct compat_rlimit;

#ifndef compat_sigset_t
typedef struct {
	compat_sigset_word	sig[_COMPAT_NSIG_WORDS];
} compat_sigset_t;
#endif

#define _COMPAT_NSIG_WORDS	(_COMPAT_NSIG / _COMPAT_NSIG_BPW)

#ifndef compat_user_stack_pointer
#define compat_user_stack_pointer() current_user_stack_pointer()
#endif
#ifndef COMPAT_MINSIGSTKSZ
#define COMPAT_MINSIGSTKSZ	MINSIGSTKSZ
#endif

typedef __compat_uid32_t	compat_uid_t;
typedef __compat_gid32_t	compat_gid_t;

#define is_compat_task() (0)
#define in_compat_syscall in_compat_syscall
static inline bool in_compat_syscall(void) { return false; }

#define BITS_PER_COMPAT_LONG    (8*sizeof(compat_long_t))
#define BITS_TO_COMPAT_LONGS(bits) DIV_ROUND_UP(bits, BITS_PER_COMPAT_LONG)

#ifndef compat_ptr
static inline void __user *compat_ptr(compat_uptr_t uptr)
{
	return (void __user *)(unsigned long)uptr;
}
#endif

static inline compat_uptr_t ptr_to_compat(void __user *uptr)
{
	return (u32)(unsigned long)uptr;
}

#endif /* _LINUX_COMPAT_H */
