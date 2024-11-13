/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_X86_APIC_H
#define _ASM_X86_APIC_H

#include <linux/cpumask.h>

#include <asm/alternative.h>
#include <asm/cpufeature.h>
#include <asm/apicdef.h>
#include <linux/atomic.h>
#include <asm/fixmap.h>
#include <asm/mpspec.h>
#include <asm/msr.h>
#include <asm/hardirq.h>

#define ARCH_APICTIMER_STOPS_ON_C3	1

/*
 * Debugging macros
 */
#define APIC_QUIET   0
#define APIC_VERBOSE 1
#define APIC_DEBUG   2

/* Macros for apic_extnmi which controls external NMI masking */
#define APIC_EXTNMI_BSP		0 /* Default */
#define APIC_EXTNMI_ALL		1
#define APIC_EXTNMI_NONE	2

/*
 * Define the default level of output to be very little
 * This can be turned up by using apic=verbose for more
 * information and apic=debug for _lots_ of information.
 * apic_verbosity is defined in apic.c
 */
#define apic_printk(v, s, a...) do {       \
		if ((v) <= apic_verbosity) \
			printk(s, ##a);    \
	} while (0)


static inline void generic_apic_probe(void)
{
}

static inline void lapic_shutdown(void) { }
#define local_apic_timer_c2_ok		1
static inline void init_apic_mappings(void) { }
static inline void disable_local_APIC(void) { }
# define setup_boot_APIC_clock x86_init_noop
# define setup_secondary_APIC_clock x86_init_noop
static inline void lapic_update_tsc_freq(void) { }
static inline void init_bsp_APIC(void) { }
static inline void apic_intr_mode_select(void) { }
static inline void apic_intr_mode_init(void) { }
static inline void lapic_assign_system_vectors(void) { }
static inline void lapic_assign_legacy_vector(unsigned int i, bool r) { }
static inline bool apic_needs_pit(void) { return true; }

static inline void check_x2apic(void) { }
static inline void x2apic_setup(void) { }
static inline int x2apic_enabled(void) { return 0; }

#define x2apic_mode		(0)
#define	x2apic_supported()	(0)

struct irq_data;

/*
 * Copyright 2004 James Cleverdon, IBM.
 *
 * Generic APIC sub-arch data struct.
 *
 * Hacked for x86-64 by James Cleverdon from i386 architecture code by
 * Martin Bligh, Andi Kleen, James Bottomley, John Stultz, and
 * James Cleverdon.
 */
struct apic {
	/* Hotpath functions first */
	void	(*eoi_write)(u32 reg, u32 v);
	void	(*native_eoi_write)(u32 reg, u32 v);
	void	(*write)(u32 reg, u32 v);
	u32	(*read)(u32 reg);

	/* IPI related functions */
	void	(*wait_icr_idle)(void);
	u32	(*safe_wait_icr_idle)(void);

	void	(*send_IPI)(int cpu, int vector);
	void	(*send_IPI_mask)(const struct cpumask *mask, int vector);
	void	(*send_IPI_mask_allbutself)(const struct cpumask *msk, int vec);
	void	(*send_IPI_allbutself)(int vector);
	void	(*send_IPI_all)(int vector);
	void	(*send_IPI_self)(int vector);

	u32	disable_esr;

	enum apic_delivery_modes delivery_mode;
	bool	dest_mode_logical;

	u32	(*calc_dest_apicid)(unsigned int cpu);

	/* ICR related functions */
	u64	(*icr_read)(void);
	void	(*icr_write)(u32 low, u32 high);

	/* Probe, setup and smpboot functions */
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

	/* wakeup_secondary_cpu */
	int	(*wakeup_secondary_cpu)(int apicid, unsigned long start_eip);
	/* wakeup secondary CPU using 64-bit wakeup point */
	int	(*wakeup_secondary_cpu_64)(int apicid, unsigned long start_eip);

	void	(*inquire_remote_apic)(int apicid);

	/*
	 * Called very early during boot from get_smp_config().  It should
	 * return the logical apicid.  x86_[bios]_cpu_to_apicid is
	 * initialized before this function is called.
	 *
	 * If logical apicid can't be determined that early, the function
	 * may return BAD_APICID.  Logical apicid will be configured after
	 * init_apic_ldr() while bringing up CPUs.  Note that NUMA affinity
	 * won't be applied properly during early boot in this case.
	 */
	int (*x86_32_early_logical_apicid)(int cpu);
	char	*name;
};

/*
 * Pointer to the local APIC driver in use on this system (there's
 * always just one such driver in use - the kernel decides via an
 * early probing process which one it picks - and then sticks to it):
 */
extern struct apic *apic;

/*
 * APIC drivers are probed based on how they are listed in the .apicdrivers
 * section. So the order is important and enforced by the ordering
 * of different apic driver files in the Makefile.
 *
 * For the files having two apic drivers, we use apic_drivers()
 * to enforce the order with in them.
 */
#define apic_driver(sym)					\
	static const struct apic *__apicdrivers_##sym __used		\
	__aligned(sizeof(struct apic *))			\
	__section(".apicdrivers") = { &sym }

#define apic_drivers(sym1, sym2)					\
	static struct apic *__apicdrivers_##sym1##sym2[2] __used	\
	__aligned(sizeof(struct apic *))				\
	__section(".apicdrivers") = { &sym1, &sym2 }

extern struct apic *__apicdrivers[], *__apicdrivers_end[];

/*
 * APIC functionality to boot other CPUs - only used on SMP:
 */


static inline u32 apic_read(u32 reg) { return 0; }
static inline void apic_write(u32 reg, u32 val) { }
static inline void apic_eoi(void) { }
static inline u64 apic_icr_read(void) { return 0; }
static inline void apic_icr_write(u32 low, u32 high) { }
static inline void apic_wait_icr_idle(void) { }
static inline u32 safe_apic_wait_icr_idle(void) { return 0; }
static inline void apic_set_eoi_write(void (*eoi_write)(u32 reg, u32 v)) {}


extern void apic_ack_irq(struct irq_data *data);

static inline void ack_APIC_irq(void)
{
	/*
	 * ack_APIC_irq() actually gets compiled as a single instruction
	 * ... yummie.
	 */
	apic_eoi();
}


static inline bool lapic_vector_set_in_irr(unsigned int vector)
{
	u32 irr = apic_read(APIC_IRR + (vector / 32 * 0x10));

	return !!(irr & (1U << (vector % 32)));
}

static inline unsigned default_get_apic_id(unsigned long x)
{
	unsigned int ver = GET_APIC_VERSION(apic_read(APIC_LVR));

	if (APIC_XAPIC(ver) || boot_cpu_has(X86_FEATURE_EXTD_APICID))
		return (x >> 24) & 0xFF;
	else
		return (x >> 24) & 0x0F;
}

/*
 * Warm reset vector position:
 */
#define TRAMPOLINE_PHYS_LOW		0x467
#define TRAMPOLINE_PHYS_HIGH		0x469

extern void generic_bigsmp_probe(void);


static inline bool apic_id_is_primary_thread(unsigned int id) { return false; }
static inline void apic_smt_update(void) { }

struct msi_msg;
struct irq_cfg;

extern void __irq_msi_compose_msg(struct irq_cfg *cfg, struct msi_msg *msg,
				  bool dmar);

extern void ioapic_zap_locks(void);

#endif /* _ASM_X86_APIC_H */
