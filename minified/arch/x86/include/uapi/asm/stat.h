 
#ifndef _ASM_X86_STAT_H
#define _ASM_X86_STAT_H

#include <asm/posix_types.h>

/* 32-bit only kernel - removed x86_64 stat structures */
struct stat { unsigned long st_dev, st_ino; unsigned short st_mode, st_nlink, st_uid, st_gid; unsigned long st_rdev, st_size, st_blksize, st_blocks, st_atime, st_atime_nsec, st_mtime, st_mtime_nsec, st_ctime, st_ctime_nsec, __unused4, __unused5; };


/* struct stat64 removed - 0-ref tree-wide (no stat64 syscall consumer) */


/* Removed x86_64 stat structure - 32-bit only kernel */

/* struct __old_kernel_stat removed - 0-ref tree-wide (oldstat syscall absent) */

#endif /* _ASM_X86_STAT_H */
