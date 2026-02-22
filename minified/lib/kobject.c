/* Simplified kobject - no kset/uevent/sysfs infrastructure needed */
#include <linux/kobject.h>
#include <linux/slab.h>

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
