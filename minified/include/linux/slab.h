 
 

#ifndef _LINUX_SLAB_H
#define	_LINUX_SLAB_H

#include <linux/gfp.h>
#include <linux/overflow.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/percpu-refcount.h>


 
 
#define SLAB_CONSISTENCY_CHECKS	((slab_flags_t __force)0x00000100U)
 
#define SLAB_RED_ZONE		((slab_flags_t __force)0x00000400U)
 
#define SLAB_POISON		((slab_flags_t __force)0x00000800U)
 
#define SLAB_HWCACHE_ALIGN	((slab_flags_t __force)0x00002000U)
 
#define SLAB_CACHE_DMA		((slab_flags_t __force)0x00004000U)
 
#define SLAB_CACHE_DMA32	((slab_flags_t __force)0x00008000U)
 
#define SLAB_STORE_USER		((slab_flags_t __force)0x00010000U)
 
#define SLAB_PANIC		((slab_flags_t __force)0x00040000U)
 
 
#define SLAB_TYPESAFE_BY_RCU	((slab_flags_t __force)0x00080000U)
 
#define SLAB_MEM_SPREAD		((slab_flags_t __force)0x00100000U)
 
#define SLAB_TRACE		((slab_flags_t __force)0x00200000U)

 
# define SLAB_DEBUG_OBJECTS	0

 
#define SLAB_NOLEAKTRACE	((slab_flags_t __force)0x00800000U)

 
# define SLAB_FAILSLAB		0
 
# define SLAB_ACCOUNT		0

#define SLAB_KASAN		0

 
#define SLAB_NO_USER_FLAGS	((slab_flags_t __force)0x10000000U)

 
 
#define SLAB_RECLAIM_ACCOUNT	((slab_flags_t __force)0x00020000U)
#define SLAB_TEMPORARY		SLAB_RECLAIM_ACCOUNT	 

 
#define ZERO_SIZE_PTR ((void *)16)

#define ZERO_OR_NULL_PTR(x) ((unsigned long)(x) <= \
				(unsigned long)ZERO_SIZE_PTR)

#include <linux/kasan.h>

struct list_lru;
struct mem_cgroup;
 
void __init kmem_cache_init(void);
bool slab_is_available(void);

struct kmem_cache *kmem_cache_create(const char *name, unsigned int size,
			unsigned int align, slab_flags_t flags,
			void (*ctor)(void *));
struct kmem_cache *kmem_cache_create_usercopy(const char *name,
			unsigned int size, unsigned int align,
			slab_flags_t flags,
			unsigned int useroffset, unsigned int usersize,
			void (*ctor)(void *));
void kmem_cache_destroy(struct kmem_cache *s);
int kmem_cache_shrink(struct kmem_cache *s);

 
#define KMEM_CACHE(__struct, __flags)					\
		kmem_cache_create(#__struct, sizeof(struct __struct),	\
			__alignof__(struct __struct), (__flags), NULL)

 
#define KMEM_CACHE_USERCOPY(__struct, __flags, __field)			\
		kmem_cache_create_usercopy(#__struct,			\
			sizeof(struct __struct),			\
			__alignof__(struct __struct), (__flags),	\
			offsetof(struct __struct, __field),		\
			sizeof_field(struct __struct, __field), NULL)

 
void * __must_check krealloc(const void *objp, size_t new_size, gfp_t flags) __alloc_size(2);
void kfree(const void *objp);
void kfree_sensitive(const void *objp);
size_t __ksize(const void *objp);
size_t ksize(const void *objp);

 
#if defined(ARCH_DMA_MINALIGN) && ARCH_DMA_MINALIGN > 8
#define ARCH_KMALLOC_MINALIGN ARCH_DMA_MINALIGN
#define KMALLOC_MIN_SIZE ARCH_DMA_MINALIGN
#define KMALLOC_SHIFT_LOW ilog2(ARCH_DMA_MINALIGN)
#else
#define ARCH_KMALLOC_MINALIGN __alignof__(unsigned long long)
#endif

 
#ifndef ARCH_SLAB_MINALIGN
#define ARCH_SLAB_MINALIGN __alignof__(unsigned long long)
#endif

 
#ifndef arch_slab_minalign
static inline unsigned int arch_slab_minalign(void)
{
	return ARCH_SLAB_MINALIGN;
}
#endif

 
#define __assume_kmalloc_alignment __assume_aligned(ARCH_KMALLOC_MINALIGN)
#define __assume_slab_alignment __assume_aligned(ARCH_SLAB_MINALIGN)
#define __assume_page_alignment __assume_aligned(PAGE_SIZE)

 


 
#define KMALLOC_SHIFT_HIGH	(PAGE_SHIFT + 1)
#define KMALLOC_SHIFT_MAX	(MAX_ORDER + PAGE_SHIFT - 1)
#ifndef KMALLOC_SHIFT_LOW
#define KMALLOC_SHIFT_LOW	3
#endif


 
#define KMALLOC_MAX_SIZE	(1UL << KMALLOC_SHIFT_MAX)
 
