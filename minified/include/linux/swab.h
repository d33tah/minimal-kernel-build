#ifndef _LINUX_SWAB_H
#define _LINUX_SWAB_H

#include <uapi/linux/swab.h>

# define swab __swab
/* swab16/32/64, swahw32, swahb32 + all p/s variant aliases removed - only swab (find_bit.c) used */
#endif  
