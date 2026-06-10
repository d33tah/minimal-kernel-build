/* Minimal includes for capability stubs */
#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/binfmts.h>
#include <linux/mm.h>


/* cap_capable, cap_settime, cap_ptrace_*, cap_capget, cap_capset,
   cap_inode_need_killpriv, cap_inode_killpriv, cap_vm_enough_memory,
   cap_mmap_addr removed - unused */

int cap_bprm_creds_from_file(struct linux_binprm *bprm, struct file *file)
{
	return 0;
}
