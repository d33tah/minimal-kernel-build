#ifndef _ASM_GENERIC_INT_LL64_H
#define _ASM_GENERIC_INT_LL64_H

/* Inlined from uapi/asm-generic/int-ll64.h */
#include <asm/bitsperlong.h>

#ifndef __ASSEMBLY__

typedef __signed__ char __s8;
typedef unsigned char __u8;
typedef unsigned short __u16;
typedef __signed__ int __s32;
typedef unsigned int __u32;
__extension__ typedef __signed__ long long __s64;
__extension__ typedef unsigned long long __u64;
/* End uapi/asm-generic/int-ll64.h */

typedef __s8  s8;
typedef __u8  u8;
typedef __u16 u16;
typedef __s32 s32;
typedef __u32 u32;
typedef __s64 s64;
typedef __u64 u64;

#endif  /* __ASSEMBLY__ */

#endif  /* _ASM_GENERIC_INT_LL64_H */
