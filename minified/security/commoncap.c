/* Minimal includes for capability stubs */
#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/binfmts.h>
#include <linux/mm.h>
int cap_capable(const struct cred *cred, struct user_namespace *targ_ns,
		int cap, unsigned int opts)
{
	return 0;
}
int cap_bprm_creds_from_file(struct linux_binprm *bprm, struct file *file)
{
	return 0;
}
