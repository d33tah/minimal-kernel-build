 
#include <linux/err.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/gfp.h>
#include <linux/export.h>
#include <linux/of_address.h>

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

static int devm_ioremap_match(struct device *dev, void *res, void *match_data)
{
	return *(void **)res == match_data;
}

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

 
void __iomem *devm_ioremap_uc(struct device *dev, resource_size_t offset,
			      resource_size_t size)
{
	return __devm_ioremap(dev, offset, size, DEVM_IOREMAP_UC);
}

 
void __iomem *devm_ioremap_wc(struct device *dev, resource_size_t offset,
			      resource_size_t size)
{
	return __devm_ioremap(dev, offset, size, DEVM_IOREMAP_WC);
}

 
void __iomem *devm_ioremap_np(struct device *dev, resource_size_t offset,
			      resource_size_t size)
{
	return __devm_ioremap(dev, offset, size, DEVM_IOREMAP_NP);
}

 
void devm_iounmap(struct device *dev, void __iomem *addr)
{
	WARN_ON(devres_destroy(dev, devm_ioremap_release, devm_ioremap_match,
			       (__force void *)addr));
	iounmap(addr);
}

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

 
void __iomem *devm_ioremap_resource_wc(struct device *dev,
				       const struct resource *res)
{
	return __devm_ioremap_resource(dev, res, DEVM_IOREMAP_WC);
}

 
void __iomem *devm_of_iomap(struct device *dev, struct device_node *node, int index,
			    resource_size_t *size)
{
	struct resource res;

	if (of_address_to_resource(node, index, &res))
		return IOMEM_ERR_PTR(-EINVAL);
	if (size)
		*size = resource_size(&res);
	return devm_ioremap_resource(dev, &res);
}

 
static void devm_ioport_map_release(struct device *dev, void *res)
{
	ioport_unmap(*(void __iomem **)res);
}

static int devm_ioport_map_match(struct device *dev, void *res,
				 void *match_data)
{
	return *(void **)res == match_data;
}

 
void __iomem *devm_ioport_map(struct device *dev, unsigned long port,
			       unsigned int nr)
{
	void __iomem **ptr, *addr;

	ptr = devres_alloc(devm_ioport_map_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	addr = ioport_map(port, nr);
	if (addr) {
		*ptr = addr;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return addr;
}

 
void devm_ioport_unmap(struct device *dev, void __iomem *addr)
{
	ioport_unmap(addr);
	WARN_ON(devres_destroy(dev, devm_ioport_map_release,
			       devm_ioport_map_match, (__force void *)addr));
}


static void devm_arch_phys_ac_add_release(struct device *dev, void *res)
{
	arch_phys_wc_del(*((int *)res));
}

 
int devm_arch_phys_wc_add(struct device *dev, unsigned long base, unsigned long size)
{
	int *mtrr;
	int ret;

	mtrr = devres_alloc(devm_arch_phys_ac_add_release, sizeof(*mtrr), GFP_KERNEL);
	if (!mtrr)
		return -ENOMEM;

	ret = arch_phys_wc_add(base, size);
	if (ret < 0) {
		devres_free(mtrr);
		return ret;
	}

	*mtrr = ret;
	devres_add(dev, mtrr);

	return ret;
}

struct arch_io_reserve_memtype_wc_devres {
	resource_size_t start;
	resource_size_t size;
};

static void devm_arch_io_free_memtype_wc_release(struct device *dev, void *res)
{
	const struct arch_io_reserve_memtype_wc_devres *this = res;

	arch_io_free_memtype_wc(this->start, this->size);
}

 
int devm_arch_io_reserve_memtype_wc(struct device *dev, resource_size_t start,
				    resource_size_t size)
{
	struct arch_io_reserve_memtype_wc_devres *dr;
	int ret;

	dr = devres_alloc(devm_arch_io_free_memtype_wc_release, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	ret = arch_io_reserve_memtype_wc(start, size);
	if (ret < 0) {
		devres_free(dr);
		return ret;
	}

	dr->start = start;
	dr->size = size;
	devres_add(dev, dr);

	return ret;
}
