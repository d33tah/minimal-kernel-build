#ifndef __LINUX_CONSOLEMAP_H__
#define __LINUX_CONSOLEMAP_H__

#define LAT1_MAP 0
#define GRAF_MAP 1

#include <linux/types.h>

struct vc_data;

static inline unsigned short *set_translate(int m, struct vc_data *vc)
{
	return NULL;
}
static inline int conv_uni_to_pc(struct vc_data *conp, long ucs)
{
	return ucs < 0x100 ? ucs : -1;
}
/* console_map_init, con_set_default_unimap, con_free_unimap removed - unused */

#endif
