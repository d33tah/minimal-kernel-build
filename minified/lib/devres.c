#include <linux/err.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/gfp.h>
#include <linux/export.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/of.h>

/* Inlined from of_address.h - stub implementations for !CONFIG_OF case */
struct of_bus;

struct of_pci_range_parser {
	struct device_node *node;
	struct of_bus *bus;
	const __be32 *range;
	const __be32 *end;
	int na;
	int ns;
	int pna;
	bool dma;
};
#define of_range_parser of_pci_range_parser

struct of_pci_range {
	union {
		u64 pci_addr;
		u64 bus_addr;
	};
	u64 cpu_addr;
	u64 size;
	u32 flags;
};
#define of_range of_pci_range

#define for_each_of_pci_range(parser, range) \
	for (; of_pci_range_parser_one(parser, range);)
#define for_each_of_range for_each_of_pci_range

static inline void __iomem *of_io_request_and_map(struct device_node *device,
						  int index, const char *name)
{
	return IOMEM_ERR_PTR(-EINVAL);
}

static inline u64 of_translate_address(struct device_node *np,
				       const __be32 *addr)
{
	return OF_BAD_ADDR;
}

static inline const __be32 *__of_get_address(struct device_node *dev, int index, int bar_no,
					     u64 *size, unsigned int *flags)
{
	return NULL;
}

static inline int of_pci_range_parser_init(struct of_pci_range_parser *parser,
			struct device_node *node)
{
	return -ENOSYS;
}

static inline int of_pci_dma_range_parser_init(struct of_pci_range_parser *parser,
			struct device_node *node)
{
	return -ENOSYS;
}

static inline struct of_pci_range *of_pci_range_parser_one(
					struct of_pci_range_parser *parser,
					struct of_pci_range *range)
{
	return NULL;
}

static inline int of_pci_address_to_resource(struct device_node *dev, int bar,
				             struct resource *r)
{
	return -ENOSYS;
}

static inline int of_pci_range_to_resource(struct of_pci_range *range,
					   struct device_node *np,
					   struct resource *res)
{
	return -ENOSYS;
}

static inline bool of_dma_is_coherent(struct device_node *np)
{
	return false;
}

static inline int of_address_to_resource(struct device_node *dev, int index,
					 struct resource *r)
{
	return -EINVAL;
}

static inline void __iomem *of_iomap(struct device_node *device, int index)
{
	return NULL;
}
#define of_range_parser_init of_pci_range_parser_init

static inline const __be32 *of_get_address(struct device_node *dev, int index,
					   u64 *size, unsigned int *flags)
{
	return __of_get_address(dev, index, -1, size, flags);
}

static inline const __be32 *of_get_pci_address(struct device_node *dev, int bar_no,
					       u64 *size, unsigned int *flags)
{
	return __of_get_address(dev, -1, bar_no, size, flags);
}
/* End of inlined of_address.h content */

enum devm_ioremap_type {
	DEVM_IOREMAP = 0,
	DEVM_IOREMAP_UC,
	DEVM_IOREMAP_WC,
	DEVM_IOREMAP_NP,
};

void devm_ioremap_release(struct device *dev, void *res)
{
	iounmap(*(void __iomem **)res);
}

/* devm_ioremap_match removed - unused */

static void __iomem *__devm_ioremap(struct device *dev, resource_size_t offset,
				    resource_size_t size,
				    enum devm_ioremap_type type)
{
	void __iomem **ptr, *addr = NULL;

