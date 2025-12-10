/* STUB: I/O port mapping functions - return safe defaults */

#include <linux/pci.h>
#include <linux/io.h>
#include <linux/export.h>

/* Read functions - return all-bits-set (safe error values) */
unsigned int ioread8(const void __iomem *addr) { return 0xff; }
unsigned int ioread16(const void __iomem *addr) { return 0xffff; }
unsigned int ioread16be(const void __iomem *addr) { return 0xffff; }
unsigned int ioread32(const void __iomem *addr) { return 0xffffffff; }
unsigned int ioread32be(const void __iomem *addr) { return 0xffffffff; }
u64 ioread64_lo_hi(const void __iomem *addr) { return 0xffffffffffffffffULL; }
u64 ioread64_hi_lo(const void __iomem *addr) { return 0xffffffffffffffffULL; }
u64 ioread64be_lo_hi(const void __iomem *addr) { return 0xffffffffffffffffULL; }
u64 ioread64be_hi_lo(const void __iomem *addr) { return 0xffffffffffffffffULL; }

/* Write functions - no-ops */
void iowrite8(u8 val, void __iomem *addr) { }
void iowrite16(u16 val, void __iomem *addr) { }
void iowrite16be(u16 val, void __iomem *addr) { }
void iowrite32(u32 val, void __iomem *addr) { }
void iowrite32be(u32 val, void __iomem *addr) { }
void iowrite64_lo_hi(u64 val, void __iomem *addr) { }
void iowrite64_hi_lo(u64 val, void __iomem *addr) { }
void iowrite64be_lo_hi(u64 val, void __iomem *addr) { }
void iowrite64be_hi_lo(u64 val, void __iomem *addr) { }
