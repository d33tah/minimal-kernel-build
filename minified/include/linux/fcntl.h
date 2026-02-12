#ifndef _LINUX_FCNTL_H
#define _LINUX_FCNTL_H

#include <linux/stat.h>
#include <linux/types.h>

/* inlined from asm-generic/fcntl.h */
#define O_ACCMODE	00000003
#define O_RDONLY	00000000
#define O_WRONLY	00000001
#define O_RDWR		00000002
#define O_CREAT		00000100
#define O_EXCL		00000200
#define O_NOCTTY	00000400
#define O_TRUNC		00001000
#define O_APPEND	00002000
#define O_NONBLOCK	00004000
#define O_DSYNC		00010000
#define FASYNC		00020000
#define O_DIRECT	00040000
#define O_LARGEFILE	00100000
#define O_DIRECTORY	00200000
#define O_NOFOLLOW	00400000
#define O_NOATIME	01000000
#define O_CLOEXEC	02000000
#define __O_SYNC	04000000
#define O_SYNC		(__O_SYNC|O_DSYNC)
#define O_PATH		010000000
#define __O_TMPFILE	020000000
#define O_NDELAY	O_NONBLOCK

struct open_how {
	__u64 flags;
	__u64 mode;
	__u64 resolve;
};

#define AT_FDCWD		-100

#define VALID_OPEN_FLAGS \
	(O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC | \
	 O_APPEND | O_NDELAY | O_NONBLOCK | __O_SYNC | O_DSYNC | \
	 FASYNC	| O_DIRECT | O_LARGEFILE | O_DIRECTORY | O_NOFOLLOW | \
	 O_NOATIME | O_CLOEXEC | O_PATH | __O_TMPFILE)

#endif
