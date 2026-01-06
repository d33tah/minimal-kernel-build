#ifndef __VDSO_HELPERS_H
#define __VDSO_HELPERS_H

#ifndef __ASSEMBLY__

#include <vdso/datapage.h>

/* vdso_read_begin removed - never called */

static __always_inline u32 vdso_read_retry(const struct vdso_data *vd,
					   u32 start)
{
	u32 seq;

	smp_rmb();
	seq = READ_ONCE(vd->seq);
	return seq != start;
}

/* vdso_write_begin, vdso_write_end removed - never called */

#endif  

#endif  
