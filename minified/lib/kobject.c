
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/export.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/random.h>

/*
 * The kset membership list (kset->list / kobj->entry) was never iterated
 * anywhere tree-wide, so join/leave reduce to the kset refcount get/put.
 */
static void kobj_kset_join(struct kobject *kobj)
{
	if (!kobj->kset)
		return;

	kset_get(kobj->kset);
}

static void kobj_kset_leave(struct kobject *kobj)
{
	if (!kobj->kset)
		return;

	kset_put(kobj->kset);
}

static void kobject_init_internal(struct kobject *kobj)
{
	if (!kobj)
		return;
	kref_init(&kobj->kref);
	kobj->state_in_sysfs = 0;
	kobj->state_initialized = 1;
}


static int kobject_add_internal(struct kobject *kobj)
{
	struct kobject *parent;

	if (!kobj)
		return -ENOENT;

	if (!kobj->name || !kobj->name[0]) {
		WARN(1,
		     "kobject: (%p): attempted to be registered with empty name!\n",
		     kobj);
		return -EINVAL;
	}

	parent = kobject_get(kobj->parent);


	if (kobj->kset) {
		if (!parent)
			parent = kobject_get(&kobj->kset->kobj);
		kobj_kset_join(kobj);
		kobj->parent = parent;
	}

	kobj->state_in_sysfs = 1;

	return 0;
}

int kobject_set_name_vargs(struct kobject *kobj, const char *fmt,
				  va_list vargs)
{
	const char *s;

	if (kobj->name && !fmt)
		return 0;

	s = kvasprintf_const(GFP_KERNEL, fmt, vargs);
	if (!s)
		return -ENOMEM;

	 
	if (strchr(s, '/')) {
		char *t;

		t = kstrdup(s, GFP_KERNEL);
		kfree_const(s);
		if (!t)
			return -ENOMEM;
		strreplace(t, '/', '!');
		s = t;
	}
	kfree_const(kobj->name);
	kobj->name = s;

	return 0;
}

int kobject_set_name(struct kobject *kobj, const char *fmt, ...)
{
	va_list vargs;
	int retval;

	va_start(vargs, fmt);
	retval = kobject_set_name_vargs(kobj, fmt, vargs);
	va_end(vargs);

	return retval;
}

void kobject_init(struct kobject *kobj, const struct kobj_type *ktype)
{
	char *err_str;

	if (!kobj) {
		err_str = "invalid kobject pointer!";
		goto error;
	}
	if (!ktype) {
		err_str = "must have a ktype to be initialized properly!\n";
		goto error;
	}
	if (kobj->state_initialized) {
		 
		pr_err("kobject (%p): tried to init an initialized object, something is seriously wrong.\n",
		       kobj);
		dump_stack();
	}

	kobject_init_internal(kobj);
	kobj->ktype = ktype;
	return;

error:
	pr_err("kobject (%p): %s\n", kobj, err_str);
	dump_stack();
}

static __printf(3, 0) int kobject_add_varg(struct kobject *kobj,
					   struct kobject *parent,
					   const char *fmt, va_list vargs)
{
	int retval;

	retval = kobject_set_name_vargs(kobj, fmt, vargs);
	if (retval) {
		pr_err("kobject: can not set name properly!\n");
		return retval;
	}
	kobj->parent = parent;
	return kobject_add_internal(kobj);
}

int kobject_add(struct kobject *kobj, struct kobject *parent,
		const char *fmt, ...)
{
	va_list args;
	int retval;

	if (!kobj)
		return -EINVAL;

	if (!kobj->state_initialized) {
		pr_err("kobject '%s' (%p): tried to add an uninitialized object, something is seriously wrong.\n",
		       kobject_name(kobj), kobj);
		dump_stack();
		return -EINVAL;
	}
	va_start(args, fmt);
	retval = kobject_add_varg(kobj, parent, fmt, args);
	va_end(args);

	return retval;
}

/* Simplified: sysfs functions are already stubs */
static void __kobject_del(struct kobject *kobj)
{
	kobj->state_in_sysfs = 0;
	kobj_kset_leave(kobj);
	kobj->parent = NULL;
}

