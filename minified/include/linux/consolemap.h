#ifndef __LINUX_CONSOLEMAP_H__
#define __LINUX_CONSOLEMAP_H__

#define LAT1_MAP 0
#define GRAF_MAP 1
#define IBMPC_MAP 2
#define USER_MAP 3

#include <linux/types.h>

struct vc_data;

static inline u16 inverse_translate(const struct vc_data *conp, int glyph, int use_unicode)
{
	return glyph;
}
static inline unsigned short *set_translate(int m, struct vc_data *vc)
{
	return NULL;
}
static inline int conv_uni_to_pc(struct vc_data *conp, long ucs)
{
	return ucs < 0x100 ? ucs : -1;
}
static inline void console_map_init(void) { }
static inline int con_set_default_unimap(struct vc_data *vc) { return 0; }
static inline void con_free_unimap(struct vc_data *vc) { }

#endif
