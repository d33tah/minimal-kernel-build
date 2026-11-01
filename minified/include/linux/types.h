/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TYPES_H
#define _LINUX_TYPES_H

#include <uapi/linux/types.h>

#ifndef __ASSEMBLY__

#define DECLARE_BITMAP(name, bits) unsigned long name[BITS_TO_LONGS(bits)]

typedef u32 __kernel_dev_t;

typedef __kernel_dev_t dev_t;
typedef unsigned short umode_t;
typedef __kernel_pid_t pid_t;

typedef _Bool bool;

typedef __kernel_uid32_t uid_t;
typedef __kernel_gid32_t gid_t;

typedef unsigned long uintptr_t;

typedef __kernel_loff_t loff_t;

typedef __kernel_size_t size_t;

typedef __kernel_ssize_t ssize_t;

typedef u8 uint8_t;
typedef u16 uint16_t;
typedef u32 uint32_t;

/*
 * The type of an index into the pagecache.
 */
#define pgoff_t unsigned long

typedef unsigned int __bitwise gfp_t;
typedef unsigned int __bitwise slab_flags_t;
typedef unsigned int __bitwise fmode_t;

typedef u32 phys_addr_t;

typedef phys_addr_t resource_size_t;

typedef struct {
	int counter;
} atomic_t;

#define ATOMIC_INIT(i) { (i) }

struct list_head {
	struct list_head *next, *prev;
};

struct hlist_node {
	struct hlist_node *next, **pprev;
};

/**
 * struct callback_head - callback structure for use with RCU and task_work
 * @next: next update requests in a list
 * @func: actual update function to call after the grace period.
 *
 * The struct is aligned to size of pointer. On most architectures it happens
 * naturally due ABI requirements, but some architectures (like CRIS) have
 * weird ABI and we need to ask it explicitly.
 *
 * The alignment is required to guarantee that bit 0 of @next will be
 * clear under normal conditions -- as long as we use call_rcu() or
 * call_srcu() to queue the callback.
 *
 * This guarantee is important for few reasons:
 *  - future call_rcu_lazy() will make use of lower bits in the pointer;
 *  - the structure shares storage space in struct page with @compound_head,
 *    which encode PageTail() in bit 0. The guarantee is needed to avoid
 *    false-positive PageTail().
 */
struct callback_head {
	struct callback_head *next;
	void (*func)(struct callback_head *head);
} __attribute__((aligned(sizeof(void *))));
#define rcu_head callback_head

#endif /*  __ASSEMBLY__ */
#endif /* _LINUX_TYPES_H */
