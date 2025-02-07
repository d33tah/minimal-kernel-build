// SPDX-License-Identifier: GPL-2.0
/*
 * kernel userspace event delivery
 *
 * Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
 * Copyright (C) 2004 Novell, Inc.  All rights reserved.
 * Copyright (C) 2004 IBM, Inc. All rights reserved.
 *
 * Authors:
 *	Robert Love		<rml@novell.com>
 *	Kay Sievers		<kay.sievers@vrfy.org>
 *	Arjan van de Ven	<arjanv@redhat.com>
 *	Greg Kroah-Hartman	<greg@kroah.com>
 */

#include <linux/string.h>
#include <asm/bug.h>
#include <linux/kobject.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/uuid.h>
#include <linux/ctype.h>

#include "asm-generic/errno-base.h"
#include "asm-generic/int-ll64.h"
#include "asm/string_32.h"
#include "linux/gfp.h"
#include "linux/kern_levels.h"
#include "linux/kernel.h"
#include "linux/mutex.h"
#include "linux/printk.h"
#include "linux/stdarg.h"
#include "linux/types.h"


u64 uevent_seqnum;

struct uevent_sock {
	struct list_head list;
	struct sock *sk;
};


/* This lock protects uevent_seqnum and uevent_sock_list */
static DEFINE_MUTEX(uevent_sock_mutex);

/* the strings here must match the enum in include/linux/kobject.h */
static const char *kobject_actions[] = {
	[KOBJ_ADD] =		"add",
	[KOBJ_REMOVE] =		"remove",
	[KOBJ_CHANGE] =		"change",
	[KOBJ_MOVE] =		"move",
	[KOBJ_ONLINE] =		"online",
	[KOBJ_OFFLINE] =	"offline",
	[KOBJ_BIND] =		"bind",
	[KOBJ_UNBIND] =		"unbind",
};

static int kobject_action_type(const char *buf, size_t count,
			       enum kobject_action *type,
			       const char **args)
{
	enum kobject_action action;
	size_t count_first;
	const char *args_start;
	int ret = -EINVAL;

	if (count && (buf[count-1] == '\n' || buf[count-1] == '\0'))
		count--;

	if (!count)
		goto out;

	args_start = strnchr(buf, count, ' ');
	if (args_start) {
		count_first = args_start - buf;
		args_start = args_start + 1;
	} else
		count_first = count;

	for (action = 0; action < ARRAY_SIZE(kobject_actions); action++) {
		if (strncmp(kobject_actions[action], buf, count_first) != 0)
			continue;
		if (kobject_actions[action][count_first] != '\0')
			continue;
		if (args)
			*args = args_start;
		*type = action;
		ret = 0;
		break;
	}
out:
	return ret;
}

static const char *action_arg_word_end(const char *buf, const char *buf_end,
				       char delim)
{
	const char *next = buf;

	while (next <= buf_end && *next != delim)
		if (!isalnum(*next++))
			return NULL;

	if (next == buf)
		return NULL;

	return next;
}

static int kobject_action_args(const char *buf, size_t count,
			       struct kobj_uevent_env **ret_env)
{
	struct kobj_uevent_env *env = NULL;
	const char *next, *buf_end, *key;
	int key_len;
	int r = -EINVAL;

	if (count && (buf[count - 1] == '\n' || buf[count - 1] == '\0'))
		count--;

	if (!count)
		return -EINVAL;

	env = kzalloc(sizeof(*env), GFP_KERNEL);
	if (!env)
		return -ENOMEM;

	/* first arg is UUID */
	if (count < UUID_STRING_LEN || !uuid_is_valid(buf) ||
	    add_uevent_var(env, "SYNTH_UUID=%.*s", UUID_STRING_LEN, buf))
		goto out;

	/*
	 * the rest are custom environment variables in KEY=VALUE
	 * format with ' ' delimiter between each KEY=VALUE pair
	 */
	next = buf + UUID_STRING_LEN;
	buf_end = buf + count - 1;

	while (next <= buf_end) {
		if (*next != ' ')
			goto out;

		/* skip the ' ', key must follow */
		key = ++next;
		if (key > buf_end)
			goto out;

		buf = next;
		next = action_arg_word_end(buf, buf_end, '=');
		if (!next || next > buf_end || *next != '=')
			goto out;
		key_len = next - buf;

		/* skip the '=', value must follow */
		if (++next > buf_end)
			goto out;

		buf = next;
		next = action_arg_word_end(buf, buf_end, ' ');
		if (!next)
			goto out;

		if (add_uevent_var(env, "SYNTH_ARG_%.*s=%.*s",
				   key_len, key, (int) (next - buf), buf))
			goto out;
	}

