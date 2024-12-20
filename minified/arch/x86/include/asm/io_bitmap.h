/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IOBITMAP_H
#define _ASM_X86_IOBITMAP_H

#include <linux/refcount.h>
#include <asm/processor.h>

struct io_bitmap {
	u64		sequence;
	refcount_t	refcnt;
	/* The maximum number of bytes to copy so all zero bits are covered */
	unsigned int	max;
	unsigned long	bitmap[IO_BITMAP_LONGS];
};

struct task_struct;

static inline void io_bitmap_share(struct task_struct *tsk) { }
static inline void io_bitmap_exit(struct task_struct *tsk) { }
static inline void tss_update_io_bitmap(void) { }

#endif