#define KMALLOC_MAX_CACHE_SIZE	(1UL << KMALLOC_SHIFT_HIGH)
 
#define KMALLOC_MAX_ORDER	(KMALLOC_SHIFT_MAX - PAGE_SHIFT)

 
#ifndef KMALLOC_MIN_SIZE
#define KMALLOC_MIN_SIZE (1 << KMALLOC_SHIFT_LOW)
#endif

 
#define SLAB_OBJ_MIN_SIZE      (KMALLOC_MIN_SIZE < 16 ? \
                               (KMALLOC_MIN_SIZE) : 16)

 
enum kmalloc_cache_type {
	KMALLOC_NORMAL = 0,
	KMALLOC_DMA = KMALLOC_NORMAL,
	KMALLOC_CGROUP = KMALLOC_NORMAL,
	KMALLOC_RECLAIM,
	NR_KMALLOC_TYPES
};

extern struct kmem_cache *
kmalloc_caches[NR_KMALLOC_TYPES][KMALLOC_SHIFT_HIGH + 1];

 
#define KMALLOC_NOT_NORMAL_BITS					\
	(__GFP_RECLAIMABLE |					\
	(IS_ENABLED(CONFIG_ZONE_DMA)   ? __GFP_DMA : 0) |	\
	(IS_ENABLED(CONFIG_MEMCG_KMEM) ? __GFP_ACCOUNT : 0))

static __always_inline enum kmalloc_cache_type kmalloc_type(gfp_t flags)
{
	 
	if (likely((flags & KMALLOC_NOT_NORMAL_BITS) == 0))
		return KMALLOC_NORMAL;

	 
	if (IS_ENABLED(CONFIG_ZONE_DMA) && (flags & __GFP_DMA))
		return KMALLOC_DMA;
	if (!IS_ENABLED(CONFIG_MEMCG_KMEM) || (flags & __GFP_RECLAIMABLE))
		return KMALLOC_RECLAIM;
	else
		return KMALLOC_CGROUP;
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
	if (size <=          8) return 3;
	if (size <=         16) return 4;
	if (size <=         32) return 5;
	if (size <=         64) return 6;
	if (size <=        128) return 7;
	if (size <=        256) return 8;
	if (size <=        512) return 9;
	if (size <=       1024) return 10;
	if (size <=   2 * 1024) return 11;
	if (size <=   4 * 1024) return 12;
	if (size <=   8 * 1024) return 13;
	if (size <=  16 * 1024) return 14;
	if (size <=  32 * 1024) return 15;
	if (size <=  64 * 1024) return 16;
	if (size <= 128 * 1024) return 17;
	if (size <= 256 * 1024) return 18;
	if (size <= 512 * 1024) return 19;
	if (size <= 1024 * 1024) return 20;
	if (size <=  2 * 1024 * 1024) return 21;
	if (size <=  4 * 1024 * 1024) return 22;
	if (size <=  8 * 1024 * 1024) return 23;
	if (size <=  16 * 1024 * 1024) return 24;
	if (size <=  32 * 1024 * 1024) return 25;

	if (!IS_ENABLED(CONFIG_PROFILE_ALL_BRANCHES) && size_is_constant)
		BUILD_BUG_ON_MSG(1, "unexpected size in kmalloc_index()");
	else
		BUG();

	 
	return -1;
}
#define kmalloc_index(s) __kmalloc_index(s, true)

void *__kmalloc(size_t size, gfp_t flags) __assume_kmalloc_alignment __alloc_size(1);
void *kmem_cache_alloc(struct kmem_cache *s, gfp_t flags) __assume_slab_alignment __malloc;
void *kmem_cache_alloc_lru(struct kmem_cache *s, struct list_lru *lru,
			   gfp_t gfpflags) __assume_slab_alignment __malloc;
void kmem_cache_free(struct kmem_cache *s, void *objp);

 
void kmem_cache_free_bulk(struct kmem_cache *s, size_t size, void **p);
int kmem_cache_alloc_bulk(struct kmem_cache *s, gfp_t flags, size_t size, void **p);

static __always_inline __alloc_size(1) void *__kmalloc_node(size_t size, gfp_t flags, int node)
{
	return __kmalloc(size, flags);
}

static __always_inline void *kmem_cache_alloc_node(struct kmem_cache *s, gfp_t flags, int node)
{
	return kmem_cache_alloc(s, flags);
}

