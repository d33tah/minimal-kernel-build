#include <linux/slab.h>
#include <linux/cpu.h>

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
#define CACHE_WRITE_THROUGH BIT(0)
#define CACHE_WRITE_BACK BIT(1)
#define CACHE_WRITE_POLICY_MASK (CACHE_WRITE_THROUGH | CACHE_WRITE_BACK)
#define CACHE_READ_ALLOCATE BIT(2)
#define CACHE_WRITE_ALLOCATE BIT(3)
#define CACHE_ALLOCATE_POLICY_MASK (CACHE_READ_ALLOCATE | CACHE_WRITE_ALLOCATE)
#define CACHE_ID BIT(4)
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

const struct attribute_group *cache_get_priv_group(struct cacheinfo *this_leaf);

/* get_cpu_cacheinfo_id, init_cache_level, populate_cache_leaves removed - never called */

/* init_intel_cacheinfo, init_amd_cacheinfo, init_hygon_cacheinfo, cacheinfo_hygon_init_llc_id,
   cacheinfo_amd_init_llc_id removed - no callers */