/* Removed: kobject_del - only caller was kset_unregister (also removed). */

struct kobject *kobject_get(struct kobject *kobj)
{
	if (kobj) {
		if (!kobj->state_initialized)
			WARN(1, KERN_WARNING
				"kobject: '%s' (%p): is not initialized, yet kobject_get() is being called.\n",
			     kobject_name(kobj), kobj);
		kref_get(&kobj->kref);
	}
	return kobj;
}

struct kobject * __must_check kobject_get_unless_zero(struct kobject *kobj)
{
	if (!kobj)
		return NULL;
	if (!kref_get_unless_zero(&kobj->kref))
		kobj = NULL;
	return kobj;
}

static void kobject_cleanup(struct kobject *kobj)
{
	struct kobject *parent = kobj->parent;
	const struct kobj_type *t = get_ktype(kobj);
	const char *name = kobj->name;

	 
	if (kobj->state_in_sysfs) {
		__kobject_del(kobj);
	} else {
		 
		parent = NULL;
	}

	if (t && t->release) {
		t->release(kobj);
	}

	 
	if (name) {
		kfree_const(name);
	}

	kobject_put(parent);
}


static void kobject_release(struct kref *kref)
{
	struct kobject *kobj = container_of(kref, struct kobject, kref);
	kobject_cleanup(kobj);
}

void kobject_put(struct kobject *kobj)
{
	if (kobj) {
		if (!kobj->state_initialized)
			WARN(1, KERN_WARNING
				"kobject: '%s' (%p): is not initialized, yet kobject_put() is being called.\n",
			     kobject_name(kobj), kobj);
		kref_put(&kobj->kref, kobject_release);
	}
}

static void dynamic_kobj_release(struct kobject *kobj)
{
	kfree(kobj);
}

static struct kobj_type dynamic_kobj_ktype = {
	.release	= dynamic_kobj_release,
};

static struct kobject *kobject_create(void)
{
	struct kobject *kobj;

	kobj = kzalloc(sizeof(*kobj), GFP_KERNEL);
	if (!kobj)
		return NULL;

	kobject_init(kobj, &dynamic_kobj_ktype);
	return kobj;
}

struct kobject *kobject_create_and_add(const char *name, struct kobject *parent)
{
	struct kobject *kobj;
	int retval;

	kobj = kobject_create();
	if (!kobj)
		return NULL;

	retval = kobject_add(kobj, parent, "%s", name);
	if (retval) {
		pr_warn("%s: kobject_add error: %d\n", __func__, retval);
		kobject_put(kobj);
		kobj = NULL;
	}
	return kobj;
}

int kset_register(struct kset *k)
{
	int err;

	if (!k)
		return -EINVAL;

	kobject_init_internal(&k->kobj);
	err = kobject_add_internal(&k->kobj);
	if (err)
		return err;
	return 0;
}

/* Removed: kset_unregister - only caller was a runtime-dead devices_init()
   error-cleanup branch (boot never hits the alloc-failure path). */

static void kset_release(struct kobject *kobj)
{
	struct kset *kset = container_of(kobj, struct kset, kobj);
	kfree(kset);
}

static struct kobj_type kset_ktype = {
	.release	= kset_release,
};

static struct kset *kset_create(const char *name,
				struct kobject *parent_kobj)
{
	struct kset *kset;
	int retval;

	kset = kzalloc(sizeof(*kset), GFP_KERNEL);
	if (!kset)
		return NULL;
	retval = kobject_set_name(&kset->kobj, "%s", name);
	if (retval) {
		kfree(kset);
		return NULL;
	}
	kset->kobj.parent = parent_kobj;

	 
	kset->kobj.ktype = &kset_ktype;
	kset->kobj.kset = NULL;

	return kset;
}

struct kset *kset_create_and_add(const char *name,
				 struct kobject *parent_kobj)
{
	struct kset *kset;
	int error;

	kset = kset_create(name, parent_kobj);
	if (!kset)
		return NULL;
	error = kset_register(kset);
	if (error) {
		kfree(kset);
		return NULL;
	}
	return kset;
}
