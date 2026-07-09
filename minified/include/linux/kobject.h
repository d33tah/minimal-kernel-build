
#ifndef _KOBJECT_H_
#define _KOBJECT_H_

#include <linux/types.h>
#include <linux/list.h>
#include <linux/container_of.h>
#include <linux/spinlock.h>
#include <linux/kref.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>
#include <linux/uidgid.h>

struct kobject {
	const char		*name;
	struct kobject		*parent;
	struct kset		*kset;
	const struct kobj_type	*ktype;
	struct kref		kref;
	unsigned int state_initialized:1;
	unsigned int state_in_sysfs:1;
};

extern __printf(2, 3)
int kobject_set_name(struct kobject *kobj, const char *name, ...);
extern __printf(2, 0)
int kobject_set_name_vargs(struct kobject *kobj, const char *fmt,
			   va_list vargs);

static inline const char *kobject_name(const struct kobject *kobj)
{
	return kobj->name;
}

extern void kobject_init(struct kobject *kobj, const struct kobj_type *ktype);
extern __printf(3, 4) __must_check
int kobject_add(struct kobject *kobj, struct kobject *parent,
		const char *fmt, ...);

extern struct kobject * __must_check kobject_create_and_add(const char *name,
						struct kobject *parent);

extern struct kobject *kobject_get(struct kobject *kobj);
extern struct kobject * __must_check kobject_get_unless_zero(
						struct kobject *kobj);
extern void kobject_put(struct kobject *kobj);

struct kobj_type {
	void (*release)(struct kobject *kobj);
};

/* struct kset_uevent_ops removed - uevent_ops never dispatched (all NULL) */

struct kset {
	struct kobject kobj;
} __randomize_layout;

extern int __must_check kset_register(struct kset *kset);
extern struct kset * __must_check kset_create_and_add(const char *name,
						struct kobject *parent_kobj);

static inline struct kset *to_kset(struct kobject *kobj)
{
	return kobj ? container_of(kobj, struct kset, kobj) : NULL;
}

static inline struct kset *kset_get(struct kset *k)
{
	return k ? to_kset(kobject_get(&k->kobj)) : NULL;
}

static inline void kset_put(struct kset *k)
{
	kobject_put(&k->kobj);
}

static inline const struct kobj_type *get_ktype(struct kobject *kobj)
{
	return kobj->ktype;
}

#endif
