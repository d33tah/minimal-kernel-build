/* Minimal mmiotrace.h - MMIO tracing stubs */
#ifndef _LINUX_MMIOTRACE_H
#define _LINUX_MMIOTRACE_H

#include <linux/types.h>

struct pt_regs;

static inline int is_kmmio_active(void) { return 0; }
static inline int kmmio_handler(struct pt_regs *regs, unsigned long addr) { return 0; }
static inline void mmiotrace_ioremap(resource_size_t off, unsigned long size, void __iomem *addr) { }
static inline void mmiotrace_iounmap(volatile void __iomem *addr) { }

#endif
