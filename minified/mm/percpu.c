#include <linux/percpu.h>
#include <linux/slab.h>

/*
 * UP per-cpu allocator: with one CPU a dynamic per-cpu area is one zeroed
 * object and per_cpu_ptr() is the identity. The only user (irq descriptors)
 * allocates after kmem_cache_init().
 */
void __percpu *__alloc_percpu(size_t size, size_t align)
{
	return (void __percpu __force *)kzalloc(ALIGN(size, align), GFP_KERNEL);
}

phys_addr_t per_cpu_ptr_to_phys(void *addr)
{
	return __pa(addr);
}
