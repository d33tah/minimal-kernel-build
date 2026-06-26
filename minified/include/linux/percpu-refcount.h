
#ifndef _LINUX_PERCPU_REFCOUNT_H
#define _LINUX_PERCPU_REFCOUNT_H

/* Whole percpu-refcount API removed - 0 external refs tree-wide (struct
 * percpu_ref / percpu_ref_data, percpu_ref_put*, __ref_is_percpu, the
 * __PERCPU_REF_*/PERCPU_REF_* enums and percpu_ref_func_t were all unused).
 * The two former includers (mm.h, memremap.h) dropped their #include. Guard
 * kept harmless. */

#endif
