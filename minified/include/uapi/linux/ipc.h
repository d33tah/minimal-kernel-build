#ifndef _UAPI_LINUX_IPC_H
#define _UAPI_LINUX_IPC_H

/* Minimal ipc.h - SysV IPC disabled, only ipc_perm struct needed */

#include <linux/types.h>

struct ipc_perm
{
	__kernel_key_t	key;
	__kernel_uid_t	uid;
	__kernel_gid_t	gid;
	__kernel_uid_t	cuid;
	__kernel_gid_t	cgid;
	__kernel_mode_t	mode;
	unsigned short	seq;
};

#include <asm/ipcbuf.h>

#endif  