	ptr = devres_alloc(devm_ioremap_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	switch (type) {
	case DEVM_IOREMAP:
		addr = ioremap(offset, size);
		break;
	case DEVM_IOREMAP_UC:
		addr = ioremap_uc(offset, size);
		break;
	case DEVM_IOREMAP_WC:
		addr = ioremap_wc(offset, size);
		break;
	case DEVM_IOREMAP_NP:
		addr = ioremap_np(offset, size);
		break;
	}

	if (addr) {
		*ptr = addr;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return addr;
}

void __iomem *devm_ioremap(struct device *dev, resource_size_t offset,
			   resource_size_t size)
{
	return __devm_ioremap(dev, offset, size, DEVM_IOREMAP);
}

/* Stub: devm_ioremap_uc not used in minimal kernel */
void __iomem *devm_ioremap_uc(struct device *dev, resource_size_t offset,
			      resource_size_t size)
{ return NULL; }

/* Stub: devm_ioremap_wc not used in minimal kernel */
void __iomem *devm_ioremap_wc(struct device *dev, resource_size_t offset,
			      resource_size_t size)
{ return NULL; }

/* Stub: devm_ioremap_np not used in minimal kernel */
void __iomem *devm_ioremap_np(struct device *dev, resource_size_t offset,
			      resource_size_t size)
{ return NULL; }

/* Stub: devm_iounmap not used in minimal kernel */
void devm_iounmap(struct device *dev, void __iomem *addr) { }

static void __iomem *
__devm_ioremap_resource(struct device *dev, const struct resource *res,
			enum devm_ioremap_type type)
{
	resource_size_t size;
	void __iomem *dest_ptr;
	char *pretty_name;

	BUG_ON(!dev);

	if (!res || resource_type(res) != IORESOURCE_MEM) {
		dev_err(dev, "invalid resource\n");
		return IOMEM_ERR_PTR(-EINVAL);
	}

	if (type == DEVM_IOREMAP && res->flags & IORESOURCE_MEM_NONPOSTED)
		type = DEVM_IOREMAP_NP;

	size = resource_size(res);

	if (res->name)
		pretty_name = devm_kasprintf(dev, GFP_KERNEL, "%s %s",
					     dev_name(dev), res->name);
	else
		pretty_name = devm_kstrdup(dev, dev_name(dev), GFP_KERNEL);
	if (!pretty_name) {
		dev_err(dev, "can't generate pretty name for resource %pR\n", res);
		return IOMEM_ERR_PTR(-ENOMEM);
	}

	if (!devm_request_mem_region(dev, res->start, size, pretty_name)) {
		dev_err(dev, "can't request region for resource %pR\n", res);
		return IOMEM_ERR_PTR(-EBUSY);
	}

	dest_ptr = __devm_ioremap(dev, res->start, size, type);
	if (!dest_ptr) {
		dev_err(dev, "ioremap failed for resource %pR\n", res);
		devm_release_mem_region(dev, res->start, size);
		dest_ptr = IOMEM_ERR_PTR(-ENOMEM);
	}

	return dest_ptr;
}

void __iomem *devm_ioremap_resource(struct device *dev,
				    const struct resource *res)
{
	return __devm_ioremap_resource(dev, res, DEVM_IOREMAP);
}

/* Stub: devm_ioremap_resource_wc not used in minimal kernel */
void __iomem *devm_ioremap_resource_wc(struct device *dev,
				       const struct resource *res)
{ return NULL; }

/* Stub: devm_of_iomap not used in minimal kernel */
void __iomem *devm_of_iomap(struct device *dev, struct device_node *node, int index,
			    resource_size_t *size)
{ return IOMEM_ERR_PTR(-EINVAL); }

/* Stub: devm_ioport_map not used in minimal kernel */
void __iomem *devm_ioport_map(struct device *dev, unsigned long port,
			       unsigned int nr)
{ return NULL; }

/* Stub: devm_ioport_unmap not used in minimal kernel */
void devm_ioport_unmap(struct device *dev, void __iomem *addr) { }

/* Stub: devm_arch_phys_wc_add not used in minimal kernel */
int devm_arch_phys_wc_add(struct device *dev, unsigned long base, unsigned long size)
{ return -ENODEV; }

/* Stub: devm_arch_io_reserve_memtype_wc not used in minimal kernel */
int devm_arch_io_reserve_memtype_wc(struct device *dev, resource_size_t start,
				    resource_size_t size)
{ return -ENODEV; }
