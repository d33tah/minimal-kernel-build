#ifndef __LINUX_CACHE_H
#define __LINUX_CACHE_H

#include <linux/const.h>
#include <asm/cache.h>

#ifndef L1_CACHE_ALIGN
#define L1_CACHE_ALIGN(x) __ALIGN_KERNEL(x, L1_CACHE_BYTES)
#endif

#ifndef SMP_CACHE_BYTES
#define SMP_CACHE_BYTES L1_CACHE_BYTES
#endif

#ifndef __read_mostly
#define __read_mostly
#endif

#ifndef __ro_after_init
#define __ro_after_init __section(".data..ro_after_init")
#endif

#ifndef ____cacheline_aligned
#define ____cacheline_aligned __attribute__((__aligned__(SMP_CACHE_BYTES)))
#endif

#ifndef ____cacheline_aligned_in_smp
#define ____cacheline_aligned_in_smp
#endif

#ifndef __cacheline_aligned
#define __cacheline_aligned					\
  __attribute__((__aligned__(SMP_CACHE_BYTES),			\
		 __section__(".data..cacheline_aligned")))
#endif  

#ifndef __cacheline_aligned_in_smp
#define __cacheline_aligned_in_smp
#endif

#ifndef INTERNODE_CACHE_SHIFT
#define INTERNODE_CACHE_SHIFT L1_CACHE_SHIFT
#endif

#if !defined(____cacheline_internodealigned_in_smp)
#define ____cacheline_internodealigned_in_smp
#endif


#endif  
