 
#ifndef __LINUX_FAULT_INJECT_USERCOPY_H__
#define __LINUX_FAULT_INJECT_USERCOPY_H__

 

#include <linux/types.h>


static inline bool should_fail_usercopy(void) { return false; }


#endif  
