#ifndef _LINUX_FCNTL_H
#define _LINUX_FCNTL_H

#include <linux/stat.h>

#define O_ACCMODE	00000003
#define O_RDONLY	00000000
#define O_RDWR		00000002
#define O_CREAT		00000100
#define O_EXCL		00000200
#define O_NOCTTY	00000400
#define O_TRUNC		00001000
#define O_DIRECTORY	00200000
#define O_LARGEFILE	00100000
#define O_NOFOLLOW	00400000
#define O_CLOEXEC	02000000
#define O_PATH		010000000
#define __O_TMPFILE	020000000

struct open_how {
	__u64 flags;
	__u64 mode;
	__u64 resolve;
};

#define AT_FDCWD		-100

#define VALID_OPEN_FLAGS 0x007fffc3

#endif
