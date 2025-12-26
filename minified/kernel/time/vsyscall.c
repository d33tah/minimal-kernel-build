/* Simplified vsyscall - no VDSO optimization needed for minimal kernel */

#include <linux/timekeeper_internal.h>
#include <vdso/datapage.h>
#include <asm/vdso/vsyscall.h>

#include "timekeeping_internal.h"

void update_vsyscall(struct timekeeper *tk)
{
	/* Stub - VDSO not needed for Hello World */
}
