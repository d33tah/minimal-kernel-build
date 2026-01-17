 
#ifndef ARCH_X86_CPU_H
#define ARCH_X86_CPU_H

 
struct cpu_dev {
	const char	*c_vendor;

	 
	const char	*c_ident[2];

	void            (*c_early_init)(struct cpuinfo_x86 *);
	void		(*c_bsp_init)(struct cpuinfo_x86 *);
	void		(*c_init)(struct cpuinfo_x86 *);
	void		(*c_identify)(struct cpuinfo_x86 *);
	/* c_detect_tlb removed - never called */
	int		c_x86_vendor;
	/* legacy_cache_size removed - never used */
	/* legacy_cpu_model_info removed - x86_model_id never read */
};

#define cpu_dev_register(cpu_devX) \
	static const struct cpu_dev *const __cpu_dev_##cpu_devX __used \
	__section(".x86_cpu_dev.init") = \
	&cpu_devX;

extern const struct cpu_dev *const __x86_cpu_dev_start[],
			    *const __x86_cpu_dev_end[];

/* enum tsx_ctrl_states and tsx_ctrl_state removed - never used */

extern void get_cpu_cap(struct cpuinfo_x86 *c);
extern void get_cpu_address_sizes(struct cpuinfo_x86 *c);
/* cpu_detect_cache_sizes, init_scattered_cpuid_features, init_intel_cacheinfo, init_amd_cacheinfo,
   init_hygon_cacheinfo, detect_num_cpu_cores, detect_extended_topology_early,
   detect_extended_topology, detect_ht_early, detect_ht, check_null_seg_clears_base,
   x86_read_arch_cap_msr, aperfmperf_get_khz removed - no callers */

#endif  
