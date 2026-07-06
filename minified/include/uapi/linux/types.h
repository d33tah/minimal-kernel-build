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

typedef __u16 __bitwise __le16;
typedef __u32 __bitwise __le32;

typedef unsigned __bitwise __poll_t;

#endif  
#endif  
