/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_CACHE_H
#define __LINUX_CACHE_H

#include <asm/cache.h>

#define SMP_CACHE_BYTES L1_CACHE_BYTES

#define __ro_after_init __section(".data..ro_after_init")

#define ____cacheline_aligned __attribute__((__aligned__(SMP_CACHE_BYTES)))

#define ____cacheline_aligned_in_smp

#define __cacheline_aligned_in_smp

#define ____cacheline_internodealigned_in_smp

#endif
