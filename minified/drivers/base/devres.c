
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

/* devres_for_each_res, devres_find, devres_get, devres_remove,
   devres_destroy, devres_release removed - never called */

void devres_free(void *res)
{
}

void devres_add(struct device *dev, void *res)
{
}

int devres_release_all(struct device *dev)
{
	return 0;
}

/* devm_kstrdup, devm_kasprintf, devm_kmalloc, devm_kfree, devm_kvasprintf,
   devm_kstrdup_const, devm_kmemdup, devm_get_free_pages, devm_free_pages,
   devm_krealloc removed - never called in minimal kernel */
char *devm_kstrdup(struct device *dev, const char *s, gfp_t gfp)
{
	return NULL;
}
char *devm_kasprintf(struct device *dev, gfp_t gfp, const char *fmt, ...)
{
	return NULL;
}
