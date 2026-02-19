 
#ifndef _ASM_X86_APIC_H
#define _ASM_X86_APIC_H

#include <linux/cpumask.h>

#include <asm/alternative.h>
#include <asm/cpufeature.h>
#include <linux/atomic.h>
#include <asm/fixmap.h>
#include <asm/x86_init.h>
#include <asm/msr.h>
#include <asm/hardirq.h>

static inline void generic_apic_probe(void)
{
}

#endif
