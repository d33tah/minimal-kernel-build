/* SPDX-License-Identifier: GPL-2.0 */
/*
 * asm-generic/int-ll64.h
 *
 * Integer declarations for architectures which use "long long"
 * for 64-bit types.
 */
#ifndef _ASM_GENERIC_INT_LL64_H
#define _ASM_GENERIC_INT_LL64_H

#ifndef __ASSEMBLY__

typedef unsigned char __u8;
typedef unsigned short __u16;
typedef unsigned int __u32;
__extension__ typedef unsigned long long __u64;

typedef __u8 u8;
typedef __u16 u16;
typedef __u32 u32;
typedef __u64 u64;

#endif /* __ASSEMBLY__ */

#endif /* _ASM_GENERIC_INT_LL64_H */
