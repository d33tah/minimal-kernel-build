/* Minimal kernfs.h - kernfs_node/kernfs_elem_dir removed (no .c referenced them;
 * the only user was struct kobject's dead `sd` field). Kept as an empty header so
 * the <linux/kernfs.h> includes in sysfs.h/cgroup.h still resolve. */
#ifndef __LINUX_KERNFS_H
#define __LINUX_KERNFS_H

#endif /* __LINUX_KERNFS_H */
