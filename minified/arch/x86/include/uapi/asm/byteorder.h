/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_X86_BYTEORDER_H
#define _ASM_X86_BYTEORDER_H

#define __cpu_to_le32(x) ((__force __le32)(__u32)(x))
#define __le32_to_cpu(x) ((__force __u32)(__le32)(x))

#define cpu_to_le32 __cpu_to_le32
#define le32_to_cpu __le32_to_cpu

#endif /* _ASM_X86_BYTEORDER_H */
