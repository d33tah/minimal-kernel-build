
#ifndef __IDR_H__
#define __IDR_H__

#include <linux/radix-tree.h>
#include <linux/gfp.h>
#include <linux/percpu.h>

struct idr {
	struct radix_tree_root	idr_rt;
	unsigned int		idr_base;
	unsigned int		idr_next;
};

#define IDR_FREE	0

#define IDR_RT_MARKER	(ROOT_IS_IDR | (__force gfp_t)			\
					(1 << (ROOT_TAG_SHIFT + IDR_FREE)))

#define IDR_INIT_BASE(name, base) {					\
	.idr_rt = RADIX_TREE_INIT(name, IDR_RT_MARKER),			\
	.idr_base = (base),						\
	.idr_next = 0,							\
}

#define IDR_INIT(name) IDR_INIT_BASE(name, 0)
/* idr_get_cursor inlined into pid.c (~2 LOC) */
/* idr_set_cursor inlined into pid.c (~2 LOC) */

void idr_preload(gfp_t gfp_mask);

int idr_alloc(struct idr *, void *ptr, int start, int end, gfp_t);
int __must_check idr_alloc_u32(struct idr *, void *ptr, u32 *id,
				unsigned long max, gfp_t);
int idr_alloc_cyclic(struct idr *, void *ptr, int start, int end, gfp_t);
void *idr_remove(struct idr *, unsigned long id);
void *idr_find(const struct idr *, unsigned long id);
void *idr_replace(struct idr *, void *, unsigned long id);

static inline void idr_init_base(struct idr *idr, int base)
{
	INIT_RADIX_TREE(&idr->idr_rt, IDR_RT_MARKER);
	idr->idr_base = base;
	idr->idr_next = 0;
}

/* idr_init inlined into pid.c (~2 LOC) */
/* idr_preload_end inlined into pid.c (~2 LOC) */

#define IDA_CHUNK_SIZE		128
#define IDA_BITMAP_LONGS	(IDA_CHUNK_SIZE / sizeof(long))
#define IDA_BITMAP_BITS 	(IDA_BITMAP_LONGS * sizeof(long) * 8)

struct ida_bitmap {
	unsigned long		bitmap[IDA_BITMAP_LONGS];
};

struct ida {
	struct xarray xa;
};

#define IDA_INIT_FLAGS	(XA_FLAGS_LOCK_IRQ | XA_FLAGS_ALLOC)

#define IDA_INIT(name)	{						\
	.xa = XARRAY_INIT(name, IDA_INIT_FLAGS)				\
}
#define DEFINE_IDA(name)	struct ida name = IDA_INIT(name)

int ida_alloc_range(struct ida *, unsigned int min, unsigned int max, gfp_t);
void ida_free(struct ida *, unsigned int id);

/* ida_alloc inlined into namespace.c (~3 LOC) */
/* ida_alloc_min inlined into namespace.c (~3 LOC) */

#endif  
