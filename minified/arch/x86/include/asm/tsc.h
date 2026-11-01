#ifndef _ASM_X86_TSC_H
#define _ASM_X86_TSC_H

#include <asm/cpufeature.h>

typedef unsigned long long cycles_t;

static inline cycles_t get_cycles(void)
{
	return rdtsc();
}

extern void tsc_early_init(void);
extern void tsc_init(void);

#endif
