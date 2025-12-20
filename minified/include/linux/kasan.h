/* Minimal kasan.h - stubs for CONFIG_KASAN disabled */
#ifndef _LINUX_KASAN_H
#define _LINUX_KASAN_H

#include <linux/types.h>

/* --- 2025-12-06 20:30 --- kasan-enabled.h inlined (18 LOC) */
/* kasan_enabled removed - never called */
static inline bool kasan_hw_tags_enabled(void) { return false; }
/* --- end kasan-enabled.h inlined --- */

struct kmem_cache;
struct page;
struct slab;
struct vm_struct;
struct task_struct;


typedef unsigned int __bitwise kasan_vmalloc_flags_t;

#define KASAN_VMALLOC_NONE		((__force kasan_vmalloc_flags_t)0x00u)
#define KASAN_VMALLOC_INIT		((__force kasan_vmalloc_flags_t)0x01u)
#define KASAN_VMALLOC_VM_ALLOC		((__force kasan_vmalloc_flags_t)0x02u)
#define KASAN_VMALLOC_PROT_NORMAL	((__force kasan_vmalloc_flags_t)0x04u)



/* kasan_enable_current, kasan_disable_current removed - unused */




static inline bool kasan_has_integrated_init(void)
{
	return kasan_hw_tags_enabled();
}


static inline slab_flags_t kasan_never_merge(void)
{
	return 0;
}
/* kasan_unpoison_range removed - unused */
static inline void kasan_cache_create(struct kmem_cache *cache,
				      unsigned int *size,
				      slab_flags_t *flags) {}
static inline void kasan_cache_create_kmalloc(struct kmem_cache *cache) {}
static inline void kasan_poison_slab(struct slab *slab) {}
static inline void kasan_unpoison_object_data(struct kmem_cache *cache,
					void *object) {}
static inline void kasan_poison_object_data(struct kmem_cache *cache,
					void *object) {}
static inline void *kasan_init_slab_obj(struct kmem_cache *cache,
				const void *object)
{
	return (void *)object;
}
static inline bool kasan_slab_free(struct kmem_cache *s, void *object, bool init)
{
	return false;
}
static inline void kasan_kfree_large(void *ptr) {}
static inline void *kasan_slab_alloc(struct kmem_cache *s, void *object,
				   gfp_t flags, bool init)
{
	return object;
}
static inline void *kasan_kmalloc(struct kmem_cache *s, const void *object,
				size_t size, gfp_t flags)
{
	return (void *)object;
}
static inline void *kasan_kmalloc_large(const void *ptr, size_t size, gfp_t flags)
{
	return (void *)ptr;
}
/* kasan_krealloc, kasan_check_byte removed - unused */


static inline void kasan_unpoison_task_stack(struct task_struct *task) {}


static inline void kasan_record_aux_stack(void *ptr) {}



static inline void *kasan_reset_tag(const void *addr)
{
	return (void *)addr;
}





/* kasan_populate_early_vm_area_shadow removed - unused */
static inline int kasan_populate_vmalloc(unsigned long start,
					unsigned long size)
{
	return 0;
}

static inline void *kasan_unpoison_vmalloc(const void *start,
					   unsigned long size,
					   kasan_vmalloc_flags_t flags)
{
	return (void *)start;
}
static inline void kasan_poison_vmalloc(const void *start, unsigned long size)
{ }


static inline void kasan_free_module_shadow(const struct vm_struct *vm) {}

static inline void kasan_non_canonical_hook(unsigned long addr) { }

#endif  
