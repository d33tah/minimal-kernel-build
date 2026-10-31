#ifndef _LINUX_CAPABILITY_H
#define _LINUX_CAPABILITY_H

#include <linux/types.h>

#define CAP_DAC_OVERRIDE 1
#define CAP_DAC_READ_SEARCH 2
/* End uapi/linux/capability.h */

struct inode;
struct user_namespace;

/* privileged_wrt_inode_uidgid moved to static in capability.c */
bool capable_wrt_inode_uidgid(struct user_namespace *mnt_userns,
			      const struct inode *inode, int cap);

#endif
