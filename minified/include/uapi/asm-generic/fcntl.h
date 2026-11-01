/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_GENERIC_FCNTL_H
#define _ASM_GENERIC_FCNTL_H

/*
 * FMODE_EXEC is 0x20
 * FMODE_NONOTIFY is 0x4000000
 * These cannot be used by userspace O_* until internal and external open
 * flags are split.
 * -Eric Paris
 */

/*
 * When introducing new O_* bits, please check its uniqueness in fcntl_init().
 */

#define O_ACCMODE 00000003
#define O_RDONLY 00000000
#define O_WRONLY 00000001
#define O_RDWR 00000002
#define O_CREAT 00000100 /* not fcntl */
#define O_EXCL 00000200 /* not fcntl */
#define O_TRUNC 00001000 /* not fcntl */
#define O_LARGEFILE 00100000

#endif
