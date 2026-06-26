
#ifndef _ASM_X86_MMAN_H
#define _ASM_X86_MMAN_H

/* From asm-generic/mman-common.h */
#define PROT_READ	0x1
#define PROT_WRITE	0x2
#define PROT_EXEC	0x4

#define MAP_FIXED	0x10

/* MAP_TYPE/MAP_POPULATE (comment-only) + MAP_SYNC (unused, linux/mman.h fallback) removed - 0-ref */
#define MAP_FIXED_NOREPLACE	0x100000

/* From asm-generic/mman.h */
#define MAP_GROWSDOWN	0x0100
#define MAP_LOCKED	0x2000
/* MAP_NORESERVE removed - 0-ref */

#endif
