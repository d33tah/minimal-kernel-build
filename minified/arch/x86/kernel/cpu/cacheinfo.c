#include <linux/slab.h>
#include <linux/cacheinfo.h>
#include <linux/cpu.h>

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
