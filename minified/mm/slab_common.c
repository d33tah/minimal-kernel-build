#include "internal.h"

#include "slab.h"

enum slab_state slab_state;
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

	align = max(align, arch_slab_minalign());

	return ALIGN(align, sizeof(void *));
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

	s->size = s->object_size = size;

	if (is_power_of_2(size))
		align = max(align, size);
	s->align = calculate_alignment(flags, align, size);

	err = __kmem_cache_create(s, flags);

	if (err)
		panic("Creation of kmalloc slab %s size=%u failed. Reason %d\n",
		      name, size, err);
}

static struct kmem_cache *__init create_kmalloc_cache(const char *name,
						      unsigned int size,
						      slab_flags_t flags,
						      unsigned int useroffset,
						      unsigned int usersize)
{
	struct kmem_cache *s = kmem_cache_zalloc(kmem_cache, GFP_NOWAIT);

	if (!s)
		panic("Out of memory when creating slab %s\n", name);

	create_boot_cache(s, name, size, flags, useroffset, usersize);
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
		if (WARN_ON_ONCE(size > KMALLOC_MAX_CACHE_SIZE))
			return NULL;
		index = fls(size - 1);
	}

	return kmalloc_caches[kmalloc_type(flags)][index];
}

#define INIT_KMALLOC_INFO(__size, __short_size)                   \
	{                                                         \
		.name[KMALLOC_NORMAL] = "kmalloc-" #__short_size, \
		.size = __size,                                   \
	}

const struct kmalloc_info_struct kmalloc_info[] __initconst = {
	INIT_KMALLOC_INFO(0, 0),     INIT_KMALLOC_INFO(96, 96),
	INIT_KMALLOC_INFO(192, 192), INIT_KMALLOC_INFO(8, 8),
	INIT_KMALLOC_INFO(16, 16),   INIT_KMALLOC_INFO(32, 32),
	INIT_KMALLOC_INFO(64, 64),   INIT_KMALLOC_INFO(128, 128),
	INIT_KMALLOC_INFO(256, 256), INIT_KMALLOC_INFO(512, 512),
	INIT_KMALLOC_INFO(1024, 1k), INIT_KMALLOC_INFO(2048, 2k),
	INIT_KMALLOC_INFO(4096, 4k), INIT_KMALLOC_INFO(8192, 8k)
};

void __init setup_kmalloc_cache_index_table(void)
{
	unsigned int i;

	BUILD_BUG_ON(KMALLOC_MIN_SIZE > 256 ||
		     !is_power_of_2(KMALLOC_MIN_SIZE));

	for (i = 8; i < KMALLOC_MIN_SIZE; i += 8) {
		unsigned int elem = size_index_elem(i);

		if (elem >= ARRAY_SIZE(size_index))
			break;
		size_index[elem] = KMALLOC_SHIFT_LOW;
	}
}

static void __init new_kmalloc_cache(int idx, enum kmalloc_cache_type type,
				     slab_flags_t flags)
{
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
	}
	return ret;
}
