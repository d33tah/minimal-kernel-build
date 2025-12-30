/* Minimal security.h - stubs for !CONFIG_SECURITY */
#ifndef __LINUX_SECURITY_H
#define __LINUX_SECURITY_H

#include <linux/kernel_read_file.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/mm.h>

struct linux_binprm;
struct mm_struct;

/* Reduced lockdown enum for minimal kernel */
enum lockdown_reason {
	LOCKDOWN_NONE,
	LOCKDOWN_MODULE_PARAMETERS, /* Used by kernel/params.c */
};

extern int cap_bprm_creds_from_file(struct linux_binprm *bprm, struct file *file);
extern unsigned long mmap_min_addr;

#define LSM_UNSAFE_SHARE	1
#define LSM_UNSAFE_PTRACE	2
#define LSM_UNSAFE_NO_NEW_PRIVS	4

#define __data_id_enumify(ENUM, dummy) LOADING_ ## ENUM,
#define __data_id_stringify(dummy, str) #str,

enum kernel_load_data_id {
	__kernel_read_file_id(__data_id_enumify)
};

/* Only two security functions are actually called */
static inline int security_vm_enough_memory_mm(struct mm_struct *mm, long pages)
{
	return __vm_enough_memory(mm, pages, 1);  /* Stub: always assume capability present */
}

static inline int security_bprm_creds_from_file(struct linux_binprm *bprm,
						struct file *file)
{
	return cap_bprm_creds_from_file(bprm, file);
}

/* All other security_* functions removed - never called */

#endif
