 
#ifndef _ASM_X86_PLATFORM_H
#define _ASM_X86_PLATFORM_H

#include <asm/bootparam.h>

struct x86_init_resources {
	void (*reserve_resources)(void);
	char *(*memory_setup)(void);
};

struct x86_init_irqs {
	void (*pre_vector_init)(void);
	void (*intr_init)(void);
};

struct x86_init_paging {
	void (*pagetable_init)(void);
};

struct x86_init_ops {
	struct x86_init_resources	resources;
	struct x86_init_irqs		irqs;
	struct x86_init_paging		paging;
};

struct timespec64;

struct x86_legacy_features {
	int reserve_bios_regions;
};

struct x86_platform_ops {
	unsigned long (*calibrate_cpu)(void);
	unsigned long (*calibrate_tsc)(void);
	void (*get_wallclock)(struct timespec64 *ts);
	bool (*is_untracked_pat_range)(u64 start, u64 end);
	unsigned char (*get_nmi_reason)(void);
	struct x86_legacy_features legacy;
};

extern struct x86_init_ops x86_init;
extern struct x86_platform_ops x86_platform;

#endif