static __always_inline __alloc_size(3) void *kmem_cache_alloc_trace(struct kmem_cache *s,
								    gfp_t flags, size_t size)
{
	void *ret = kmem_cache_alloc(s, flags);

	ret = kasan_kmalloc(s, ret, size, flags);
	return ret;
}

static __always_inline void *kmem_cache_alloc_node_trace(struct kmem_cache *s, gfp_t gfpflags,
							 int node, size_t size)
{
	void *ret = kmem_cache_alloc_node(s, gfpflags, node);

	ret = kasan_kmalloc(s, ret, size, gfpflags);
	return ret;
}

extern void *kmalloc_order(size_t size, gfp_t flags, unsigned int order) __assume_page_alignment
									 __alloc_size(1);

static __always_inline __alloc_size(1) void *kmalloc_order_trace(size_t size, gfp_t flags,
								 unsigned int order)
{
	return kmalloc_order(size, flags, order);
}

static __always_inline __alloc_size(1) void *kmalloc_large(size_t size, gfp_t flags)
{
	unsigned int order = get_order(size);
	return kmalloc_order_trace(size, flags, order);
}

 
static __always_inline __alloc_size(1) void *kmalloc(size_t size, gfp_t flags)
{
	if (__builtin_constant_p(size)) {
		unsigned int index;
		if (size > KMALLOC_MAX_CACHE_SIZE)
			return kmalloc_large(size, flags);
		index = kmalloc_index(size);

		if (!index)
			return ZERO_SIZE_PTR;

		return kmem_cache_alloc_trace(
				kmalloc_caches[kmalloc_type(flags)][index],
				flags, size);
	}
	return __kmalloc(size, flags);
}

static __always_inline __alloc_size(1) void *kmalloc_node(size_t size, gfp_t flags, int node)
{
	if (__builtin_constant_p(size) &&
		size <= KMALLOC_MAX_CACHE_SIZE) {
		unsigned int i = kmalloc_index(size);

		if (!i)
			return ZERO_SIZE_PTR;

		return kmem_cache_alloc_node_trace(
				kmalloc_caches[kmalloc_type(flags)][i],
						flags, node, size);
	}
	return __kmalloc_node(size, flags, node);
}

 
static inline __alloc_size(1, 2) void *kmalloc_array(size_t n, size_t size, gfp_t flags)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(n, size, &bytes)))
		return NULL;
	if (__builtin_constant_p(n) && __builtin_constant_p(size))
		return kmalloc(bytes, flags);
	return __kmalloc(bytes, flags);
}


static inline __alloc_size(1, 2) void *kcalloc(size_t n, size_t size, gfp_t flags)
{
	return kmalloc_array(n, size, flags | __GFP_ZERO);
}

 
extern void *__kmalloc_track_caller(size_t size, gfp_t flags, unsigned long caller);
#define kmalloc_track_caller(size, flags) \
	__kmalloc_track_caller(size, flags, _RET_IP_)

#define kmalloc_node_track_caller(size, flags, node) \
	kmalloc_track_caller(size, flags)


 
static inline void *kmem_cache_zalloc(struct kmem_cache *k, gfp_t flags)
{
	return kmem_cache_alloc(k, flags | __GFP_ZERO);
}

 
static inline __alloc_size(1) void *kzalloc(size_t size, gfp_t flags)
{
	return kmalloc(size, flags | __GFP_ZERO);
}

 
static inline __alloc_size(1) void *kzalloc_node(size_t size, gfp_t flags, int node)
{
	return kmalloc_node(size, flags | __GFP_ZERO, node);
}

extern void *kvmalloc_node(size_t size, gfp_t flags, int node) __alloc_size(1);
static inline __alloc_size(1) void *kvmalloc(size_t size, gfp_t flags)
{
	return kvmalloc_node(size, flags, NUMA_NO_NODE);
}
static inline __alloc_size(1) void *kvzalloc(size_t size, gfp_t flags)
{
	return kvmalloc(size, flags | __GFP_ZERO);
}

static inline __alloc_size(1, 2) void *kvmalloc_array(size_t n, size_t size, gfp_t flags)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(n, size, &bytes)))
		return NULL;

	return kvmalloc(bytes, flags);
}

extern void *kvrealloc(const void *p, size_t oldsize, size_t newsize, gfp_t flags)
		      __alloc_size(3);
extern void kvfree(const void *addr);
extern void kvfree_sensitive(const void *addr, size_t len);

unsigned int kmem_cache_size(struct kmem_cache *s);
void __init kmem_cache_init_late(void);

#define slab_prepare_cpu	NULL
#define slab_dead_cpu		NULL

#endif	 
