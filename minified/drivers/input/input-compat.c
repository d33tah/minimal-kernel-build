// SPDX-License-Identifier: GPL-2.0-only
/*
 * Input compat layer - STUBBED for minimal kernel
 */

#include <linux/input.h>
#include <linux/export.h>

/* Stub functions - return error */
int input_event_from_user(const char __user *buffer,
			  struct input_event *event)
{
	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(input_event_from_user);

int input_event_to_user(char __user *buffer,
			const struct input_event *event)
{
	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(input_event_to_user);

int input_ff_effect_from_user(const char __user *buffer, size_t size,
			      struct ff_effect *effect)
{
	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(input_ff_effect_from_user);
