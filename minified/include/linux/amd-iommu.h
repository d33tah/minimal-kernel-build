 
 

#ifndef _ASM_X86_AMD_IOMMU_H
#define _ASM_X86_AMD_IOMMU_H

#include <linux/types.h>

struct amd_iommu;

 
struct amd_iommu_pi_data {
	u32 ga_tag;
	u32 prev_ga_tag;
	u64 base;
	bool is_guest_mode;
	struct vcpu_data *vcpu_data;
	void *ir_data;
};


static inline int amd_iommu_detect(void) { return -ENODEV; }



static inline int
amd_iommu_register_ga_log_notifier(int (*notifier)(u32))
{
	return 0;
}

static inline int
amd_iommu_update_ga(int cpu, bool is_run, void *data)
{
	return 0;
}

static inline int amd_iommu_activate_guest_mode(void *data)
{
	return 0;
}

static inline int amd_iommu_deactivate_guest_mode(void *data)
{
	return 0;
}

int amd_iommu_get_num_iommus(void);
bool amd_iommu_pc_supported(void);
u8 amd_iommu_pc_get_max_banks(unsigned int idx);
u8 amd_iommu_pc_get_max_counters(unsigned int idx);
int amd_iommu_pc_set_reg(struct amd_iommu *iommu, u8 bank, u8 cntr, u8 fxn,
		u64 *value);
int amd_iommu_pc_get_reg(struct amd_iommu *iommu, u8 bank, u8 cntr, u8 fxn,
		u64 *value);
struct amd_iommu *get_amd_iommu(unsigned int idx);

#endif  
