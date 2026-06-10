/* Minimal security.h - stubs for !CONFIG_SECURITY */
#ifndef __LINUX_SECURITY_H
#define __LINUX_SECURITY_H

#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/mm.h>

struct linux_binprm;
struct cred;
struct super_block;
struct inode;
struct dentry;
struct file;
struct path;
struct xattr;
struct mm_struct;
struct user_namespace;
struct fs_context;
struct fs_parameter;

/* cap_capable, cap_settime, cap_ptrace_*, cap_capget, cap_capset,
   cap_inode_*, cap_mmap_addr, cap_vm_enough_memory removed - unused */
extern int cap_bprm_creds_from_file(struct linux_binprm *bprm, struct file *file);

extern unsigned long mmap_min_addr;

#define LSM_UNSAFE_SHARE	1
#define LSM_UNSAFE_PTRACE	2
#define LSM_UNSAFE_NO_NEW_PRIVS	4


static inline int security_init(void)
{
	return 0;
}

static inline int early_security_init(void)
{
	return 0;
}

static inline int security_vm_enough_memory_mm(struct mm_struct *mm, long pages)
{
	return __vm_enough_memory(mm, pages, 1);  /* Stub: always assume capability present */
}

static inline int security_bprm_creds_from_file(struct linux_binprm *bprm,
						struct file *file)
{
	return cap_bprm_creds_from_file(bprm, file);
}

static inline int security_fs_context_parse_param(struct fs_context *fc,
						  struct fs_parameter *param)
{
	return -ENOPARAM;
}

#endif
