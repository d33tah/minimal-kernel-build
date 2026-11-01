#ifndef _LINUX_SLAB_H
#define _LINUX_SLAB_H

#include <linux/gfp.h>
#include <linux/overflow.h>

#define SLAB_HWCACHE_ALIGN ((slab_flags_t __force)0x00002000U)

#define ZERO_SIZE_PTR ((void *)16)

#define ZERO_OR_NULL_PTR(x) ((unsigned long)(x) <= (unsigned long)ZERO_SIZE_PTR)

void __init kmem_cache_init(void);
bool slab_is_available(void);

void kfree(const void *objp);

#define ARCH_KMALLOC_MINALIGN __alignof__(unsigned long long)

#define ARCH_SLAB_MINALIGN __alignof__(unsigned long long)

static inline unsigned int arch_slab_minalign(void)
{
	return ARCH_SLAB_MINALIGN;
}

#define __assume_kmalloc_alignment __assume_aligned(ARCH_KMALLOC_MINALIGN)
#define __assume_slab_alignment __assume_aligned(ARCH_SLAB_MINALIGN)

#define KMALLOC_SHIFT_HIGH (PAGE_SHIFT + 1)
#define KMALLOC_SHIFT_LOW 3

#define KMALLOC_MAX_CACHE_SIZE (1UL << KMALLOC_SHIFT_HIGH)

#define KMALLOC_MIN_SIZE (1 << KMALLOC_SHIFT_LOW)

enum kmalloc_cache_type { KMALLOC_NORMAL = 0, NR_KMALLOC_TYPES };

extern struct kmem_cache
	*kmalloc_caches[NR_KMALLOC_TYPES][KMALLOC_SHIFT_HIGH + 1];

static __always_inline enum kmalloc_cache_type kmalloc_type(gfp_t flags)
{
	return KMALLOC_NORMAL;
}

static __always_inline unsigned int __kmalloc_index(size_t size,
						    bool size_is_constant)
{
	if (!size)
		return 0;

	if (size <= KMALLOC_MIN_SIZE)
		return KMALLOC_SHIFT_LOW;

	if (KMALLOC_MIN_SIZE <= 32 && size > 64 && size <= 96)
		return 1;
	if (KMALLOC_MIN_SIZE <= 64 && size > 128 && size <= 192)
		return 2;
	if (size <= 8)
		return 3;
	if (size <= 16)
		return 4;
	if (size <= 32)
		return 5;
	if (size <= 64)
		return 6;
	if (size <= 128)
		return 7;
	if (size <= 256)
		return 8;
	if (size <= 512)
		return 9;
	if (size <= 1024)
		return 10;
	if (size <= 2 * 1024)
		return 11;
	if (size <= 4 * 1024)
		return 12;
	if (size <= 8 * 1024)
		return 13;

	if (!IS_ENABLED(CONFIG_PROFILE_ALL_BRANCHES) && size_is_constant)
		BUILD_BUG_ON_MSG(1, "unexpected size in kmalloc_index()");
	else
		BUG();

	return -1;
}
#define kmalloc_index(s) __kmalloc_index(s, true)

void *__kmalloc(size_t size, gfp_t flags) __assume_kmalloc_alignment
	__alloc_size(1);
void *kmem_cache_alloc(struct kmem_cache *s,
		       gfp_t flags) __assume_slab_alignment __malloc;

static __always_inline
	__alloc_size(1) void *__kmalloc_node(size_t size, gfp_t flags, int node)
{
	return __kmalloc(size, flags);
}

static __always_inline void *kmem_cache_alloc_node(struct kmem_cache *s,
						   gfp_t flags, int node)
{
	return kmem_cache_alloc(s, flags);
}

static __always_inline
	__alloc_size(3) void *kmem_cache_alloc_trace(struct kmem_cache *s,
						     gfp_t flags, size_t size)
{
	void *ret = kmem_cache_alloc(s, flags);

	return ret;
}

static __always_inline void *kmem_cache_alloc_node_trace(struct kmem_cache *s,
							 gfp_t gfpflags,
							 int node, size_t size)
{
	void *ret = kmem_cache_alloc_node(s, gfpflags, node);

	return ret;
}

static __always_inline __alloc_size(1) void *kmalloc(size_t size, gfp_t flags)
{
	if (__builtin_constant_p(size)) {
		unsigned int index;
		index = kmalloc_index(size);

		if (!index)
			return ZERO_SIZE_PTR;

		return kmem_cache_alloc_trace(
			kmalloc_caches[kmalloc_type(flags)][index], flags,
			size);
	}
	return __kmalloc(size, flags);
}

static __always_inline __alloc_size(1) void *kmalloc_node(size_t size,
							  gfp_t flags, int node)
{
	if (__builtin_constant_p(size) && size <= KMALLOC_MAX_CACHE_SIZE) {
		unsigned int i = kmalloc_index(size);

		if (!i)
			return ZERO_SIZE_PTR;

		return kmem_cache_alloc_node_trace(
			kmalloc_caches[kmalloc_type(flags)][i], flags, node,
			size);
	}
	return __kmalloc_node(size, flags, node);
}

static inline __alloc_size(1, 2) void *kmalloc_array(size_t n, size_t size,
						     gfp_t flags)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(n, size, &bytes)))
		return NULL;
	if (__builtin_constant_p(n) && __builtin_constant_p(size))
		return kmalloc(bytes, flags);
	return __kmalloc(bytes, flags);
}

static inline __alloc_size(1, 2) void *kcalloc(size_t n, size_t size,
					       gfp_t flags)
{
	return kmalloc_array(n, size, flags | __GFP_ZERO);
}

static inline void *kmem_cache_zalloc(struct kmem_cache *k, gfp_t flags)
{
	return kmem_cache_alloc(k, flags | __GFP_ZERO);
}

static inline __alloc_size(1) void *kzalloc(size_t size, gfp_t flags)
{
	return kmalloc(size, flags | __GFP_ZERO);
}

static inline __alloc_size(1) void *kzalloc_node(size_t size, gfp_t flags,
						 int node)
{
	return kmalloc_node(size, flags | __GFP_ZERO, node);
}

#endif
