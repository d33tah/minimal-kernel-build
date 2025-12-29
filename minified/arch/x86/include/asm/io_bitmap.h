 
#ifndef _ASM_X86_IOBITMAP_H
#define _ASM_X86_IOBITMAP_H

#include <linux/refcount.h>
#include <asm/processor.h>

struct io_bitmap {
	u64		sequence;
	refcount_t	refcnt;
	 
	unsigned int	max;
	unsigned long	bitmap[IO_BITMAP_LONGS];
};

/* io_bitmap_share, io_bitmap_exit call sites removed */
static inline void tss_update_io_bitmap(void) { }

#endif
