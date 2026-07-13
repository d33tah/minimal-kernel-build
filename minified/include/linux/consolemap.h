#ifndef __LINUX_CONSOLEMAP_H__
#define __LINUX_CONSOLEMAP_H__

#include <linux/types.h>

struct vc_data;

static inline int conv_uni_to_pc(struct vc_data *conp, long ucs) {
	return ucs < 0x100 ? ucs : -1; }
static inline int con_set_default_unimap(struct vc_data *vc) { return 0; }
static inline void con_free_unimap(struct vc_data *vc) { }

#endif
