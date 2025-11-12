#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

/* Allocate an array of spinlocks - STUBBED */

int __alloc_bucket_spinlocks(spinlock_t **locks, unsigned int *locks_mask,
			     size_t max_size, unsigned int cpu_mult, gfp_t gfp,
			     const char *name, struct lock_class_key *key)
{
	return -ENOMEM;
}
EXPORT_SYMBOL(__alloc_bucket_spinlocks);

void free_bucket_spinlocks(spinlock_t *locks)
{
}
EXPORT_SYMBOL(free_bucket_spinlocks);
