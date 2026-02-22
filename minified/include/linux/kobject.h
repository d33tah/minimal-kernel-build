
#ifndef _KOBJECT_H_
#define _KOBJECT_H_

#include <linux/sysfs.h>
#include <linux/kref.h>
#include <linux/wait.h>
#include <linux/uidgid.h>

struct kobject {
	const char		*name;
	struct list_head	entry;
	struct kobject		*parent;
	struct kset		*kset;
	const struct kobj_type	*ktype;
	struct kernfs_node	*sd;  
	struct kref		kref;
	unsigned int state_initialized:1;
	unsigned int state_in_sysfs:1;
	unsigned int state_add_uevent_sent:1;
	unsigned int state_remove_uevent_sent:1;
};

extern struct kobject * __must_check kobject_get_unless_zero(
						struct kobject *kobj);
extern void kobject_put(struct kobject *kobj);

/* kobject_get_ownership now static in kobject.c */

struct kobj_type {
	void (*release)(struct kobject *kobj);
};

struct kset {
	struct list_head list;
	spinlock_t list_lock;
	struct kobject kobj;
} __randomize_layout;

static inline const struct kobj_type *get_ktype(struct kobject *kobj)
{
	return kobj->ktype;
}

#endif  
