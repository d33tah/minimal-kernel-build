/* Minimal audit.h - stubs for !CONFIG_AUDIT */
/* --- 2025-12-06 13:10 --- elf-em.h replaced with elf.h */
#ifndef _LINUX_AUDIT_H_
#define _LINUX_AUDIT_H_

#include <linux/ptrace.h>
#include <linux/types.h>
#include <linux/elf.h>

/* From uapi/linux/audit.h - inlined */
#define __AUDIT_ARCH_64BIT 0x80000000
#define __AUDIT_ARCH_LE	   0x40000000
#define AUDIT_ARCH_I386		(EM_386|__AUDIT_ARCH_LE)

struct task_struct;
/* Forward declarations for audit_buffer, audit_context, kern_ipc_perm removed - unused */

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


#define audit_enabled AUDIT_OFF

static inline int audit_alloc(struct task_struct *task)
{
	return 0;
}
static inline void audit_free(struct task_struct *task)
{ }
static inline void audit_syscall_entry(int major, unsigned long a0,
				       unsigned long a1, unsigned long a2,
				       unsigned long a3)
{ }
static inline void audit_syscall_exit(void *pt_regs)
{ }
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
static inline void audit_inode_child(struct inode *parent,
				     const struct dentry *dentry,
				     const unsigned char type)
{ }
static inline void audit_bprm(struct linux_binprm *bprm)
{ }
static inline void audit_mmap_fd(int fd, int flags)
{ }

static inline void audit_openat2_how(struct open_how *how)
{ }


#define audit_n_rules 0
#define audit_signals 0


#endif
