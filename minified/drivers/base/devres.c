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
char *devm_kstrdup(struct device *dev, const char *s, gfp_t gfp)
{
	return NULL;
}
char *devm_kasprintf(struct device *dev, gfp_t gfp, const char *fmt, ...)
{
	return NULL;
}
