 
 
#ifndef _LINUX_FIRMWARE_MAP_H
#define _LINUX_FIRMWARE_MAP_H

#include <linux/list.h>

 

static inline int firmware_map_add_early(u64 start, u64 end, const char *type)
{
	return 0;
}

static inline int firmware_map_add_hotplug(u64 start, u64 end, const char *type)
{
	return 0;
}

static inline int firmware_map_remove(u64 start, u64 end, const char *type)
{
	return 0;
}


#endif  
