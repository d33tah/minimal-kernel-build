 
#ifndef ARCH_X86_CPU_H
#define ARCH_X86_CPU_H

 
struct cpu_dev {
	const char	*c_vendor;

	void		(*c_init)(struct cpuinfo_x86 *);
};

/* cpu_dev_register macro + __x86_cpu_dev_start/end externs removed - the
 * .x86_cpu_dev.init section is empty (no vendor cpu_dev registered) and
 * early_cpu_init no longer walks it. */

/* tsx_init, tsx_ap_init, init_spectral_chicken, tsx_ctrl_state, enum tsx_ctrl_states removed - no callers */

extern void get_cpu_cap(struct cpuinfo_x86 *c);
extern void get_cpu_address_sizes(struct cpuinfo_x86 *c);
/* init_intel_cacheinfo, init_amd_cacheinfo, init_hygon_cacheinfo removed - no callers */

/* detect_extended_topology_early, detect_extended_topology, detect_ht_early, detect_ht, check_null_seg_clears_base, x86_read_arch_cap_msr, aperfmperf_get_khz removed - no callers */

#endif  
