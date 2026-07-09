/* Minimal security.h - stubs for !CONFIG_SECURITY */
#ifndef __LINUX_SECURITY_H
#define __LINUX_SECURITY_H

#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/mm.h>

struct linux_binprm;
struct cred;
struct dentry;
struct path;
struct mm_struct;
struct user_namespace;

/* cap_capable, cap_settime, cap_ptrace_*, cap_capget, cap_capset,
   cap_inode_*, cap_mmap_addr, cap_vm_enough_memory removed - unused */

extern unsigned long mmap_min_addr;


static inline int security_vm_enough_memory_mm(struct mm_struct *mm, long pages)
{
	return __vm_enough_memory(mm, pages, 1);  /* Stub: always assume capability present */
}

#endif
