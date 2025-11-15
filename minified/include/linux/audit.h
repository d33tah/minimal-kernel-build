

#ifndef _LINUX_AUDIT_H_
#define _LINUX_AUDIT_H_

#include <linux/sched.h>
#include <linux/ptrace.h>

#include <uapi/linux/audit.h>

struct audit_buffer;
struct audit_context;
struct kern_ipc_perm;

enum audit_ntp_type {
	AUDIT_NTP_OFFSET,
	AUDIT_NTP_FREQ,
	AUDIT_NTP_STATUS,
	AUDIT_NTP_TAI,
	AUDIT_NTP_TICK,
	AUDIT_NTP_ADJUST,

	AUDIT_NTP_NVALS
};

struct audit_ntp_data {};

enum audit_nfcfgop {
	AUDIT_XT_OP_REGISTER,
	AUDIT_XT_OP_REPLACE,
	AUDIT_XT_OP_UNREGISTER,
	AUDIT_NFT_OP_TABLE_REGISTER,
	AUDIT_NFT_OP_TABLE_UNREGISTER,
	AUDIT_NFT_OP_CHAIN_REGISTER,
	AUDIT_NFT_OP_CHAIN_UNREGISTER,
	AUDIT_NFT_OP_RULE_REGISTER,
	AUDIT_NFT_OP_RULE_UNREGISTER,
	AUDIT_NFT_OP_SET_REGISTER,
	AUDIT_NFT_OP_SET_UNREGISTER,
	AUDIT_NFT_OP_SETELEM_REGISTER,
	AUDIT_NFT_OP_SETELEM_UNREGISTER,
	AUDIT_NFT_OP_GEN_REGISTER,
	AUDIT_NFT_OP_OBJ_REGISTER,
	AUDIT_NFT_OP_OBJ_UNREGISTER,
	AUDIT_NFT_OP_OBJ_RESET,
	AUDIT_NFT_OP_FLOWTABLE_REGISTER,
	AUDIT_NFT_OP_FLOWTABLE_UNREGISTER,
	AUDIT_NFT_OP_INVALID,
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

static inline __printf(4, 5)
void audit_log(struct audit_context *ctx, gfp_t gfp_mask, int type,
	       const char *fmt, ...)
{ }
static inline struct audit_buffer *audit_log_start(struct audit_context *ctx,
						   gfp_t gfp_mask, int type)
{
	return NULL;
}
static inline __printf(2, 3)
void audit_log_format(struct audit_buffer *ab, const char *fmt, ...)
{ }
static inline void audit_log_end(struct audit_buffer *ab)
{ }
static inline void audit_log_n_hex(struct audit_buffer *ab,
				   const unsigned char *buf, size_t len)
{ }
static inline void audit_log_n_string(struct audit_buffer *ab,
				      const char *buf, size_t n)
{ }
static inline void  audit_log_n_untrustedstring(struct audit_buffer *ab,
						const char *string, size_t n)
{ }
static inline void audit_log_untrustedstring(struct audit_buffer *ab,
					     const char *string)
{ }
static inline void audit_log_d_path(struct audit_buffer *ab,
				    const char *prefix,
				    const struct path *path)
{ }
static inline void audit_log_key(struct audit_buffer *ab, char *key)
{ }
static inline void audit_log_path_denied(int type, const char *operation)
{ }
static inline int audit_log_task_context(struct audit_buffer *ab)
{
	return 0;
}
static inline void audit_log_task_info(struct audit_buffer *ab)
{ }

static inline kuid_t audit_get_loginuid(struct task_struct *tsk)
{
	return INVALID_UID;
}

static inline unsigned int audit_get_sessionid(struct task_struct *tsk)
{
	return AUDIT_SID_UNSET;
}

#define audit_enabled AUDIT_OFF

static inline int audit_signal_info(int sig, struct task_struct *t)
{
	return 0;
}

#define audit_is_compat(arch)  false

static inline int audit_alloc(struct task_struct *task)
{
	return 0;
}
static inline int audit_alloc_kernel(struct task_struct *task)
{
	return 0;
}
static inline void audit_free(struct task_struct *task)
{ }
static inline void audit_uring_entry(u8 op)
{ }
static inline void audit_uring_exit(int success, long code)
{ }
static inline void audit_syscall_entry(int major, unsigned long a0,
				       unsigned long a1, unsigned long a2,
				       unsigned long a3)
{ }
static inline void audit_syscall_exit(void *pt_regs)
{ }
static inline bool audit_dummy_context(void)
{
	return true;
}
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
static inline void audit_inode_parent_hidden(struct filename *name,
				const struct dentry *dentry)
{ }
static inline void audit_inode_child(struct inode *parent,
				     const struct dentry *dentry,
				     const unsigned char type)
{ }
static inline void audit_core_dumps(long signr)
{ }
static inline void audit_seccomp(unsigned long syscall, long signr, int code)
{ }
static inline void audit_seccomp_actions_logged(const char *names,
						const char *old_names, int res)
{ }
static inline void audit_ipc_obj(struct kern_ipc_perm *ipcp)
{ }
static inline void audit_ipc_set_perm(unsigned long qbytes, uid_t uid,
					gid_t gid, umode_t mode)
{ }
static inline void audit_bprm(struct linux_binprm *bprm)
{ }
static inline int audit_socketcall(int nargs, unsigned long *args)
{
	return 0;
}

static inline int audit_socketcall_compat(int nargs, u32 *args)
{
	return 0;
}

static inline void audit_fd_pair(int fd1, int fd2)
{ }
static inline int audit_sockaddr(int len, void *addr)
{
	return 0;
}
static inline void audit_mq_open(int oflag, umode_t mode, struct mq_attr *attr)
{ }
static inline void audit_mq_sendrecv(mqd_t mqdes, size_t msg_len,
				     unsigned int msg_prio,
				     const struct timespec64 *abs_timeout)
{ }
static inline void audit_mq_notify(mqd_t mqdes,
				   const struct sigevent *notification)
{ }
static inline void audit_mq_getsetattr(mqd_t mqdes, struct mq_attr *mqstat)
{ }
static inline int audit_log_bprm_fcaps(struct linux_binprm *bprm,
				       const struct cred *new,
				       const struct cred *old)
{
	return 0;
}
static inline void audit_log_capset(const struct cred *new,
				    const struct cred *old)
{ }
static inline void audit_mmap_fd(int fd, int flags)
{ }

static inline void audit_openat2_how(struct open_how *how)
{ }

static inline void audit_log_kern_module(char *name)
{
}

static inline void audit_fanotify(unsigned int response)
{ }

static inline void audit_tk_injoffset(struct timespec64 offset)
{ }

static inline void audit_ntp_init(struct audit_ntp_data *ad)
{ }

static inline void audit_ntp_set_old(struct audit_ntp_data *ad,
				     enum audit_ntp_type type, long long val)
{ }

static inline void audit_ntp_set_new(struct audit_ntp_data *ad,
				     enum audit_ntp_type type, long long val)
{ }

static inline void audit_ntp_log(const struct audit_ntp_data *ad)
{ }

static inline void audit_ptrace(struct task_struct *t)
{ }

static inline void audit_log_nfcfg(const char *name, u8 af,
				   unsigned int nentries,
				   enum audit_nfcfgop op, gfp_t gfp)
{ }

#define audit_n_rules 0
#define audit_signals 0

static inline bool audit_loginuid_set(struct task_struct *tsk)
{
	return uid_valid(audit_get_loginuid(tsk));
}

#endif
