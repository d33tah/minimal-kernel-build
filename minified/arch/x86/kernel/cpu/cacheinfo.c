#include <linux/slab.h>
#include <linux/cpu.h>

/* --- 2025-12-06 17:22 --- cacheinfo.h inlined */
#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/smp.h>

struct device_node;
struct attribute;

enum cache_type {
	CACHE_TYPE_NOCACHE = 0,
};

extern unsigned int coherency_max_size;

struct cacheinfo {
	unsigned int id;
	enum cache_type type;
	unsigned int level;
	unsigned int coherency_line_size;
	unsigned int number_of_sets;
	unsigned int ways_of_associativity;
	unsigned int physical_line_partition;
	unsigned int size;
	cpumask_t shared_cpu_map;
	unsigned int attributes;
#define CACHE_WRITE_THROUGH	BIT(0)
#define CACHE_WRITE_BACK	BIT(1)
#define CACHE_WRITE_POLICY_MASK	(CACHE_WRITE_THROUGH | CACHE_WRITE_BACK)
#define CACHE_READ_ALLOCATE	BIT(2)
#define CACHE_WRITE_ALLOCATE	BIT(3)
#define CACHE_ALLOCATE_POLICY_MASK (CACHE_READ_ALLOCATE | CACHE_WRITE_ALLOCATE)
#define CACHE_ID		BIT(4)
	void *fw_token;
	bool disable_sysfs;
	void *priv;
};

struct cpu_cacheinfo {
	struct cacheinfo *info_list;
	unsigned int num_levels;
	unsigned int num_leaves;
	bool cpu_map_populated;
};

struct cpu_cacheinfo *get_cpu_cacheinfo(unsigned int cpu);
int init_cache_level(unsigned int cpu);
int populate_cache_leaves(unsigned int cpu);
int cache_setup_acpi(unsigned int cpu);

static inline int acpi_find_last_cache_level(unsigned int cpu) { return 0; }

const struct attribute_group *cache_get_priv_group(struct cacheinfo *this_leaf);

static inline int get_cpu_cacheinfo_id(int cpu, int level)
{
	struct cpu_cacheinfo *ci = get_cpu_cacheinfo(cpu);
	int i;
	for (i = 0; i < ci->num_leaves; i++) {
		if (ci->info_list[i].level == level) {
			if (ci->info_list[i].attributes & CACHE_ID)
				return ci->info_list[i].id;
			return -1;
		}
	}
	return -1;
}
/* --- end cacheinfo.h inlined --- */

int init_cache_level(unsigned int cpu)
{
	return -ENOENT;
}

int populate_cache_leaves(unsigned int cpu)
{
	return -ENOENT;
}

void init_intel_cacheinfo(struct cpuinfo_x86 *c)
{
	 
}

void init_amd_cacheinfo(struct cpuinfo_x86 *c)
{
	 
}

void init_hygon_cacheinfo(struct cpuinfo_x86 *c)
{
	 
}

void cacheinfo_hygon_init_llc_id(struct cpuinfo_x86 *c, int cpu)
{
	 
}

void cacheinfo_amd_init_llc_id(struct cpuinfo_x86 *c, int cpu)
{
	 
}
