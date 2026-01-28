 
#ifndef _ASM_X86_PLATFORM_H
#define _ASM_X86_PLATFORM_H

#include <asm/bootparam.h>

/* struct ghcb, mpc_bus, mpc_cpu, mpc_table, pt_regs, cpuinfo_x86, irq_domain forward decls removed - unused in this header */

/* x86_init_mpparse removed - never called */

 
struct x86_init_resources {
	/* probe_roms removed - never called */
	void (*reserve_resources)(void);
	char *(*memory_setup)(void);
};

 
struct x86_init_irqs {
	void (*pre_vector_init)(void);
	void (*intr_init)(void);
	/* intr_mode_select, intr_mode_init, create_pci_msi_domain removed - never called */
};

/* x86_init_oem removed - never accessed */

struct x86_init_paging {
	void (*pagetable_init)(void);
};

/* x86_init_timers struct emptied - setup_percpu_clockev, timer_init, wallclock_init never called */
struct x86_init_timers {
};

/* x86_init_iommu, x86_init_pci, x86_hyper_init, x86_init_acpi, x86_guest removed - never used */

struct x86_init_ops {
	struct x86_init_resources	resources;
	/* mpparse field removed - never called */
	struct x86_init_irqs		irqs;
	/* oem field removed - never accessed */
	struct x86_init_paging		paging;
	struct x86_init_timers		timers;
	/* iommu, pci, hyper, acpi fields removed - never accessed */
};

 
/* x86_cpuinit_ops removed - never accessed */

struct timespec64;

 
/* x86_legacy_devices, x86_legacy_i8042_state, most x86_legacy_features fields removed - never read */
struct x86_legacy_features {
	int reserve_bios_regions;
};

/* x86_hyper_runtime removed - never called */

struct x86_platform_ops {
	unsigned long (*calibrate_cpu)(void);
	unsigned long (*calibrate_tsc)(void);
	void (*get_wallclock)(struct timespec64 *ts);
	/* set_wallclock, iommu_shutdown removed - never called */
	bool (*is_untracked_pat_range)(u64 start, u64 end);
	/* nmi_init removed - never called */
	unsigned char (*get_nmi_reason)(void);
	/* save/restore_sched_clock_state, apic_post_init removed - never called */
	struct x86_legacy_features legacy;
	/* set_legacy_features, hyper removed - never called */
	/* guest removed - never called */
};

/* x86_apic_ops removed - never accessed */

extern struct x86_init_ops x86_init;
/* x86_cpuinit removed - never accessed */
extern struct x86_platform_ops x86_platform;

/* x86_early_init_platform_quirks removed - inlined into head32.c */
extern void x86_init_noop(void);
/* x86_init_uint_noop, bool_x86_init_noop, x86_op_int_noop, x86_pnpbios_disabled removed - never called */

#endif
