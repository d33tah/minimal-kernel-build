// SPDX-License-Identifier: GPL-2.0
/*
 *  syscore.c - Execution of system core operations.
 *
 *  Copyright (C) 2011 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 */

#include <linux/syscore_ops.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include <trace/events/power.h>

static LIST_HEAD(syscore_ops_list);
static DEFINE_MUTEX(syscore_ops_lock);

/**
 * register_syscore_ops - Register a set of system core operations.
 * @ops: System core operations to register.
 */
void register_syscore_ops(struct syscore_ops *ops)
{
	mutex_lock(&syscore_ops_lock);
	list_add_tail(&ops->node, &syscore_ops_list);
	mutex_unlock(&syscore_ops_lock);
}
EXPORT_SYMBOL_GPL(register_syscore_ops);

/**
 * unregister_syscore_ops - Unregister a set of system core operations.
 * @ops: System core operations to unregister.
 */
void unregister_syscore_ops(struct syscore_ops *ops)
{
	mutex_lock(&syscore_ops_lock);
	list_del(&ops->node);
	mutex_unlock(&syscore_ops_lock);
}
EXPORT_SYMBOL_GPL(unregister_syscore_ops);


/**
 * syscore_shutdown - Execute all the registered system core shutdown callbacks.
 */
void syscore_shutdown(void)
{
	struct syscore_ops *ops;

	mutex_lock(&syscore_ops_lock);

	list_for_each_entry_reverse(ops, &syscore_ops_list, node)
		if (ops->shutdown) {
			if (initcall_debug)
				pr_info("PM: Calling %pS\n", ops->shutdown);
			ops->shutdown();
		}

	mutex_unlock(&syscore_ops_lock);
}
