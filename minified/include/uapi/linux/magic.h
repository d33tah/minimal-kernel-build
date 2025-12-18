#ifndef __LINUX_MAGIC_H__
#define __LINUX_MAGIC_H__

/* Only magic numbers actually used in the minimal kernel */
#define RAMFS_MAGIC		0x858458f6
/* TMPFS_MAGIC removed - unused */
#define STACK_END_MAGIC		0x57AC6E9D
/* PIPEFS_MAGIC, SOCKFS_MAGIC removed - unused */
#define ANON_INODE_FS_MAGIC	0x09041934
#define DEVMEM_MAGIC		0x454d444d

#endif
