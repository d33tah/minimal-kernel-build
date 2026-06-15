#ifndef _LINUX_RECIPROCAL_DIV_H
#define _LINUX_RECIPROCAL_DIV_H

#include <linux/types.h>


struct reciprocal_value {
	u32 m;
	u8 sh1, sh2;
};

struct reciprocal_value reciprocal_value(u32 d);

struct reciprocal_value_adv {
	u32 m;
	u8 sh, exp;
	bool is_wide_m;
};

struct reciprocal_value_adv reciprocal_value_adv(u32 d, u8 prec);

#endif  
