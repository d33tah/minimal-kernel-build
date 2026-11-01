#ifndef _LINUX_CAPABILITY_H
#define _LINUX_CAPABILITY_H

#define CAP_DAC_OVERRIDE 1
#define CAP_DAC_READ_SEARCH 2

struct inode;
struct user_namespace;

bool capable_wrt_inode_uidgid(struct user_namespace *mnt_userns,
			      const struct inode *inode, int cap);

#endif
