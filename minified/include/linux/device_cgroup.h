/* Minimal stub - CONFIG_CGROUP_DEVICE/BPF disabled */
#ifndef _DEVICE_CGROUP_H
#define _DEVICE_CGROUP_H

#include <linux/fs.h>

static inline int devcgroup_check_permission(short type, u32 major, u32 minor,
			       short access)
{ return 0; }
static inline int devcgroup_inode_permission(struct inode *inode, int mask)
{ return 0; }
static inline int devcgroup_inode_mknod(int mode, dev_t dev)
{ return 0; }

#endif
