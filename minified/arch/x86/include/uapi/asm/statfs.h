#ifndef _ASM_X86_STATFS_H
#define _ASM_X86_STATFS_H

/* Inlined from asm-generic/statfs.h */
#include <linux/types.h>

#ifndef __statfs_word
#define __statfs_word __u32
#endif

struct statfs {
	__statfs_word f_type;
	__statfs_word f_bsize;
	__statfs_word f_blocks;
	__statfs_word f_bfree;
	__statfs_word f_bavail;
	__statfs_word f_files;
	__statfs_word f_ffree;
	__kernel_fsid_t f_fsid;
	__statfs_word f_namelen;
	__statfs_word f_frsize;
	__statfs_word f_flags;
	__statfs_word f_spare[4];
};

#ifndef ARCH_PACK_STATFS64
#define ARCH_PACK_STATFS64
#endif

struct statfs64 {
	__statfs_word f_type;
	__statfs_word f_bsize;
	__u64 f_blocks;
	__u64 f_bfree;
	__u64 f_bavail;
	__u64 f_files;
	__u64 f_ffree;
	__kernel_fsid_t f_fsid;
	__statfs_word f_namelen;
	__statfs_word f_frsize;
	__statfs_word f_flags;
	__statfs_word f_spare[4];
} ARCH_PACK_STATFS64;

typedef __kernel_fsid_t	fsid_t;
#endif  
