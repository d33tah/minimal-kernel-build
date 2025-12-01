#include <linux/kobject.h>
#include <linux/export.h>

u64 uevent_seqnum;

int kobject_synth_uevent(struct kobject *kobj, const char *buf, size_t count)
{
	return 0;
}

int kobject_uevent_env(struct kobject *kobj, enum kobject_action action,
		       char *envp_ext[])
{
	 
	if (action == KOBJ_REMOVE)
		kobj->state_remove_uevent_sent = 1;
	if (action == KOBJ_ADD)
		kobj->state_add_uevent_sent = 1;
	return 0;
}

int kobject_uevent(struct kobject *kobj, enum kobject_action action)
{
	return kobject_uevent_env(kobj, action, NULL);
}

int add_uevent_var(struct kobj_uevent_env *env, const char *format, ...)
{
	return 0;
}
