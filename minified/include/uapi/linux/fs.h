/* Minimal fs.h - only keep what's actually used */
#ifndef _UAPI_LINUX_FS_H
#define _UAPI_LINUX_FS_H

#include <linux/limits.h>
#include <linux/ioctl.h>
#include <linux/types.h>

#undef NR_OPEN
#define INR_OPEN_CUR 1024
#define INR_OPEN_MAX 4096

#define BLOCK_SIZE_BITS 10
#define BLOCK_SIZE (1<<BLOCK_SIZE_BITS)

#define SEEK_SET	0
#define SEEK_CUR	1
#define SEEK_END	2
#define SEEK_DATA	3
#define SEEK_HOLE	4
#define SEEK_MAX	SEEK_HOLE

/* Rename flags used in fs/libfs.c */
#define RENAME_NOREPLACE	(1 << 0)
#define RENAME_EXCHANGE		(1 << 1)
#define RENAME_WHITEOUT		(1 << 2)

/* Files stats used in fs/file_table.c */
struct files_stat_struct {
	unsigned long nr_files;
	unsigned long nr_free_files;
	unsigned long max_files;
};

#define NR_FILE  8192

/* fsxattr used in fs/ioctl.c */
struct fsxattr {
	__u32		fsx_xflags;
	__u32		fsx_extsize;
	__u32		fsx_nextents;
	__u32		fsx_projid;
	__u32		fsx_cowextsize;
	unsigned char	fsx_pad[8];
};

/* RWF flags used in fs code */
typedef int __bitwise __kernel_rwf_t;
#define RWF_HIPRI	((__force __kernel_rwf_t)0x00000001)
#define RWF_DSYNC	((__force __kernel_rwf_t)0x00000002)
#define RWF_SYNC	((__force __kernel_rwf_t)0x00000004)
#define RWF_NOWAIT	((__force __kernel_rwf_t)0x00000008)
#define RWF_APPEND	((__force __kernel_rwf_t)0x00000010)
#define RWF_SUPPORTED	(RWF_HIPRI | RWF_DSYNC | RWF_SYNC | RWF_NOWAIT | RWF_APPEND)

#endif
