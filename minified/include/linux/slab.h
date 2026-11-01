/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Written by Mark Hemment, 1996 (markhe@nextd.demon.co.uk).
 *
 * (C) SGI 2006, Christoph Lameter
 * 	Cleaned up and restructured to ease the addition of alternative
 * 	implementations of SLAB allocators.
 * (C) Linux Foundation 2008-2013
 *      Unified interface for all slab allocators
 */

#ifndef _LINUX_SLAB_H
#define _LINUX_SLAB_H

#include <linux/gfp.h>
#include <linux/overflow.h>

/* Align objs on cache lines */
#define SLAB_HWCACHE_ALIGN ((slab_flags_t __force)0x00002000U)

/*
 * ZERO_SIZE_PTR will be returned for zero sized kmalloc requests.
 *
 * Dereferencing ZERO_SIZE_PTR will lead to a distinct access fault.
 *
 * ZERO_SIZE_PTR can be passed to kfree though in the same way that NULL can.
 * Both make kfree a no-op.
 */
#define ZERO_SIZE_PTR ((void *)16)

#define ZERO_OR_NULL_PTR(x) ((unsigned long)(x) <= (unsigned long)ZERO_SIZE_PTR)

/*
 * struct kmem_cache related prototypes
 */
void __init kmem_cache_init(void);
bool slab_is_available(void);

void kfree(const void *objp);

#define ARCH_KMALLOC_MINALIGN __alignof__(unsigned long long)

#define ARCH_SLAB_MINALIGN __alignof__(unsigned long long)

static inline unsigned int arch_slab_minalign(void)
{
	return ARCH_SLAB_MINALIGN;
}

/*
 * kmem_cache_alloc and friends return pointers aligned to ARCH_SLAB_MINALIGN.
 * kmalloc and friends return pointers aligned to both ARCH_KMALLOC_MINALIGN
 * and ARCH_SLAB_MINALIGN, but here we only assume the former alignment.
 */
#define __assume_kmalloc_alignment __assume_aligned(ARCH_KMALLOC_MINALIGN)
#define __assume_slab_alignment __assume_aligned(ARCH_SLAB_MINALIGN)

/*
 * SLUB directly allocates requests fitting in to an order-1 page
 * (PAGE_SIZE*2).  Larger requests are passed to the page allocator.
 */
#define KMALLOC_SHIFT_HIGH (PAGE_SHIFT + 1)
#define KMALLOC_SHIFT_LOW 3

/* Maximum size for which we actually use a slab cache */
#define KMALLOC_MAX_CACHE_SIZE (1UL << KMALLOC_SHIFT_HIGH)

#define KMALLOC_MIN_SIZE (1 << KMALLOC_SHIFT_LOW)

enum kmalloc_cache_type { KMALLOC_NORMAL = 0, NR_KMALLOC_TYPES };

extern struct kmem_cache
	*kmalloc_caches[NR_KMALLOC_TYPES][KMALLOC_SHIFT_HIGH + 1];

static __always_inline enum kmalloc_cache_type kmalloc_type(gfp_t flags)
{
	return KMALLOC_NORMAL;
}

/*
 * Figure out which kmalloc slab an allocation of a certain size
 * belongs to.
 * 0 = zero alloc
 * 1 =  65 .. 96 bytes
 * 2 = 129 .. 192 bytes
 * n = 2^(n-1)+1 .. 2^n
 *
 * Note: __kmalloc_index() is compile-time optimized, and not runtime optimized;
 * typical usage is via kmalloc_index() and therefore evaluated at compile-time.
 * Callers where !size_is_constant should only be test modules, where runtime
 * overheads of __kmalloc_index() can be tolerated.  Also see kmalloc_slab().
 */
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

	/* Will never be reached. Needed because the compiler may complain */
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

/**
 * kmalloc - allocate memory
 * @size: how many bytes of memory are required.
 * @flags: the type of memory to allocate.
 *
 * kmalloc is the normal method of allocating memory
 * for objects smaller than page size in the kernel.
 *
 * The allocated object address is aligned to at least ARCH_KMALLOC_MINALIGN
 * bytes. For @size of power of two bytes, the alignment is also guaranteed
 * to be at least to the size.
 *
 * The @flags argument may be one of the GFP flags defined at
 * include/linux/gfp.h and described at
 * :ref:`Documentation/core-api/mm-api.rst <mm-api-gfp-flags>`
 *
 * The recommended usage of the @flags is described at
 * :ref:`Documentation/core-api/memory-allocation.rst <memory_allocation>`
 *
 * Below is a brief outline of the most useful GFP flags
 *
 * %GFP_KERNEL
 *	Allocate normal kernel ram. May sleep.
 *
 * %GFP_NOWAIT
 *	Allocation will not sleep.
 *
 * %GFP_ATOMIC
 *	Allocation will not sleep.  May use emergency pools.
 *
 * %GFP_HIGHUSER
 *	Allocate memory from high memory on behalf of user.
 *
 * Also it is possible to set different flags by OR'ing
 * in one or more of the following additional @flags:
 *
 * %__GFP_HIGH
 *	This allocation has high priority and may use emergency pools.
 *
 * %__GFP_NOFAIL
 *	Indicate that this allocation is in no way allowed to fail
 *	(think twice before using).
 *
 * %__GFP_NORETRY
 *	If memory is not immediately available,
 *	then give up at once.
 *
 * %__GFP_NOWARN
 *	If allocation fails, don't issue any warnings.
 *
 * %__GFP_RETRY_MAYFAIL
 *	Try really hard to succeed the allocation but fail
 *	eventually.
 */
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

/*
 * Shortcuts
 */
static inline void *kmem_cache_zalloc(struct kmem_cache *k, gfp_t flags)
{
	return kmem_cache_alloc(k, flags | __GFP_ZERO);
}

/**
 * kzalloc - allocate memory. The memory is set to zero.
 * @size: how many bytes of memory are required.
 * @flags: the type of memory to allocate (see kmalloc).
 */
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
