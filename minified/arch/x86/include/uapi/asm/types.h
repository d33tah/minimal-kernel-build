/* x86-32 type definitions (replaces asm-generic/types.h) */
#ifndef _ASM_X86_TYPES_H
#define _ASM_X86_TYPES_H
#include <asm/bitsperlong.h>

#ifndef __ASSEMBLY__

typedef __signed__ char __s8;
typedef unsigned char __u8;
typedef __signed__ short __s16;
typedef unsigned short __u16;
typedef __signed__ int __s32;
typedef unsigned int __u32;
__extension__ typedef __signed__ long long __s64;
__extension__ typedef unsigned long long __u64;

typedef __s8 s8;
typedef __u8 u8;
typedef __s16 s16;
typedef __u16 u16;
typedef __s32 s32;
typedef __u32 u32;
typedef __s64 s64;
typedef __u64 u64;

#define U64_C(x) x##ULL

#else /* __ASSEMBLY__ */

#define U64_C(x) x

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_TYPES_H */
