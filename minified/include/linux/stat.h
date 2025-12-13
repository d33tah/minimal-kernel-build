/* --- 2025-12-06 13:25 --- uapi/linux/stat.h inlined */
#ifndef _LINUX_STAT_H
#define _LINUX_STAT_H

#include <asm/stat.h>

/* Inlined from uapi/linux/stat.h */
#include <linux/types.h>

#if defined(__KERNEL__) || !defined(__GLIBC__) || (__GLIBC__ < 2)
#define S_IFMT  00170000
#define S_IFSOCK 0140000
#define S_IFLNK	 0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000

#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m)	(((m) & S_IFMT) == S_IFSOCK)

#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001
#endif

struct statx_timestamp {
	__s64	tv_sec;
	__u32	tv_nsec;
	__s32	__reserved;
};

struct statx {
	__u32	stx_mask;
	__u32	stx_blksize;
	__u64	stx_attributes;
	__u32	stx_nlink;
	__u32	stx_uid;
	__u32	stx_gid;
	__u16	stx_mode;
	__u16	__spare0[1];
	__u64	stx_ino;
	__u64	stx_size;
	__u64	stx_blocks;
	__u64	stx_attributes_mask;
	struct statx_timestamp	stx_atime;
	struct statx_timestamp	stx_btime;
	struct statx_timestamp	stx_ctime;
	struct statx_timestamp	stx_mtime;
	__u32	stx_rdev_major;
	__u32	stx_rdev_minor;
	__u32	stx_dev_major;
	__u32	stx_dev_minor;
	__u64	stx_mnt_id;
	__u64	__spare2;
	__u64	__spare3[12];
};

/* Only keeping STATX_BASIC_STATS which is used */
#define STATX_BASIC_STATS	0x000007ffU

/* End uapi/linux/stat.h */

#define S_IRWXUGO	(S_IRWXU|S_IRWXG|S_IRWXO)
#define S_IALLUGO	(S_ISUID|S_ISGID|S_ISVTX|S_IRWXUGO)
#define S_IRUGO		(S_IRUSR|S_IRGRP|S_IROTH)
#define S_IWUGO		(S_IWUSR|S_IWGRP|S_IWOTH)
#define S_IXUGO		(S_IXUSR|S_IXGRP|S_IXOTH)

#define UTIME_NOW	((1l << 30) - 1l)
#define UTIME_OMIT	((1l << 30) - 2l)

#include <linux/time.h>
#include <linux/uidgid.h>

struct kstat {
	u32		result_mask;
	umode_t		mode;
	unsigned int	nlink;
	uint32_t	blksize;
	u64		attributes;
	u64		attributes_mask;
	/* KSTAT_ATTR_* macros removed - unused */
	u64		ino;
	dev_t		dev;
	dev_t		rdev;
	kuid_t		uid;
	kgid_t		gid;
	loff_t		size;
	struct timespec64 atime;
	struct timespec64 mtime;
	struct timespec64 ctime;
	struct timespec64 btime;
	u64		blocks;
	u64		mnt_id;
};

#endif
