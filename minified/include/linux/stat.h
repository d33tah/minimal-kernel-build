#ifndef _LINUX_STAT_H
#define _LINUX_STAT_H
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

struct stat64 {
	unsigned long long	st_dev;
	unsigned char	__pad0[4];

	unsigned long	__st_ino;

	unsigned int	st_mode;
	unsigned int	st_nlink;

	unsigned long	st_uid;
	unsigned long	st_gid;

	unsigned long long	st_rdev;
	unsigned char	__pad3[4];

	long long	st_size;
	unsigned long	st_blksize;

	 
	unsigned long long	st_blocks;

	unsigned long	st_atime;
	unsigned long	st_atime_nsec;

	unsigned long	st_mtime;
	unsigned int	st_mtime_nsec;

	unsigned long	st_ctime;
	unsigned long	st_ctime_nsec;

	unsigned long long	st_ino;
};

/* struct __old_kernel_stat removed - never used */
#include <linux/types.h>
#if defined(__KERNEL__) || !defined(__GLIBC__) || (__GLIBC__ < 2)
#define S_IFMT  00170000
#define S_IFSOCK 0140000
/* S_IFLNK removed - never used (no symlinks) */
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000
/* S_ISLNK removed - never used (no symlinks) */
#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m)	(((m) & S_IFMT) == S_IFSOCK)
#define S_IRWXU 00700
/* S_IRUSR, S_IWUSR, S_IXUSR removed - unused */
#define S_IRWXG 00070
/* S_IRGRP, S_IWGRP removed - never used */
#define S_IXGRP 00010
#define S_IRWXO 00007
/* S_IROTH, S_IWOTH, S_IXOTH removed - never used */
#endif
/* struct statx forward decl removed - unused */
/* STATX_BASIC_STATS removed - unused */
#define S_IRWXUGO	(S_IRWXU|S_IRWXG|S_IRWXO)
#define S_IALLUGO	(S_ISUID|S_ISGID|S_ISVTX|S_IRWXUGO)
/* S_IRUGO, S_IWUGO, S_IXUGO removed - unused */
#include <linux/time.h>
#include <linux/uidgid.h>
/* struct kstat removed - unused */
#endif
