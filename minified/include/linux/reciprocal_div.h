#ifndef _LINUX_RECIPROCAL_DIV_H
#define _LINUX_RECIPROCAL_DIV_H

#include <linux/types.h>

struct reciprocal_value {
	u32 m;
	u8 sh1, sh2;
};

struct reciprocal_value reciprocal_value(u32 d);

/* reciprocal_value_adv struct and function removed - never used */

#endif  
