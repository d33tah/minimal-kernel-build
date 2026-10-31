#ifndef _ASM_GENERIC_FCNTL_H
#define _ASM_GENERIC_FCNTL_H

#define O_ACCMODE 00000003
#define O_RDONLY 00000000
#define O_WRONLY 00000001
#define O_RDWR 00000002
#define O_CREAT 00000100
#define O_EXCL 00000200
#define O_TRUNC 00001000
#define O_NONBLOCK 00004000
#define O_LARGEFILE 00100000

/* Dead UABI struct flock removed (0-ref; the only other mention is a comment in fs.h). */

#endif
