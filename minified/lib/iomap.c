/* STUB: I/O port mapping functions - return safe defaults */

#include <linux/pci.h>
#include <linux/io.h>
#include <linux/export.h>

/* Read functions - return all-bits-set (safe error values) */
unsigned int ioread8(const void __iomem *addr) { return 0xff; }
unsigned int ioread16(const void __iomem *addr) { return 0xffff; }
unsigned int ioread32(const void __iomem *addr) { return 0xffffffff; }
/* ioread16be, ioread32be, ioread64* removed - unused */

/* Write functions - no-ops */
void iowrite8(u8 val, void __iomem *addr) { }
void iowrite16(u16 val, void __iomem *addr) { }
void iowrite32(u32 val, void __iomem *addr) { }
/* iowrite16be, iowrite32be, iowrite64* removed - unused */
