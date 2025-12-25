
#ifndef _ASM_X86_MMAN_H
#define _ASM_X86_MMAN_H

/* x86 specific */
#define MAP_32BIT	0x40

/* From asm-generic/mman-common.h */
#define PROT_READ	0x1
#define PROT_WRITE	0x2
#define PROT_EXEC	0x4
#define PROT_SEM	0x8
#define PROT_NONE	0x0
#define PROT_GROWSDOWN	0x01000000
#define PROT_GROWSUP	0x02000000

#define MAP_TYPE	0x0f
#define MAP_FIXED	0x10
#define MAP_ANONYMOUS	0x20

#define MAP_POPULATE		0x008000
#define MAP_NONBLOCK		0x010000
#define MAP_STACK		0x020000
#define MAP_HUGETLB		0x040000
#define MAP_SYNC		0x080000
#define MAP_FIXED_NOREPLACE	0x100000

#define MAP_UNINITIALIZED 0x4000000

#define MLOCK_ONFAULT	0x01

#define MS_ASYNC	1
#define MS_INVALIDATE	2
#define MS_SYNC		4

/* MADV_* macros removed - madvise syscall is stubbed */
/* PKEY_* macros removed - pkey functions are stubbed */
#define MAP_FILE	0

/* From asm-generic/mman.h */
#define MAP_GROWSDOWN	0x0100
#define MAP_DENYWRITE	0x0800
#define MAP_EXECUTABLE	0x1000
#define MAP_LOCKED	0x2000
#define MAP_NORESERVE	0x4000

#define MCL_CURRENT	1
#define MCL_FUTURE	2
#define MCL_ONFAULT	4

#endif
