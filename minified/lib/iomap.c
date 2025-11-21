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

/* Repeated I/O operations - no-ops */
void ioread8_rep(const void __iomem *addr, void *dst, unsigned long count) { }
void ioread16_rep(const void __iomem *addr, void *dst, unsigned long count) { }
void ioread32_rep(const void __iomem *addr, void *dst, unsigned long count) { }
void iowrite8_rep(void __iomem *addr, const void *src, unsigned long count) { }
void iowrite16_rep(void __iomem *addr, const void *src, unsigned long count) { }
void iowrite32_rep(void __iomem *addr, const void *src, unsigned long count) { }

/* PCI I/O mapping - return NULL */
void __iomem *ioport_map(unsigned long port, unsigned int nr) { return NULL; }
void ioport_unmap(void __iomem *addr) { }

/* PCI device mapping - return NULL/no-op */
void __iomem *pci_iomap_range(struct pci_dev *dev, int bar, unsigned long offset,
			      unsigned long maxlen) { return NULL; }
void __iomem *pci_iomap_wc_range(struct pci_dev *dev, int bar, unsigned long offset,
				 unsigned long maxlen) { return NULL; }
void __iomem *pci_iomap_wc(struct pci_dev *dev, int bar, unsigned long maxlen) { return NULL; }
/* Note: pci_iomap and pci_iounmap already defined as static inline in pci.h */

EXPORT_SYMBOL(ioread8);
EXPORT_SYMBOL(ioread16);
EXPORT_SYMBOL(ioread16be);
EXPORT_SYMBOL(ioread32);
EXPORT_SYMBOL(ioread32be);
EXPORT_SYMBOL(ioread64_lo_hi);
EXPORT_SYMBOL(ioread64_hi_lo);
EXPORT_SYMBOL(ioread64be_lo_hi);
EXPORT_SYMBOL(ioread64be_hi_lo);
EXPORT_SYMBOL(iowrite8);
EXPORT_SYMBOL(iowrite16);
EXPORT_SYMBOL(iowrite16be);
EXPORT_SYMBOL(iowrite32);
EXPORT_SYMBOL(iowrite32be);
EXPORT_SYMBOL(iowrite64_lo_hi);
EXPORT_SYMBOL(iowrite64_hi_lo);
EXPORT_SYMBOL(iowrite64be_lo_hi);
EXPORT_SYMBOL(iowrite64be_hi_lo);
EXPORT_SYMBOL(ioread8_rep);
EXPORT_SYMBOL(ioread16_rep);
EXPORT_SYMBOL(ioread32_rep);
EXPORT_SYMBOL(iowrite8_rep);
EXPORT_SYMBOL(iowrite16_rep);
EXPORT_SYMBOL(iowrite32_rep);
EXPORT_SYMBOL(ioport_map);
EXPORT_SYMBOL(ioport_unmap);
EXPORT_SYMBOL(pci_iomap_range);
EXPORT_SYMBOL(pci_iomap_wc_range);
EXPORT_SYMBOL(pci_iomap_wc);
