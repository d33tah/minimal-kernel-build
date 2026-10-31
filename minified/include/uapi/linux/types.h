#ifndef _UAPI_LINUX_TYPES_H
#define _UAPI_LINUX_TYPES_H

#include <asm/types.h>

#ifndef __ASSEMBLY__
/* userspace-only #ifndef __KERNEL__ #warning block removed - never compiled
 * (kernel build always defines __KERNEL__). */

#include <asm/posix_types.h>

#define __bitwise

typedef __u32 __bitwise __le32;

#endif  
#endif  
