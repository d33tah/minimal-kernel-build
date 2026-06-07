#ifndef _LINUX_SET_MEMORY_H_
#define _LINUX_SET_MEMORY_H_

#include <asm/set_memory.h>

#ifndef can_set_direct_map
static inline bool can_set_direct_map(void)
{
	return true;
}
#define can_set_direct_map can_set_direct_map
#endif

#endif  
