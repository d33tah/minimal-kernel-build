
#ifndef _LINUX_KOBJECT_NS_H
#define _LINUX_KOBJECT_NS_H

struct sock;
struct kobject;

enum kobj_ns_type {
	KOBJ_NS_TYPE_NONE = 0,
	KOBJ_NS_TYPE_NET,
	KOBJ_NS_TYPES
};

struct kobj_ns_type_operations {
	enum kobj_ns_type type;
	bool (*current_may_mount)(void);
	void *(*grab_current_ns)(void);
	const void *(*netlink_ns)(struct sock *sk);
	const void *(*initial_ns)(void);
	void (*drop_ns)(void *);
};

#endif
