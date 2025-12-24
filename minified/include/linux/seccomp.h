#ifndef _LINUX_SECCOMP_H
#define _LINUX_SECCOMP_H
#include <linux/types.h>
#define SECCOMP_MODE_DISABLED 0
#define SECCOMP_FILTER_FLAG_TSYNC (1UL << 0)
#define SECCOMP_FILTER_FLAG_LOG (1UL << 1)
#define SECCOMP_FILTER_FLAG_SPEC_ALLOW (1UL << 2)
#define SECCOMP_FILTER_FLAG_NEW_LISTENER (1UL << 3)
#define SECCOMP_FILTER_FLAG_TSYNC_ESRCH (1UL << 4)
#define SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV (1UL << 5)
struct seccomp_data { int nr; __u32 arch; __u64 instruction_pointer; __u64 args[6]; };
#define SECCOMP_FILTER_FLAG_MASK (SECCOMP_FILTER_FLAG_TSYNC | SECCOMP_FILTER_FLAG_LOG | SECCOMP_FILTER_FLAG_SPEC_ALLOW | SECCOMP_FILTER_FLAG_NEW_LISTENER | SECCOMP_FILTER_FLAG_TSYNC_ESRCH | SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV)
#define SECCOMP_NOTIFY_ADDFD_SIZE_VER0 24
#define SECCOMP_NOTIFY_ADDFD_SIZE_LATEST SECCOMP_NOTIFY_ADDFD_SIZE_VER0
#include <linux/errno.h>
struct seccomp { };
struct seccomp_filter { };
struct seccomp_data;
static inline int __secure_computing(const struct seccomp_data *sd) { return 0; }
static inline void seccomp_filter_release(struct task_struct *tsk) { }
#endif  
