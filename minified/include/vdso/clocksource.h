#ifndef __VDSO_CLOCKSOURCE_H
#define __VDSO_CLOCKSOURCE_H

#include <vdso/limits.h>

#include <asm/vdso/clocksource.h>

enum vdso_clock_mode {
	VDSO_CLOCKMODE_NONE,
	VDSO_ARCH_CLOCKMODES,
	VDSO_CLOCKMODE_MAX,

	 
	VDSO_CLOCKMODE_TIMENS = INT_MAX
};

#endif  
