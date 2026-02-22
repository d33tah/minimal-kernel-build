/* Simplified kobject - no kset/uevent/sysfs infrastructure needed */
#include <linux/kobject.h>
#include <linux/slab.h>

void kobject_init(struct kobject *kobj, const struct kobj_type *ktype)
{
	if (!kobj || !ktype)
		return;
	kref_init(&kobj->kref);
	INIT_LIST_HEAD(&kobj->entry);
	kobj->state_initialized = 1;
	kobj->ktype = ktype;
}

struct kobject *kobject_get(struct kobject *kobj)
{
	if (kobj)
		kref_get(&kobj->kref);
	return kobj;
}

struct kobject *__must_check kobject_get_unless_zero(struct kobject *kobj)
{
	if (!kobj)
		return NULL;
	if (!kref_get_unless_zero(&kobj->kref))
		kobj = NULL;
	return kobj;
}

static void kobject_release(struct kref *kref)
{
	struct kobject *kobj = container_of(kref, struct kobject, kref);
	const struct kobj_type *t = get_ktype(kobj);

	if (t && t->release)
		t->release(kobj);
	if (kobj->name)
		kfree_const(kobj->name);
}

void kobject_put(struct kobject *kobj)
{
	if (kobj)
		kref_put(&kobj->kref, kobject_release);
}

const struct sysfs_ops kobj_sysfs_ops = {};

struct kset *kset_create_and_add(const char *name, struct kobject *parent_kobj)
{
	return NULL;
}

int kset_register(struct kset *k)
{
	return 0;
}