	r = 0;
out:
	if (r)
		kfree(env);
	else
		*ret_env = env;
	return r;
}

/**
 * kobject_synth_uevent - send synthetic uevent with arguments
 *
 * @kobj: struct kobject for which synthetic uevent is to be generated
 * @buf: buffer containing action type and action args, newline is ignored
 * @count: length of buffer
 *
 * Returns 0 if kobject_synthetic_uevent() is completed with success or the
 * corresponding error when it fails.
 */
int kobject_synth_uevent(struct kobject *kobj, const char *buf, size_t count)
{
	char *no_uuid_envp[] = { "SYNTH_UUID=0", NULL };
	enum kobject_action action;
	const char *action_args;
	struct kobj_uevent_env *env;
	const char *msg = NULL, *devpath;
	int r;

	r = kobject_action_type(buf, count, &action, &action_args);
	if (r) {
		msg = "unknown uevent action string";
		goto out;
	}

	if (!action_args) {
		r = kobject_uevent_env(kobj, action, no_uuid_envp);
		goto out;
	}

	r = kobject_action_args(action_args,
				count - (action_args - buf), &env);
	if (r == -EINVAL) {
		msg = "incorrect uevent action arguments";
		goto out;
	}

	if (r)
		goto out;

	r = kobject_uevent_env(kobj, action, env->envp);
	kfree(env);
out:
	if (r) {
		devpath = kobject_get_path(kobj, GFP_KERNEL);
		pr_warn("synth uevent: %s: %s\n",
		       devpath ?: "unknown device",
		       msg ?: "failed to send uevent");
		kfree(devpath);
	}
	return r;
}



static int kobject_uevent_net_broadcast(struct kobject *kobj,
					struct kobj_uevent_env *env,
					const char *action_string,
					const char *devpath)
{
	int ret = 0;


	return ret;
}

static void zap_modalias_env(struct kobj_uevent_env *env)
{
	static const char modalias_prefix[] = "MODALIAS=";
	size_t len;
	int i, j;

	for (i = 0; i < env->envp_idx;) {
		if (strncmp(env->envp[i], modalias_prefix,
			    sizeof(modalias_prefix) - 1)) {
			i++;
			continue;
		}

		len = strlen(env->envp[i]) + 1;

		if (i != env->envp_idx - 1) {
			memmove(env->envp[i], env->envp[i + 1],
				env->buflen - len);

			for (j = i; j < env->envp_idx - 1; j++)
				env->envp[j] = env->envp[j + 1] - len;
		}

		env->envp_idx--;
		env->buflen -= len;
	}
}

/**
 * kobject_uevent_env - send an uevent with environmental data
 *
 * @kobj: struct kobject that the action is happening to
 * @action: action that is happening
 * @envp_ext: pointer to environmental data
 *
 * Returns 0 if kobject_uevent_env() is completed with success or the
 * corresponding error when it fails.
 */
