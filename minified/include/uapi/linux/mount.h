#ifndef _UAPI_LINUX_MOUNT_H
#define _UAPI_LINUX_MOUNT_H

#include <linux/types.h>

#define MS_RDONLY	 1	 
#define MS_NOSUID	 2	 
#define MS_NODEV	 4	 
#define MS_NOEXEC	 8
#define MS_REMOUNT	32
#define MS_NOSYMFOLLOW	256
#define MS_NOATIME	1024
#define MS_NODIRATIME	2048
#define MS_BIND		4096
#define MS_MOVE		8192
#define MS_REC		16384
#define MS_SILENT	32768
#define MS_UNBINDABLE	(1<<17)
#define MS_PRIVATE	(1<<18)
#define MS_SLAVE	(1<<19)
#define MS_SHARED	(1<<20)
#define MS_RELATIME	(1<<21)
#define MS_STRICTATIME	(1<<24)
#define MS_NOUSER	(1<<31)

#define MS_MGC_VAL 0xC0ED0000
#define MS_MGC_MSK 0xffff0000

/* Removed unused OPEN_TREE_*, MOVE_MOUNT_*, FSOPEN_*, FSPICK_*,
   enum fsconfig_command, FSMOUNT_CLOEXEC defines */

/* MOUNT_ATTR_* macros removed - all unused */

#endif  
