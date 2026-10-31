
#ifndef _ASM_X86_BYTEORDER_H
#define _ASM_X86_BYTEORDER_H

/* --- 2025-12-07 23:55 --- Inlined from uapi/linux/byteorder/little_endian.h */
#define __LITTLE_ENDIAN_BITFIELD

#include <linux/swab.h>

#define __cpu_to_le32(x) ((__force __le32)(__u32)(x))
#define __le32_to_cpu(x) ((__force __u32)(__le32)(x))

/* end little_endian.h */

/* Inlined from linux/byteorder/generic.h */
#define cpu_to_le32 __cpu_to_le32
#define le32_to_cpu __le32_to_cpu

#endif