int kobject_uevent_env(struct kobject *kobj, enum kobject_action action,
		       char *envp_ext[])
{
	struct kobj_uevent_env *env;
	const char *action_string = kobject_actions[action];
	const char *devpath = NULL;
	const char *subsystem;
	struct kobject *top_kobj;
	struct kset *kset;
	const struct kset_uevent_ops *uevent_ops;
	int i = 0;
	int retval = 0;

	/*
	 * Mark "remove" event done regardless of result, for some subsystems
	 * do not want to re-trigger "remove" event via automatic cleanup.
	 */
	if (action == KOBJ_REMOVE)
		kobj->state_remove_uevent_sent = 1;

	pr_debug("kobject: '%s' (%p): %s\n",
		 kobject_name(kobj), kobj, __func__);

	/* search the kset we belong to */
	top_kobj = kobj;
	while (!top_kobj->kset && top_kobj->parent)
		top_kobj = top_kobj->parent;

	if (!top_kobj->kset) {
		pr_debug("kobject: '%s' (%p): %s: attempted to send uevent "
			 "without kset!\n", kobject_name(kobj), kobj,
			 __func__);
		return -EINVAL;
	}

	kset = top_kobj->kset;
	uevent_ops = kset->uevent_ops;

	/* skip the event, if uevent_suppress is set*/
	if (kobj->uevent_suppress) {
		pr_debug("kobject: '%s' (%p): %s: uevent_suppress "
				 "caused the event to drop!\n",
				 kobject_name(kobj), kobj, __func__);
		return 0;
	}
	/* skip the event, if the filter returns zero. */
	if (uevent_ops && uevent_ops->filter)
		if (!uevent_ops->filter(kobj)) {
			pr_debug("kobject: '%s' (%p): %s: filter function "
				 "caused the event to drop!\n",
				 kobject_name(kobj), kobj, __func__);
			return 0;
		}

	/* originating subsystem */
	if (uevent_ops && uevent_ops->name)
		subsystem = uevent_ops->name(kobj);
	else
		subsystem = kobject_name(&kset->kobj);
	if (!subsystem) {
		pr_debug("kobject: '%s' (%p): %s: unset subsystem caused the "
			 "event to drop!\n", kobject_name(kobj), kobj,
			 __func__);
		return 0;
	}

	/* environment buffer */
	env = kzalloc(sizeof(struct kobj_uevent_env), GFP_KERNEL);
	if (!env)
		return -ENOMEM;

	/* complete object path */
	devpath = kobject_get_path(kobj, GFP_KERNEL);
	if (!devpath) {
		retval = -ENOENT;
		goto exit;
	}

	/* default keys */
	retval = add_uevent_var(env, "ACTION=%s", action_string);
	if (retval)
		goto exit;
	retval = add_uevent_var(env, "DEVPATH=%s", devpath);
	if (retval)
		goto exit;
	retval = add_uevent_var(env, "SUBSYSTEM=%s", subsystem);
	if (retval)
		goto exit;

	/* keys passed in from the caller */
	if (envp_ext) {
		for (i = 0; envp_ext[i]; i++) {
			retval = add_uevent_var(env, "%s", envp_ext[i]);
			if (retval)
				goto exit;
		}
	}

	/* let the kset specific function add its stuff */
	if (uevent_ops && uevent_ops->uevent) {
		retval = uevent_ops->uevent(kobj, env);
		if (retval) {
			pr_debug("kobject: '%s' (%p): %s: uevent() returned "
				 "%d\n", kobject_name(kobj), kobj,
				 __func__, retval);
			goto exit;
		}
	}

	switch (action) {
	case KOBJ_ADD:
		/*
		 * Mark "add" event so we can make sure we deliver "remove"
		 * event to userspace during automatic cleanup. If
		 * the object did send an "add" event, "remove" will
		 * automatically generated by the core, if not already done
		 * by the caller.
		 */
		kobj->state_add_uevent_sent = 1;
		break;

	case KOBJ_UNBIND:
		zap_modalias_env(env);
		break;

	default:
		break;
	}

	mutex_lock(&uevent_sock_mutex);
	/* we will send an event, so request a new sequence number */
	retval = add_uevent_var(env, "SEQNUM=%llu", ++uevent_seqnum);
	if (retval) {
		mutex_unlock(&uevent_sock_mutex);
		goto exit;
	}
	retval = kobject_uevent_net_broadcast(kobj, env, action_string,
					      devpath);
	mutex_unlock(&uevent_sock_mutex);


exit:
	kfree(devpath);
	kfree(env);
	return retval;
}
EXPORT_SYMBOL_GPL(kobject_uevent_env);

/**
 * kobject_uevent - notify userspace by sending an uevent
 *
 * @kobj: struct kobject that the action is happening to
 * @action: action that is happening
 *
 * Returns 0 if kobject_uevent() is completed with success or the
 * corresponding error when it fails.
 */
int kobject_uevent(struct kobject *kobj, enum kobject_action action)
{
	return kobject_uevent_env(kobj, action, NULL);
}
EXPORT_SYMBOL_GPL(kobject_uevent);

/**
 * add_uevent_var - add key value string to the environment buffer
 * @env: environment buffer structure
 * @format: printf format for the key=value pair
 *
 * Returns 0 if environment variable was added successfully or -ENOMEM
 * if no space was available.
 */
int add_uevent_var(struct kobj_uevent_env *env, const char *format, ...)
{
	va_list args;
	int len;

	if (env->envp_idx >= ARRAY_SIZE(env->envp)) {
		WARN(1, KERN_ERR "add_uevent_var: too many keys\n");
		return -ENOMEM;
	}

	va_start(args, format);
	len = vsnprintf(&env->buf[env->buflen],
			sizeof(env->buf) - env->buflen,
			format, args);
	va_end(args);

	if (len >= (sizeof(env->buf) - env->buflen)) {
		WARN(1, KERN_ERR "add_uevent_var: buffer size too small\n");
		return -ENOMEM;
	}

	env->envp[env->envp_idx++] = &env->buf[env->buflen];
	env->buflen += len + 1;
	return 0;
}
EXPORT_SYMBOL_GPL(add_uevent_var);

