#ifndef _LINUX_STAT_H
#define _LINUX_STAT_H
#include <asm/stat.h>
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
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRWXG 00070
#define S_IRGRP 00040
/* S_IWGRP removed - never used */
#define S_IXGRP 00010
#define S_IRWXO 00007
/* S_IROTH, S_IWOTH removed - never used */
#define S_IXOTH 00001
#endif
struct statx;
/* STATX_BASIC_STATS removed - unused */
#define S_IRWXUGO	(S_IRWXU|S_IRWXG|S_IRWXO)
#define S_IALLUGO	(S_ISUID|S_ISGID|S_ISVTX|S_IRWXUGO)
/* S_IRUGO, S_IWUGO removed - unused */
#define S_IXUGO		(S_IXUSR|S_IXGRP|S_IXOTH)
#include <linux/time.h>
#include <linux/uidgid.h>
/* struct kstat removed - unused */
#endif
