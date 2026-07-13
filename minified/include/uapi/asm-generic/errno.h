#ifndef _ASM_GENERIC_ERRNO_H
#define _ASM_GENERIC_ERRNO_H

/* Inlined from asm-generic/errno-base.h */
#define	EPERM		 1
#define	ENOENT		 2
#define	ESRCH		 3
#define	EINTR		 4
#define	EIO		 5
#define	ENXIO		 6
#define	E2BIG		 7
#define	ENOEXEC		 8
#define	EBADF		 9
#define	ECHILD		10
#define	EAGAIN		11
#define	ENOMEM		12
#define	EACCES		13
#define	EFAULT		14
#define	EBUSY		16
#define	EEXIST		17
#define	EXDEV		18
#define	ENODEV		19
#define	ENOTDIR		20
#define	EISDIR		21
#define	EINVAL		22
#define	EMFILE		24
#define	ETXTBSY		26
#define	ENOSPC		28
#define	EROFS		30
#define	EMLINK		31
/* ENOTTY(25), ESPIPE(29), ERANGE(34) removed - 0-caller errno constants */
/* end errno-base.h */

#define	ENAMETOOLONG	36
#define	ENOSYS		38
#define	ELOOP		40
#define	EOVERFLOW	75
#define	EOPNOTSUPP	95
#define	ENOTCONN	107
#define	ESTALE		116
/* ENOTRECOVERABLE(131) removed - 0-caller errno constant */
#define EHWPOISON	133

#endif
