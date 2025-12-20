#ifndef _LINUX_FCNTL_H
#define _LINUX_FCNTL_H

#include <linux/stat.h>
#include <asm/fcntl.h>

/* --- 2025-12-06 20:29 --- openat2.h inlined (19 LOC) */
struct open_how {
	__u64 flags;
	__u64 mode;
	__u64 resolve;
};
#define RESOLVE_NO_XDEV		0x01
#define RESOLVE_NO_MAGICLINKS	0x02
#define RESOLVE_NO_SYMLINKS	0x04
#define RESOLVE_BENEATH		0x08
#define RESOLVE_IN_ROOT		0x10
#define RESOLVE_CACHED		0x20
/* --- end openat2.h inlined --- */

/* Inlined from uapi/linux/fcntl.h */
/* F_DUPFD_CLOEXEC, F_SETPIPE_SZ, F_GETPIPE_SZ removed - unused */
#define AT_FDCWD		-100
#define AT_SYMLINK_NOFOLLOW	0x100
#define AT_EACCESS		0x200
#define AT_REMOVEDIR		0x200
#define AT_SYMLINK_FOLLOW	0x400
#define AT_NO_AUTOMOUNT		0x800
#define AT_EMPTY_PATH		0x1000

#define VALID_OPEN_FLAGS \
	(O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC | \
	 O_APPEND | O_NDELAY | O_NONBLOCK | __O_SYNC | O_DSYNC | \
	 FASYNC	| O_DIRECT | O_LARGEFILE | O_DIRECTORY | O_NOFOLLOW | \
	 O_NOATIME | O_CLOEXEC | O_PATH | __O_TMPFILE)

#define VALID_RESOLVE_FLAGS \
	(RESOLVE_NO_XDEV | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_SYMLINKS | \
	 RESOLVE_BENEATH | RESOLVE_IN_ROOT | RESOLVE_CACHED)

#define OPEN_HOW_SIZE_VER0	24  
#define OPEN_HOW_SIZE_LATEST	OPEN_HOW_SIZE_VER0

#ifndef force_o_largefile
#define force_o_largefile() (!IS_ENABLED(CONFIG_ARCH_32BIT_OFF_T))
#endif

/* IS_GETLK*, IS_SETLK*, IS_SETLKW* removed - unused (file locking not needed) */

#endif
