 
#ifndef _ASM_X86_PLATFORM_H
#define _ASM_X86_PLATFORM_H

#include <asm/bootparam.h>

struct ghcb;
/* struct mpc_bus, mpc_cpu, mpc_table removed - unused */
struct pt_regs;
struct cpuinfo_x86;
struct irq_domain;

 
struct x86_init_mpparse {
	void (*setup_ioapic_ids)(void);
	void (*find_smp_config)(void);
	void (*get_smp_config)(unsigned int early);
};

 
struct x86_init_resources {
	void (*probe_roms)(void);
	void (*reserve_resources)(void);
	char *(*memory_setup)(void);
};

 
struct x86_init_irqs {
	void (*pre_vector_init)(void);
	void (*intr_init)(void);
	void (*intr_mode_select)(void);
	void (*intr_mode_init)(void);
	struct irq_domain *(*create_pci_msi_domain)(void);
};

/* x86_init_oem removed - never accessed */

struct x86_init_paging {
	void (*pagetable_init)(void);
};

 
struct x86_init_timers {
	void (*setup_percpu_clockev)(void);
	void (*timer_init)(void);
	void (*wallclock_init)(void);
};

/* x86_init_iommu removed - never used */

struct x86_init_pci {
	int (*arch_init)(void);
	int (*init)(void);
	void (*init_irq)(void);
	void (*fixup_irqs)(void);
};

 
struct x86_hyper_init {
	void (*init_platform)(void);
	void (*guest_late_init)(void);
	bool (*x2apic_available)(void);
	bool (*msi_ext_dest_id)(void);
	void (*init_mem_mapping)(void);
	void (*init_after_bootmem)(void);
};

/* x86_init_acpi removed - never used */
/* x86_guest removed - never called */

struct x86_init_ops {
	struct x86_init_resources	resources;
	struct x86_init_mpparse		mpparse;
	struct x86_init_irqs		irqs;
	/* oem field removed - never accessed */
	struct x86_init_paging		paging;
	struct x86_init_timers		timers;
	/* iommu field removed - never used */
	struct x86_init_pci		pci;
	struct x86_hyper_init		hyper;
	/* acpi field removed - never used */
};

 
/* x86_cpuinit_ops removed - never accessed */

struct timespec64;

 
struct x86_legacy_devices {
	int pnpbios;
};

 
enum x86_legacy_i8042_state {
	X86_LEGACY_I8042_PLATFORM_ABSENT,
	X86_LEGACY_I8042_FIRMWARE_ABSENT,
	X86_LEGACY_I8042_EXPECTED_PRESENT,
};

 
struct x86_legacy_features {
	enum x86_legacy_i8042_state i8042;
	int rtc;
	int warm_reset;
	int no_vga;
	int reserve_bios_regions;
	struct x86_legacy_devices devices;
};

/* x86_hyper_runtime removed - never called */

struct x86_platform_ops {
	unsigned long (*calibrate_cpu)(void);
	unsigned long (*calibrate_tsc)(void);
	void (*get_wallclock)(struct timespec64 *ts);
	/* set_wallclock, iommu_shutdown removed - never called */
	bool (*is_untracked_pat_range)(u64 start, u64 end);
	void (*nmi_init)(void);
	unsigned char (*get_nmi_reason)(void);
	/* save/restore_sched_clock_state, apic_post_init removed - never called */
	struct x86_legacy_features legacy;
	void (*set_legacy_features)(void);
	/* hyper removed - never called */
	/* guest removed - never called */
};

/* x86_apic_ops removed - never accessed */

extern struct x86_init_ops x86_init;
/* x86_cpuinit removed - never accessed */
extern struct x86_platform_ops x86_platform;

extern void x86_early_init_platform_quirks(void);
extern void x86_init_noop(void);
extern void x86_init_uint_noop(unsigned int unused);
extern bool bool_x86_init_noop(void);
extern void x86_op_int_noop(int cpu);
/* x86_pnpbios_disabled removed - never called */

#endif
