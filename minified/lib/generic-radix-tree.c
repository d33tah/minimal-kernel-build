// SPDX-License-Identifier: GPL-2.0
// Stubbed - no external usage found
#include <linux/export.h>
#include <linux/generic-radix-tree.h>

void *__genradix_ptr(struct __genradix *radix, size_t offset)
{
	return NULL;
}
EXPORT_SYMBOL(__genradix_ptr);

void *__genradix_ptr_alloc(struct __genradix *radix, size_t offset,
			    gfp_t gfp_mask)
{
	return NULL;
}
EXPORT_SYMBOL(__genradix_ptr_alloc);

void *__genradix_iter_peek(struct genradix_iter *iter,
			    struct __genradix *radix,
			    size_t objs_per_page)
{
	return NULL;
}
EXPORT_SYMBOL(__genradix_iter_peek);

int __genradix_prealloc(struct __genradix *radix, size_t size,
			gfp_t gfp_mask)
{
	return -ENOMEM;
}
EXPORT_SYMBOL(__genradix_prealloc);

void __genradix_free(struct __genradix *radix)
{
}
EXPORT_SYMBOL(__genradix_free);
