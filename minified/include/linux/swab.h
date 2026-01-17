#ifndef _LINUX_SWAB_H
#define _LINUX_SWAB_H

#include <uapi/linux/swab.h>

# define swab16 __swab16
# define swab32 __swab32
# define swab64 __swab64
# define swab __swab
/* swab*p, swab*s, swahw32, swahb32 macros removed - unused */
#endif  
