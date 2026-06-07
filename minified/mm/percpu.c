
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/memblock.h>
#include <linux/slab.h>

#define __addr_to_pcpu_ptr(addr) (void __percpu *)(addr)

const unsigned long *pcpu_unit_offsets __ro_after_init;

/* Bump allocator state */
static void *pcpu_bump_base __ro_after_init;
static size_t pcpu_bump_offset;
static size_t pcpu_bump_limit;

static struct pcpu_alloc_info *__init pcpu_alloc_alloc_info(int nr_groups,
							    int nr_units)
{
	struct pcpu_alloc_info *ai;
	size_t base_size, ai_size;
	void *ptr;
	int unit;

	base_size = ALIGN(sizeof(*ai) + nr_groups * sizeof(ai->groups[0]),
			  __alignof__(ai->groups[0].cpu_map[0]));
	ai_size = base_size + nr_units * sizeof(ai->groups[0].cpu_map[0]);

	ptr = memblock_alloc(PFN_ALIGN(ai_size), PAGE_SIZE);
	if (!ptr)
		return NULL;
	ai = ptr;
	ptr += base_size;

	ai->groups[0].cpu_map = ptr;

	for (unit = 0; unit < nr_units; unit++)
		ai->groups[0].cpu_map[unit] = NR_CPUS;

	ai->nr_groups = nr_groups;
	ai->__ai_size = PFN_ALIGN(ai_size);

	return ai;
}

static void __init pcpu_free_alloc_info(struct pcpu_alloc_info *ai)
{
	memblock_free(ai, ai->__ai_size);
}

void __percpu *__alloc_percpu(size_t size, size_t align)
{
	size_t offset;

	if (unlikely(!size || align > PAGE_SIZE || !is_power_of_2(align)))
		return NULL;

	if (align < PCPU_MIN_ALLOC_SIZE)
		align = PCPU_MIN_ALLOC_SIZE;
	size = ALIGN(size, PCPU_MIN_ALLOC_SIZE);

	offset = ALIGN(pcpu_bump_offset, align);
	if (offset + size > pcpu_bump_limit) {
		pr_warn("percpu: allocation failed, size=%zu align=%zu\n", size,
			align);
		return NULL;
	}

	memset(pcpu_bump_base + offset, 0, size);
	pcpu_bump_offset = offset + size;

	return __addr_to_pcpu_ptr(pcpu_bump_base + offset);
}

phys_addr_t per_cpu_ptr_to_phys(void *addr)
{
	return __pa(addr);
}

static void __init pcpu_setup_first_chunk(const struct pcpu_alloc_info *ai,
					  void *base_addr)
{
	size_t static_size;
	unsigned long *unit_off;
	size_t alloc_size;

	alloc_size = nr_cpu_ids * sizeof(unit_off[0]);
	unit_off = memblock_alloc(alloc_size, SMP_CACHE_BYTES);
	if (!unit_off)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      alloc_size);

	unit_off[0] = ai->groups[0].base_offset;
	pcpu_unit_offsets = unit_off;

	static_size = ALIGN(ai->static_size, PCPU_MIN_ALLOC_SIZE);

	pcpu_bump_base = base_addr + static_size;
	pcpu_bump_offset = 0;
	pcpu_bump_limit = ai->dyn_size - (static_size - ai->static_size);
}

void __init setup_per_cpu_areas(void)
{
	const size_t unit_size = roundup_pow_of_two(
		max_t(size_t, PCPU_MIN_UNIT_SIZE, PERCPU_DYNAMIC_RESERVE));
	struct pcpu_alloc_info *ai;
	void *fc;

	ai = pcpu_alloc_alloc_info(1, 1);
	fc = memblock_alloc_try_nid(unit_size, PAGE_SIZE, __pa(MAX_DMA_ADDRESS),
				    MEMBLOCK_ALLOC_ACCESSIBLE, NUMA_NO_NODE);
	if (!ai || !fc)
		panic("Failed to allocate memory for percpu areas.");

	ai->dyn_size = unit_size;
	ai->unit_size = unit_size;
	ai->groups[0].nr_units = 1;
	ai->groups[0].cpu_map[0] = 0;

	pcpu_setup_first_chunk(ai, fc);
	pcpu_free_alloc_info(ai);
}
