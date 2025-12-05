#ifndef _LINUX_KASAN_ENABLED_H
#define _LINUX_KASAN_ENABLED_H

#include <linux/jump_label.h>


static inline bool kasan_enabled(void)
{
	return IS_ENABLED(CONFIG_KASAN);
}

static inline bool kasan_hw_tags_enabled(void)
{
	return false;
}


#endif  
