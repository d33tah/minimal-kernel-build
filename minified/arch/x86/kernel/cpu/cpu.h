 
#ifndef ARCH_X86_CPU_H
#define ARCH_X86_CPU_H

 
struct cpu_dev {
	const char	*c_vendor;

	void		(*c_init)(struct cpuinfo_x86 *);
	int		c_x86_vendor;
};

#define cpu_dev_register(cpu_devX) \
	static const struct cpu_dev *const __cpu_dev_##cpu_devX __used \
	__section(".x86_cpu_dev.init") = \
	&cpu_devX;

extern const struct cpu_dev *const __x86_cpu_dev_start[],
			    *const __x86_cpu_dev_end[];

/* tsx_init, tsx_ap_init, init_spectral_chicken, tsx_ctrl_state, enum tsx_ctrl_states removed - no callers */

extern void get_cpu_cap(struct cpuinfo_x86 *c);
extern void get_cpu_address_sizes(struct cpuinfo_x86 *c);
extern void init_scattered_cpuid_features(struct cpuinfo_x86 *c);
/* init_intel_cacheinfo, init_amd_cacheinfo, init_hygon_cacheinfo removed - no callers */

/* detect_extended_topology_early, detect_extended_topology, detect_ht_early, detect_ht, check_null_seg_clears_base, x86_read_arch_cap_msr, aperfmperf_get_khz removed - no callers */

#endif  
