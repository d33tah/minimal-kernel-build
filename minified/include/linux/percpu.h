#ifndef __LINUX_PERCPU_H
#define __LINUX_PERCPU_H

#include <linux/mmdebug.h>
#include <linux/preempt.h>
#include <linux/smp.h>

#include <asm/percpu.h>

extern void __percpu *__alloc_percpu(size_t size, size_t align) __alloc_size(1);
extern phys_addr_t per_cpu_ptr_to_phys(void *addr);

#define alloc_percpu(type) \
	(typeof(type) __percpu *)__alloc_percpu(sizeof(type), __alignof__(type))

#endif
