

#ifndef _LINUX_AUDIT_H_
#define _LINUX_AUDIT_H_

#include <linux/sched.h>
#include <linux/ptrace.h>

#include <uapi/linux/audit.h>

struct audit_buffer;
struct audit_context;
struct kern_ipc_perm;

/* Reduced audit_ntp_type enum - NTP audit not used in minimal kernel */
enum audit_ntp_type {
	AUDIT_NTP_NVALS,  /* Only value needed for stub function signature */
};

struct audit_ntp_data {};

/* Reduced audit_nfcfgop enum - netfilter audit not used in minimal kernel */
enum audit_nfcfgop {
	AUDIT_NFT_OP_INVALID,  /* Only value needed for stub function signature */
};

struct filename;

#define	AUDIT_TYPE_UNKNOWN	0
#define	AUDIT_TYPE_NORMAL	1
#define	AUDIT_TYPE_PARENT	2
#define	AUDIT_TYPE_CHILD_DELETE 3
#define	AUDIT_TYPE_CHILD_CREATE 4

#define AUDIT_INODE_PARENT	1
#define AUDIT_INODE_HIDDEN	2
#define AUDIT_INODE_NOEVAL	4

#define AUDIT_OFF	0
#define AUDIT_ON	1
#define AUDIT_LOCKED	2

/* audit_log, audit_log_start, audit_log_format, audit_log_end removed - unused */
/* audit_signal_info removed - unused */

#define audit_enabled AUDIT_OFF

static inline int audit_alloc(struct task_struct *task)
{
	return 0;
}
/* audit_alloc_kernel removed - unused */
static inline void audit_free(struct task_struct *task)
{ }
/* audit_uring_entry, audit_uring_exit removed - unused */
static inline void audit_syscall_entry(int major, unsigned long a0,
				       unsigned long a1, unsigned long a2,
				       unsigned long a3)
{ }
static inline void audit_syscall_exit(void *pt_regs)
{ }
/* audit_dummy_context removed - unused */
static inline void audit_set_context(struct task_struct *task, struct audit_context *ctx)
{ }
static inline struct audit_context *audit_context(void)
{
	return NULL;
}
static inline struct filename *audit_reusename(const __user char *name)
{
	return NULL;
}
static inline void audit_getname(struct filename *name)
{ }
static inline void audit_inode(struct filename *name,
				const struct dentry *dentry,
				unsigned int aflags)
{ }
static inline void audit_file(struct file *file)
{
}
/* audit_inode_parent_hidden removed - unused */
static inline void audit_inode_child(struct inode *parent,
				     const struct dentry *dentry,
				     const unsigned char type)
{ }
/* audit_core_dumps, audit_seccomp, audit_seccomp_actions_logged removed - unused */
/* audit_ipc_obj, audit_ipc_set_perm removed - unused */
static inline void audit_bprm(struct linux_binprm *bprm)
{ }
/* audit_socketcall, audit_socketcall_compat, audit_fd_pair, audit_sockaddr removed - unused */
/* audit_mq_open, audit_mq_sendrecv, audit_mq_notify, audit_mq_getsetattr removed - unused */
/* audit_log_bprm_fcaps, audit_log_capset removed - unused */
static inline void audit_mmap_fd(int fd, int flags)
{ }

static inline void audit_openat2_how(struct open_how *how)
{ }

/* audit_log_kern_module, audit_fanotify, audit_tk_injoffset removed - unused */
/* audit_ntp_*, audit_ptrace, audit_log_nfcfg removed - unused */

#define audit_n_rules 0
#define audit_signals 0

/* audit_loginuid_set removed - unused */

#endif
