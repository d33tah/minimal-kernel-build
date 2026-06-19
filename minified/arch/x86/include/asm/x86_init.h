 
#ifndef _ASM_X86_PLATFORM_H
#define _ASM_X86_PLATFORM_H

#include <asm/bootparam.h>

struct mpc_bus;
struct mpc_cpu;
struct mpc_table;

 
struct x86_init_mpparse {
	void (*find_smp_config)(void);
	void (*get_smp_config)(unsigned int early);
};

 
struct x86_init_resources {
	void (*reserve_resources)(void);
	char *(*memory_setup)(void);
};

 
struct x86_init_irqs {
	void (*pre_vector_init)(void);
	void (*intr_init)(void);
	void (*intr_mode_select)(void);
	void (*intr_mode_init)(void);
};


struct x86_init_paging {
	void (*pagetable_init)(void);
};

 
struct x86_init_timers {
	void (*setup_percpu_clockev)(void);
	void (*timer_init)(void);
	void (*wallclock_init)(void);
};

 
struct x86_init_acpi {
	void (*set_root_pointer)(u64 addr);
	u64 (*get_root_pointer)(void);
};


struct x86_init_ops {
	struct x86_init_resources	resources;
	struct x86_init_mpparse		mpparse;
	struct x86_init_irqs		irqs;
	struct x86_init_paging		paging;
	struct x86_init_timers		timers;
	struct x86_init_acpi		acpi;
};

 
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


struct x86_platform_ops {
	unsigned long (*calibrate_cpu)(void);
	unsigned long (*calibrate_tsc)(void);
	void (*get_wallclock)(struct timespec64 *ts);
	int (*set_wallclock)(const struct timespec64 *ts);
	unsigned char (*get_nmi_reason)(void);
	struct x86_legacy_features legacy;
	void (*set_legacy_features)(void);
};

extern struct x86_init_ops x86_init;
extern struct x86_platform_ops x86_platform;

extern void x86_early_init_platform_quirks(void);
extern void x86_init_noop(void);
extern void x86_init_uint_noop(unsigned int unused);
extern bool bool_x86_init_noop(void);

#endif
