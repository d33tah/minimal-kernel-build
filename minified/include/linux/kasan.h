#ifndef _LINUX_KASAN_H
#define _LINUX_KASAN_H

#include <linux/types.h>

struct kmem_cache;

static inline bool kasan_has_integrated_init(void) { return false; }
static inline void *kasan_slab_alloc(struct kmem_cache *s, void *object, gfp_t flags, bool init) { return object; }
static inline void *kasan_reset_tag(const void *addr) { return (void *)addr; }

#endif
