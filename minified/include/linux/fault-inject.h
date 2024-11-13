/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FAULT_INJECT_H
#define _LINUX_FAULT_INJECT_H


struct kmem_cache;

bool should_fail_alloc_page(gfp_t gfp_mask, unsigned int order);

int should_failslab(struct kmem_cache *s, gfp_t gfpflags);
static inline bool __should_failslab(struct kmem_cache *s, gfp_t gfpflags)
{
	return false;
}

#endif /* _LINUX_FAULT_INJECT_H */
