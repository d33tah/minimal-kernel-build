// SPDX-License-Identifier: GPL-2.0
/*
 * DMA channel allocator - Stubbed minimal implementation
 */
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/spinlock.h>

DEFINE_SPINLOCK(dma_spin_lock);
EXPORT_SYMBOL(dma_spin_lock);

int request_dma(unsigned int dmanr, const char *device_id) { return -EINVAL; }
EXPORT_SYMBOL(request_dma);

void free_dma(unsigned int dmanr) { }
EXPORT_SYMBOL(free_dma);
