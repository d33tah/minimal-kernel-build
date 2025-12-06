
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/percpu.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/gfp.h>

typedef void (*dr_release_t)(struct device *dev, void *res);
typedef int (*dr_match_t)(struct device *dev, void *res, void *match_data);

void *__devres_alloc_node(dr_release_t release, size_t size, gfp_t gfp, int nid,
			  const char *name)
{
	return NULL;
}

void devres_for_each_res(struct device *dev, dr_release_t release,
			 dr_match_t match, void *match_data,
			 void (*fn)(struct device *, void *, void *),
			 void *data)
{
}

void devres_free(void *res)
{
}

void devres_add(struct device *dev, void *res)
{
}

void *devres_find(struct device *dev, dr_release_t release,
		  dr_match_t match, void *match_data)
{
	return NULL;
}

void *devres_get(struct device *dev, void *new_res,
		 dr_match_t match, void *match_data)
{
	return NULL;
}

void *devres_remove(struct device *dev, dr_release_t release,
		    dr_match_t match, void *match_data)
{
	return NULL;
}

int devres_destroy(struct device *dev, dr_release_t release,
		   dr_match_t match, void *match_data)
{
	return 0;
}

int devres_release(struct device *dev, dr_release_t release,
		   dr_match_t match, void *match_data)
{
	return 0;
}

int devres_release_all(struct device *dev)
{
	return 0;
}

void *devres_open_group(struct device *dev, void *id, gfp_t gfp)
{
	return NULL;
}

void devres_close_group(struct device *dev, void *id)
{
}

void devres_remove_group(struct device *dev, void *id)
{
}

int devres_release_group(struct device *dev, void *id)
{
	return 0;
}

void *devm_kmalloc(struct device *dev, size_t size, gfp_t gfp)
{
	return kmalloc(size, gfp);
}

void devm_kfree(struct device *dev, const void *p)
{
	kfree(p);
}

char *devm_kstrdup(struct device *dev, const char *s, gfp_t gfp)
{
	return kstrdup(s, gfp);
}

char *devm_kvasprintf(struct device *dev, gfp_t gfp, const char *fmt,
		      va_list ap)
{
	return kvasprintf(gfp, fmt, ap);
}

char *devm_kasprintf(struct device *dev, gfp_t gfp, const char *fmt, ...)
{
	va_list ap;
	char *p;

	va_start(ap, fmt);
	p = devm_kvasprintf(dev, gfp, fmt, ap);
	va_end(ap);

	return p;
}

const char *devm_kstrdup_const(struct device *dev, const char *s, gfp_t gfp)
{
	return kstrdup(s, gfp);
}

void *devm_kmemdup(struct device *dev, const void *src, size_t len, gfp_t gfp)
{
	void *p = kmalloc(len, gfp);
	if (p)
		memcpy(p, src, len);
	return p;
}

unsigned long devm_get_free_pages(struct device *dev,
				  gfp_t gfp_mask, unsigned int order)
{
	return __get_free_pages(gfp_mask, order);
}

void devm_free_pages(struct device *dev, unsigned long addr)
{
	free_pages(addr, 0);
}

void *devm_krealloc(struct device *dev, void *ptr, size_t new_size, gfp_t gfp)
{
	return krealloc(ptr, new_size, gfp);
}
