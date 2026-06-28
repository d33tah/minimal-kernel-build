#ifndef _LINUX_ATOMIC_H
#define _LINUX_ATOMIC_H
#include <linux/types.h>

#include <asm/atomic.h>
#include <asm/barrier.h>

#ifndef __atomic_acquire_fence
#define __atomic_acquire_fence		smp_mb__after_atomic
#endif

#ifndef __atomic_release_fence
#define __atomic_release_fence		smp_mb__before_atomic
#endif

#ifndef __atomic_pre_full_fence
#define __atomic_pre_full_fence		smp_mb__before_atomic
#endif

#ifndef __atomic_post_full_fence
#define __atomic_post_full_fence	smp_mb__after_atomic
#endif

#include <linux/atomic/atomic-arch-fallback.h>
#include <linux/atomic/atomic-long.h>
#include <linux/atomic/atomic-instrumented.h>

#endif  
