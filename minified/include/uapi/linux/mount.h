#ifndef _UAPI_LINUX_MOUNT_H
#define _UAPI_LINUX_MOUNT_H

#include <linux/types.h>

#define MS_RDONLY	 1	 
#define MS_NOSUID	 2	 
#define MS_NODEV	 4	 
#define MS_NOEXEC	 8	 
#define MS_SYNCHRONOUS	16	 
#define MS_REMOUNT	32
/* MS_MANDLOCK, MS_DIRSYNC removed - unused */
#define MS_NOSYMFOLLOW	256
#define MS_NOATIME	1024	 
#define MS_NODIRATIME	2048	 
#define MS_BIND		4096
#define MS_MOVE		8192
#define MS_REC		16384
#define MS_VERBOSE	32768	 
#define MS_SILENT	32768
#define MS_POSIXACL	(1<<16)	 
#define MS_UNBINDABLE	(1<<17)	 
#define MS_PRIVATE	(1<<18)	 
#define MS_SLAVE	(1<<19)	 
#define MS_SHARED	(1<<20)	 
#define MS_RELATIME	(1<<21)	 
#define MS_KERNMOUNT	(1<<22)  
#define MS_I_VERSION	(1<<23)  
#define MS_STRICTATIME	(1<<24)
/* MS_LAZYTIME, MS_NOREMOTELOCK, MS_NOSEC, MS_BORN removed - unused */
#define MS_SUBMOUNT     (1<<26)
#define MS_ACTIVE	(1<<30)
#define MS_NOUSER	(1<<31)

#define MS_MGC_VAL 0xC0ED0000
#define MS_MGC_MSK 0xffff0000

/* Removed unused OPEN_TREE_*, MOVE_MOUNT_*, FSOPEN_*, FSPICK_*,
   enum fsconfig_command, FSMOUNT_CLOEXEC defines */

#define MOUNT_ATTR_RDONLY	0x00000001  
#define MOUNT_ATTR_NOSUID	0x00000002  
#define MOUNT_ATTR_NODEV	0x00000004  
#define MOUNT_ATTR_NOEXEC	0x00000008  
#define MOUNT_ATTR__ATIME	0x00000070  
#define MOUNT_ATTR_RELATIME	0x00000000  
#define MOUNT_ATTR_NOATIME	0x00000010  
#define MOUNT_ATTR_STRICTATIME	0x00000020  
#define MOUNT_ATTR_NODIRATIME	0x00000080  
#define MOUNT_ATTR_IDMAP	0x00100000  
#define MOUNT_ATTR_NOSYMFOLLOW	0x00200000  

struct mount_attr {
	__u64 attr_set;
	__u64 attr_clr;
	__u64 propagation;
	__u64 userns_fd;
};

#define MOUNT_ATTR_SIZE_VER0	32  

#endif  
