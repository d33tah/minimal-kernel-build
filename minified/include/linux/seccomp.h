#ifndef _LINUX_SECCOMP_H
#define _LINUX_SECCOMP_H

#include <linux/types.h>

/* From uapi/linux/seccomp.h - inlined */
#define SECCOMP_MODE_DISABLED	0
#define SECCOMP_FILTER_FLAG_TSYNC		(1UL << 0)
#define SECCOMP_FILTER_FLAG_LOG			(1UL << 1)
#define SECCOMP_FILTER_FLAG_SPEC_ALLOW		(1UL << 2)
#define SECCOMP_FILTER_FLAG_NEW_LISTENER	(1UL << 3)
#define SECCOMP_FILTER_FLAG_TSYNC_ESRCH		(1UL << 4)
#define SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV	(1UL << 5)

struct seccomp_data {
	int nr;
	__u32 arch;
	__u64 instruction_pointer;
	__u64 args[6];
};

#define SECCOMP_FILTER_FLAG_MASK	(SECCOMP_FILTER_FLAG_TSYNC | \
					 SECCOMP_FILTER_FLAG_LOG | \
					 SECCOMP_FILTER_FLAG_SPEC_ALLOW | \
					 SECCOMP_FILTER_FLAG_NEW_LISTENER | \
					 SECCOMP_FILTER_FLAG_TSYNC_ESRCH | \
					 SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV)

#define SECCOMP_NOTIFY_ADDFD_SIZE_VER0 24
#define SECCOMP_NOTIFY_ADDFD_SIZE_LATEST SECCOMP_NOTIFY_ADDFD_SIZE_VER0


#include <linux/errno.h>

struct seccomp { };
struct seccomp_filter { };
struct seccomp_data;

static inline int secure_computing(void) { return 0; }
static inline int __secure_computing(const struct seccomp_data *sd) { return 0; }

static inline long prctl_get_seccomp(void)
{
	return -EINVAL;
}

static inline long prctl_set_seccomp(unsigned long arg2, char __user *arg3)
{
	return -EINVAL;
}

static inline int seccomp_mode(struct seccomp *s)
{
	return SECCOMP_MODE_DISABLED;
}

static inline void seccomp_filter_release(struct task_struct *tsk)
{
	return;
}
static inline void get_seccomp_filter(struct task_struct *tsk)
{
	return;
}

static inline long seccomp_get_filter(struct task_struct *task,
				      unsigned long n, void __user *data)
{
	return -EINVAL;
}
static inline long seccomp_get_metadata(struct task_struct *task,
					unsigned long filter_off,
					void __user *data)
{
	return -EINVAL;
}

#endif  
