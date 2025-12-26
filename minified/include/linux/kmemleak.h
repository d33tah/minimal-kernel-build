#ifndef __KMEMLEAK_H
#define __KMEMLEAK_H
#include <linux/slab.h>
/* Only kmemleak_alloc_recursive is called from mm/slab.h */
static inline void kmemleak_alloc_recursive(const void *ptr, size_t size, int min_count, slab_flags_t flags, gfp_t gfp) {}
#endif
