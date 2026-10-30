#include <linux/percpu.h>
#include <linux/slab.h>

/*
 * UP per-cpu allocator: with one CPU a dynamic per-cpu area is one zeroed
 * object and per_cpu_ptr() is the identity. slub's boot caches allocate
 * theirs before kmalloc works, so those come from a static pool.
 */
static char pcpu_early[PAGE_SIZE] __aligned(SMP_CACHE_BYTES);
static size_t pcpu_early_used;

void __percpu *__alloc_percpu(size_t size, size_t align)
{
	void *p;

	size = ALIGN(size, align);
	if (slab_is_available())
		return (void __percpu __force *)kzalloc(size, GFP_KERNEL);

	pcpu_early_used = ALIGN(pcpu_early_used, align);
	if (WARN_ON(pcpu_early_used + size > sizeof(pcpu_early)))
		return NULL;
	p = pcpu_early + pcpu_early_used;
	pcpu_early_used += size;
	return (void __percpu __force *)p;
}

phys_addr_t per_cpu_ptr_to_phys(void *addr)
{
	return __pa(addr);
}
