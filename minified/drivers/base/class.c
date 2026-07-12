
#include <linux/device/class.h>
#include <linux/slab.h>
#include "base.h"

static struct kobj_type class_ktype = { };

static struct kset *class_kset;

int __class_register(struct class *cls, struct lock_class_key *key) {
	struct subsys_private *cp;
	int error;

	cp = kzalloc(sizeof(*cp), GFP_KERNEL);
	if (!cp)
		return -ENOMEM;
	error = kobject_set_name(&cp->subsys.kobj, "%s", cls->name);
	if (error) {
		kfree(cp);
		return error; }

	cp->subsys.kobj.kset = class_kset;
	cp->subsys.kobj.ktype = &class_ktype;

	error = kset_register(&cp->subsys);
	if (error) {
		kfree(cp);
		return error; }
	return 0; }

struct class *__class_create(struct module *owner, const char *name, struct lock_class_key *key) {
	struct class *cls;
	int retval;

	cls = kzalloc(sizeof(*cls), GFP_KERNEL);
	if (!cls) {
		retval = -ENOMEM;
		goto error; }

	cls->name = name;

	retval = __class_register(cls, key);
	if (retval)
		goto error;

	return cls;

error:
	kfree(cls);
	return ERR_PTR(retval); }


/* Removed: class_dev_iter_init/next/exit and class_find_device - the
   device-iteration helpers and class_find_device (sole caller was the now-removed
   tty_get_device chain) are all dead. */

/* Removed: class_interface_register - sole caller was the devlink class
 * registration, which has been removed. class_interface_unregister,
 * show_class_attr_string, class_compat_* were already removed - unused. */

int __init classes_init(void) {
	class_kset = kset_create_and_add("class", NULL);
	if (!class_kset)
		return -ENOMEM;
	return 0; }


