#ifndef _LINUX_KFENCE_H
#define _LINUX_KFENCE_H
#include <linux/mm.h>
#include <linux/types.h>
static inline bool is_kfence_address(const void *addr) { return false; }
static inline void kfence_alloc_pool(void) { }
static inline void kfence_init(void) { }
#endif  
