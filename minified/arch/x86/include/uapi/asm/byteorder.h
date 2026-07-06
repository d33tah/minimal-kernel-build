
#ifndef _ASM_X86_BYTEORDER_H
#define _ASM_X86_BYTEORDER_H

/* --- 2025-12-07 23:55 --- Inlined from uapi/linux/byteorder/little_endian.h */
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#endif
#ifndef __LITTLE_ENDIAN_BITFIELD
#define __LITTLE_ENDIAN_BITFIELD
#endif

#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/swab.h>

#define __cpu_to_le64(x) ((__force __le64)(__u64)(x))
#define __le64_to_cpu(x) ((__force __u64)(__le64)(x))
#define __cpu_to_le32(x) ((__force __le32)(__u32)(x))
#define __le32_to_cpu(x) ((__force __u32)(__le32)(x))
#define __cpu_to_le16(x) ((__force __le16)(__u16)(x))
#define __le16_to_cpu(x) ((__force __u16)(__le16)(x))

/* 11 p static inlines + 12 s macros removed - 0-ref; le32_to_cpup kept (xz get_le32) */
static __always_inline __u32 __le32_to_cpup(const __le32 *p)
{
	return (__force __u32)*p;
}
/* end little_endian.h */

/* Inlined from linux/byteorder/generic.h */
#define cpu_to_le64 __cpu_to_le64
#define le64_to_cpu __le64_to_cpu
#define cpu_to_le32 __cpu_to_le32
#define le32_to_cpu __le32_to_cpu
#define cpu_to_le16 __cpu_to_le16
#define le16_to_cpu __le16_to_cpu
/* 23 p/s byteorder aliases removed - 0-ref; le32_to_cpup kept (xz get_le32) */
#define le32_to_cpup __le32_to_cpup

#endif
