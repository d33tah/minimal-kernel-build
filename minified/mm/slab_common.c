#include <linux/slab.h>

#include <linux/mm.h>
#include <linux/cache.h>
#include <linux/compiler.h>
#include <linux/uaccess.h>
#include <asm/page.h>
#include <linux/memcontrol.h>

#include "internal.h"

#include "slab.h"

enum slab_state slab_state;
LIST_HEAD(slab_caches);
DEFINE_MUTEX(slab_mutex);
struct kmem_cache *kmem_cache;

static unsigned int calculate_alignment(slab_flags_t flags, unsigned int align,
					unsigned int size)
{
	if (flags & SLAB_HWCACHE_ALIGN) {
		unsigned int ralign;

		ralign = cache_line_size();
		while (size <= ralign / 2)
			ralign /= 2;
		align = max(align, ralign);
	}

	align = max(
		align,
		(unsigned int)
			ARCH_SLAB_MINALIGN); /* arch_slab_minalign() inlined */

	return ALIGN(align, sizeof(void *));
}

static struct kmem_cache *
create_cache(const char *name, unsigned int object_size, unsigned int align,
	     slab_flags_t flags, unsigned int useroffset, unsigned int usersize,
	     void (*ctor)(void *), struct kmem_cache *root_cache)
{
	struct kmem_cache *s;
	int err;

	if (useroffset + usersize > object_size)
		useroffset = usersize = 0;

	err = -ENOMEM;
	s = kmem_cache_zalloc(kmem_cache, GFP_KERNEL);
	if (!s)
		goto out;

	s->name = name;
	s->size = s->object_size = object_size;
	s->align = align;
	s->ctor = ctor;
	s->useroffset = useroffset;
	s->usersize = usersize;

	err = __kmem_cache_create(s, flags);
	if (err)
		goto out_free_cache;

	s->refcount = 1;
	list_add(&s->list, &slab_caches);
out:
	if (err)
		return ERR_PTR(err);
	return s;

out_free_cache:
	kmem_cache_free(kmem_cache, s);
	goto out;
}

struct kmem_cache *
kmem_cache_create_usercopy(const char *name, unsigned int size,
			   unsigned int align, slab_flags_t flags,
			   unsigned int useroffset, unsigned int usersize,
			   void (*ctor)(void *))
{
	struct kmem_cache *s = NULL;
	const char *cache_name;
	int err = 0;

	mutex_lock(&slab_mutex);

	if (flags & ~SLAB_FLAGS_PERMITTED) {
		err = -EINVAL;
		goto out_unlock;
	}

	flags &= CACHE_CREATE_MASK;

	if ((!usersize && useroffset) ||
	    (size < usersize || size - usersize < useroffset))
		usersize = useroffset = 0;

	cache_name = kstrdup_const(name, GFP_KERNEL);
	if (!cache_name) {
		err = -ENOMEM;
		goto out_unlock;
	}

	s = create_cache(cache_name, size,
			 calculate_alignment(flags, align, size), flags,
			 useroffset, usersize, ctor, NULL);
	if (IS_ERR(s)) {
		err = PTR_ERR(s);
		kfree_const(cache_name);
	}

out_unlock:
	mutex_unlock(&slab_mutex);

	if (err) {
		if (flags & SLAB_PANIC)
			panic("slab create fail");
		return NULL;
	}
	return s;
}

struct kmem_cache *kmem_cache_create(const char *name, unsigned int size,
				     unsigned int align, slab_flags_t flags,
				     void (*ctor)(void *))
{
	return kmem_cache_create_usercopy(name, size, align, flags, 0, 0, ctor);
}

bool slab_is_available(void)
{
	return slab_state >= UP;
}

void __init create_boot_cache(struct kmem_cache *s, const char *name,
			      unsigned int size, slab_flags_t flags,
			      unsigned int useroffset, unsigned int usersize)
{
	int err;
	unsigned int align = ARCH_KMALLOC_MINALIGN;

	s->name = name;
	s->size = s->object_size = size;

	if (is_power_of_2(size))
		align = max(align, size);
	s->align = calculate_alignment(flags, align, size);

	s->useroffset = useroffset;
	s->usersize = usersize;

	err = __kmem_cache_create(s, flags);

	if (err)
		panic("Creation of kmalloc slab %s size=%u failed. Reason %d\n",
		      name, size, err);

	s->refcount = -1;
}

struct kmem_cache *__init create_kmalloc_cache(const char *name,
					       unsigned int size,
					       slab_flags_t flags,
					       unsigned int useroffset,
					       unsigned int usersize)
{
	struct kmem_cache *s = kmem_cache_zalloc(kmem_cache, GFP_NOWAIT);

	if (!s)
		panic("Out of memory when creating slab %s\n", name);

	create_boot_cache(s, name, size, flags, useroffset, usersize);
	list_add(&s->list, &slab_caches);
	s->refcount = 1;
	return s;
}

