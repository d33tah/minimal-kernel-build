#include <linux/err.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/gfp.h>
#include <linux/export.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/of.h>

/* OF (Open Firmware) stubs removed - unused in minimal kernel */

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

/* devm_ioremap_uc, devm_ioremap_wc, devm_ioremap_np, devm_iounmap removed - unused */

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
