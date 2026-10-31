#ifndef __LINUX_PERCPU_H
#define __LINUX_PERCPU_H

#include <linux/preempt.h>
#include <linux/smp.h>

extern void __percpu *__alloc_percpu(size_t size, size_t align) __alloc_size(1);

#endif