struct kmem_cache *kmalloc_caches[NR_KMALLOC_TYPES]
				 [KMALLOC_SHIFT_HIGH + 1] __ro_after_init = {};

static u8 size_index[24] __ro_after_init = {
	3, 4, 5, 5, 6, 6, 6, 6, 1, 1, 1, 1, 7, 7, 7, 7, 2, 2, 2, 2, 2, 2, 2, 2
};

static inline unsigned int size_index_elem(unsigned int bytes)
{
	return (bytes - 1) / 8;
}

struct kmem_cache *kmalloc_slab(size_t size, gfp_t flags)
{
	unsigned int index;

	if (size <= 192) {
		if (!size)
			return ZERO_SIZE_PTR;

		index = size_index[size_index_elem(size)];
	} else {
		if (size > KMALLOC_MAX_CACHE_SIZE)
			return NULL;
		index = fls(size - 1);
	}

	return kmalloc_caches[kmalloc_type(flags)][index];
}

#define INIT_KMALLOC_INFO(__size, __short_size)                        \
	{                                                              \
		.name[KMALLOC_NORMAL] = "kmalloc-" #__short_size,      \
		.name[KMALLOC_RECLAIM] = "kmalloc-rcl-" #__short_size, \
		.size = __size,                                        \
	}

const struct kmalloc_info_struct kmalloc_info[] __initconst = {
	INIT_KMALLOC_INFO(0, 0),	  INIT_KMALLOC_INFO(96, 96),
	INIT_KMALLOC_INFO(192, 192),	  INIT_KMALLOC_INFO(8, 8),
	INIT_KMALLOC_INFO(16, 16),	  INIT_KMALLOC_INFO(32, 32),
	INIT_KMALLOC_INFO(64, 64),	  INIT_KMALLOC_INFO(128, 128),
	INIT_KMALLOC_INFO(256, 256),	  INIT_KMALLOC_INFO(512, 512),
	INIT_KMALLOC_INFO(1024, 1k),	  INIT_KMALLOC_INFO(2048, 2k),
	INIT_KMALLOC_INFO(4096, 4k),	  INIT_KMALLOC_INFO(8192, 8k),
	INIT_KMALLOC_INFO(16384, 16k),	  INIT_KMALLOC_INFO(32768, 32k),
	INIT_KMALLOC_INFO(65536, 64k),	  INIT_KMALLOC_INFO(131072, 128k),
	INIT_KMALLOC_INFO(262144, 256k),  INIT_KMALLOC_INFO(524288, 512k),
	INIT_KMALLOC_INFO(1048576, 1M),	  INIT_KMALLOC_INFO(2097152, 2M),
	INIT_KMALLOC_INFO(4194304, 4M),	  INIT_KMALLOC_INFO(8388608, 8M),
	INIT_KMALLOC_INFO(16777216, 16M), INIT_KMALLOC_INFO(33554432, 32M)
};

static void __init new_kmalloc_cache(int idx, enum kmalloc_cache_type type,
				     slab_flags_t flags)
{
	/* CONFIG_MEMCG_KMEM and CONFIG_ZONE_DMA not enabled */
	if (type == KMALLOC_RECLAIM)
		flags |= SLAB_RECLAIM_ACCOUNT;

	kmalloc_caches[type][idx] = create_kmalloc_cache(
		kmalloc_info[idx].name[type], kmalloc_info[idx].size, flags, 0,
		kmalloc_info[idx].size);
}

void __init create_kmalloc_caches(slab_flags_t flags)
{
	int i;
	enum kmalloc_cache_type type;

	for (type = KMALLOC_NORMAL; type < NR_KMALLOC_TYPES; type++) {
		for (i = KMALLOC_SHIFT_LOW; i <= KMALLOC_SHIFT_HIGH; i++) {
			if (!kmalloc_caches[type][i])
				new_kmalloc_cache(i, type, flags);

			if (KMALLOC_MIN_SIZE <= 32 && i == 6 &&
			    !kmalloc_caches[type][1])
				new_kmalloc_cache(1, type, flags);
			if (KMALLOC_MIN_SIZE <= 64 && i == 7 &&
			    !kmalloc_caches[type][2])
				new_kmalloc_cache(2, type, flags);
		}
	}

	slab_state = UP;
}

gfp_t kmalloc_fix_flags(gfp_t flags)
{
	gfp_t invalid_mask = flags & GFP_SLAB_BUG_MASK;

	flags &= ~GFP_SLAB_BUG_MASK;

	return flags;
}

void *kmalloc_order(size_t size, gfp_t flags, unsigned int order)
{
	void *ret = NULL;
	struct page *page;

	if (unlikely(flags & GFP_SLAB_BUG_MASK))
		flags = kmalloc_fix_flags(flags);

	flags |= __GFP_COMP;
	page = alloc_pages(flags, order);
	if (likely(page)) {
		ret = page_address(page);
		mod_lruvec_page_state(page, NR_SLAB_UNRECLAIMABLE_B,
				      PAGE_SIZE << order);
	}

	return ret;
}
