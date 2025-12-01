#ifndef __LINUX_FAULT_INJECT_USERCOPY_H__
#define __LINUX_FAULT_INJECT_USERCOPY_H__


#include <linux/types.h>
#include <linux/stddef.h>

static inline bool should_fail_usercopy(void) { return false; }


#endif  
