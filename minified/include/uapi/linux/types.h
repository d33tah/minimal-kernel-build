#ifndef _UAPI_LINUX_TYPES_H
#define _UAPI_LINUX_TYPES_H

#include <asm/types.h>

#ifndef __ASSEMBLY__
/* userspace-only #ifndef __KERNEL__ #warning block removed - never compiled
 * (kernel build always defines __KERNEL__). */

#include <asm/posix_types.h>



#ifdef __CHECKER__
#define __bitwise	__attribute__((bitwise))
#else
#define __bitwise
#endif

#define __bitwise__ __bitwise

typedef __u16 __bitwise __le16;
typedef __u16 __bitwise __be16;
typedef __u32 __bitwise __le32;
typedef __u32 __bitwise __be32;
typedef __u64 __bitwise __le64;
typedef __u64 __bitwise __be64;

typedef __u16 __bitwise __sum16;
typedef __u32 __bitwise __wsum;

#define __aligned_u64 __u64 __attribute__((aligned(8)))
#define __aligned_be64 __be64 __attribute__((aligned(8)))
#define __aligned_le64 __le64 __attribute__((aligned(8)))

typedef unsigned __bitwise __poll_t;

#endif  
#endif  
