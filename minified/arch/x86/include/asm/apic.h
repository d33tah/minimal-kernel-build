 
#ifndef _ASM_X86_APIC_H
#define _ASM_X86_APIC_H

#include <linux/cpumask.h>

#include <asm/alternative.h>
#include <asm/cpufeature.h>
#include <linux/atomic.h>
#include <asm/fixmap.h>
#include <asm/x86_init.h>
#define MAX_LOCAL_APIC 256
#define PHYSID_ARRAY_SIZE BITS_TO_LONGS(MAX_LOCAL_APIC)
struct physid_mask { unsigned long mask[PHYSID_ARRAY_SIZE]; };
typedef struct physid_mask physid_mask_t;
#include <asm/msr.h>
#include <asm/hardirq.h>

static inline void generic_apic_probe(void)
{
}

struct apic {
	 
	void	(*eoi_write)(u32 reg, u32 v);
	void	(*native_eoi_write)(u32 reg, u32 v);
	void	(*write)(u32 reg, u32 v);
	u32	(*read)(u32 reg);

	void	(*wait_icr_idle)(void);
	u32	(*safe_wait_icr_idle)(void);

	void	(*send_IPI)(int cpu, int vector);
	void	(*send_IPI_mask)(const struct cpumask *mask, int vector);
	void	(*send_IPI_mask_allbutself)(const struct cpumask *msk, int vec);
	void	(*send_IPI_allbutself)(int vector);
	void	(*send_IPI_all)(int vector);
	void	(*send_IPI_self)(int vector);

	u32	disable_esr;

	bool	dest_mode_logical;

	u32	(*calc_dest_apicid)(unsigned int cpu);

	u64	(*icr_read)(void);
	void	(*icr_write)(u32 low, u32 high);

	int	(*probe)(void);
	int	(*acpi_madt_oem_check)(char *oem_id, char *oem_table_id);
	int	(*apic_id_valid)(u32 apicid);
	int	(*apic_id_registered)(void);

	bool	(*check_apicid_used)(physid_mask_t *map, int apicid);
	void	(*init_apic_ldr)(void);
	void	(*ioapic_phys_id_map)(physid_mask_t *phys_map, physid_mask_t *retmap);
	void	(*setup_apic_routing)(void);
	int	(*cpu_present_to_apicid)(int mps_cpu);
	void	(*apicid_to_cpu_present)(int phys_apicid, physid_mask_t *retmap);
	int	(*check_phys_apicid_present)(int phys_apicid);
	int	(*phys_pkg_id)(int cpuid_apic, int index_msb);

	u32	(*get_apic_id)(unsigned long x);
	u32	(*set_apic_id)(unsigned int id);

	int	(*wakeup_secondary_cpu)(int apicid, unsigned long start_eip);
	 
	int	(*wakeup_secondary_cpu_64)(int apicid, unsigned long start_eip);

	void	(*inquire_remote_apic)(int apicid);

	int (*x86_32_early_logical_apicid)(int cpu);
	char	*name;
};

#endif
