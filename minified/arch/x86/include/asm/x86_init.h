 
#ifndef _ASM_X86_PLATFORM_H
#define _ASM_X86_PLATFORM_H

#include <asm/bootparam.h>


 
struct x86_init_mpparse { void (*find_smp_config)(void); void (*get_smp_config)(unsigned int early); };

 
struct x86_init_resources { void (*reserve_resources)(void); char *(*memory_setup)(void); };

 
struct x86_init_irqs { void (*pre_vector_init)(void); void (*intr_init)(void); };


struct x86_init_paging { void (*pagetable_init)(void); };

 
struct x86_init_timers { void (*timer_init)(void); };


struct x86_init_ops { struct x86_init_resources	resources; struct x86_init_mpparse		mpparse; struct x86_init_irqs		irqs; struct x86_init_paging		paging; struct x86_init_timers		timers; };

struct x86_legacy_features { int reserve_bios_regions; };


struct x86_platform_ops { unsigned long (*calibrate_cpu)(void); unsigned long (*calibrate_tsc)(void); unsigned char (*get_nmi_reason)(void); struct x86_legacy_features legacy; };

extern struct x86_init_ops x86_init;
extern struct x86_platform_ops x86_platform;

extern void x86_early_init_platform_quirks(void);
extern void x86_init_noop(void);
extern void x86_init_uint_noop(unsigned int unused);

#endif
