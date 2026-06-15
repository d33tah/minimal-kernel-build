/* Minimal swiotlb.h - SWIOTLB not used in this kernel */
#ifndef __LINUX_SWIOTLB_H
#define __LINUX_SWIOTLB_H

#include <linux/types.h>
#include <linux/limits.h>

static inline void swiotlb_init(bool addressing_limited, unsigned int flags)
{
}

#endif /* __LINUX_SWIOTLB_H */
