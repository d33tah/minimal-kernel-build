#ifndef _UAPI_LINUX_SECCOMP_H
#define _UAPI_LINUX_SECCOMP_H

/* Minimal seccomp.h - seccomp disabled, only mode/flag/data constants needed */

#include <linux/types.h>

#define SECCOMP_MODE_DISABLED	0

/* Filter flags needed by include/linux/seccomp.h SECCOMP_FILTER_FLAG_MASK */
#define SECCOMP_FILTER_FLAG_TSYNC		(1UL << 0)
#define SECCOMP_FILTER_FLAG_LOG			(1UL << 1)
#define SECCOMP_FILTER_FLAG_SPEC_ALLOW		(1UL << 2)
#define SECCOMP_FILTER_FLAG_NEW_LISTENER	(1UL << 3)
#define SECCOMP_FILTER_FLAG_TSYNC_ESRCH		(1UL << 4)
#define SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV	(1UL << 5)

/* seccomp_data needed by include/linux/ptrace.h syscall_info struct */
struct seccomp_data {
	int nr;
	__u32 arch;
	__u64 instruction_pointer;
	__u64 args[6];
};

#endif  
