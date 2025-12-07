 
#ifndef __ASM_VDSO_VSYSCALL_H
#define __ASM_VDSO_VSYSCALL_H

#ifndef __ASSEMBLY__

#include <linux/hrtimer.h>
#include <linux/timekeeper_internal.h>
#include <vdso/datapage.h>
#include <asm/vgtod.h>
#include <asm/vvar.h>

DEFINE_VVAR(struct vdso_data, _vdso_data);
 
static __always_inline
struct vdso_data *__x86_get_k_vdso_data(void)
{
	return _vdso_data;
}
#define __arch_get_k_vdso_data __x86_get_k_vdso_data

/* Inlined from asm-generic/vdso/vsyscall.h */
#ifndef __arch_get_k_vdso_data
static __always_inline struct vdso_data *__arch_get_k_vdso_data(void)
{
	return NULL;
}
#endif

#ifndef __arch_update_vsyscall
static __always_inline void __arch_update_vsyscall(struct vdso_data *vdata,
						   struct timekeeper *tk)
{
}
#endif

#ifndef __arch_sync_vdso_data
static __always_inline void __arch_sync_vdso_data(struct vdso_data *vdata)
{
}
#endif
/* End of inlined asm-generic/vdso/vsyscall.h */

#endif

#endif  
