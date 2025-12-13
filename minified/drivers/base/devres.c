
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

/* devres_open_group, devres_close_group, devres_remove_group, devres_release_group removed - unused */

char *devm_kstrdup(struct device *dev, const char *s, gfp_t gfp)
{
	return kstrdup(s, gfp);
}

char *devm_kasprintf(struct device *dev, gfp_t gfp, const char *fmt, ...)
{
	va_list ap;
	char *p;

	va_start(ap, fmt);
	p = kvasprintf(gfp, fmt, ap);
	va_end(ap);

	return p;
}

/* devm_kmalloc, devm_kfree, devm_kvasprintf, devm_kstrdup_const, devm_kmemdup,
   devm_get_free_pages, devm_free_pages, devm_krealloc removed - not called */
