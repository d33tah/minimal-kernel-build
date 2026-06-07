#ifndef _LINUX_HIGHUID_H
#define _LINUX_HIGHUID_H

#include <linux/types.h>




extern int overflowuid;
extern int overflowgid;

#define DEFAULT_OVERFLOWUID	65534
#define DEFAULT_OVERFLOWGID	65534

#endif  
