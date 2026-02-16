#ifndef __VDSO_DATAPAGE_H
#define __VDSO_DATAPAGE_H

#ifndef __ASSEMBLY__

#include <linux/compiler.h>
#include <uapi/linux/time.h>
#include <uapi/linux/types.h>
#include <linux/bits.h>
#include <linux/limits.h>
#include <linux/clocksource.h>
#include <linux/ktime.h>
#include <linux/time64.h>
#include <linux/time.h>

struct arch_vdso_data {};

#define VDSO_BASES	(CLOCK_TAI + 1)

#define CS_HRES_COARSE	0
#define CS_RAW		1
#define CS_BASES	(CS_RAW + 1)

struct vdso_timestamp {
	u64	sec;
	u64	nsec;
};

struct vdso_data {
	u32			seq;

	s32			clock_mode;
	u64			cycle_last;
	u64			mask;
	u32			mult;
	u32			shift;

	union {
		struct vdso_timestamp	basetime[VDSO_BASES];
		struct timens_offset	offset[VDSO_BASES];
	};

	s32			tz_minuteswest;
	s32			tz_dsttime;
	u32			hrtimer_res;
	u32			__unused;

	struct arch_vdso_data	arch_data;
};

extern struct vdso_data _vdso_data[CS_BASES] __attribute__((visibility("hidden")));

#endif /* !__ASSEMBLY__ */

#endif /* __VDSO_DATAPAGE_H */
