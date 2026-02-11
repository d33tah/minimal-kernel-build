#ifndef _LINUX_FCNTL_H
#define _LINUX_FCNTL_H

#include <linux/stat.h>
#include <asm/fcntl.h>

struct open_how {
	__u64 flags;
	__u64 mode;
	__u64 resolve;
};
/* RESOLVE_* macros removed - unused */

/* Inlined from uapi/linux/fcntl.h */
#define AT_FDCWD		-100
/* AT_NO_AUTOMOUNT removed - unused */

#define VALID_OPEN_FLAGS \
	(O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC | \
	 O_APPEND | O_NDELAY | O_NONBLOCK | __O_SYNC | O_DSYNC | \
	 FASYNC	| O_DIRECT | O_LARGEFILE | O_DIRECTORY | O_NOFOLLOW | \
	 O_NOATIME | O_CLOEXEC | O_PATH | __O_TMPFILE)

/* VALID_RESOLVE_FLAGS, OPEN_HOW_SIZE_* removed - unused */

#endif
