 
#ifndef _ASM_X86_STAT_H
#define _ASM_X86_STAT_H

#include <asm/posix_types.h>

/* 32-bit only kernel - removed x86_64 stat structures */
struct stat {
	unsigned long  st_dev;
	unsigned long  st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned long  st_rdev;
	unsigned long  st_size;
	unsigned long  st_blksize;
	unsigned long  st_blocks;
	unsigned long  st_atime;
	unsigned long  st_atime_nsec;
	unsigned long  st_mtime;
	unsigned long  st_mtime_nsec;
	unsigned long  st_ctime;
	unsigned long  st_ctime_nsec;
	unsigned long  __unused4;
	unsigned long  __unused5;
};


/* struct stat64 removed - 0-ref tree-wide (no stat64 syscall consumer) */


/* Removed x86_64 stat structure - 32-bit only kernel */

/* struct __old_kernel_stat removed - 0-ref tree-wide (oldstat syscall absent) */

#endif /* _ASM_X86_STAT_H */
