// SPDX-License-Identifier: GPL-2.0
// Stubbed - no external usage found
#include <linux/export.h>
#include <linux/generic-radix-tree.h>

void *__genradix_ptr(struct __genradix *radix, size_t offset)
{
	return NULL;
}

void *__genradix_ptr_alloc(struct __genradix *radix, size_t offset,
			    gfp_t gfp_mask)
{
	return NULL;
}

void *__genradix_iter_peek(struct genradix_iter *iter,
			    struct __genradix *radix,
			    size_t objs_per_page)
{
	return NULL;
}

int __genradix_prealloc(struct __genradix *radix, size_t size,
			gfp_t gfp_mask)
{
	return -ENOMEM;
}

void __genradix_free(struct __genradix *radix)
{
}
