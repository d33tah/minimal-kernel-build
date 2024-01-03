// SPDX-License-Identifier: GPL-2.0-only
/*
 * 32bit compatibility wrappers for the input subsystem.
 *
 * Very heavily based on evdev.c - Copyright (c) 1999-2002 Vojtech Pavlik
 */

#include <linux/export.h>
#include <linux/uaccess.h>
#include "input-compat.h"


int input_event_from_user(const char __user *buffer,
			 struct input_event *event)
{
	if (copy_from_user(event, buffer, sizeof(struct input_event)))
		return -EFAULT;

	return 0;
}

int input_event_to_user(char __user *buffer,
			const struct input_event *event)
{
	if (copy_to_user(buffer, event, sizeof(struct input_event)))
		return -EFAULT;

	return 0;
}

int input_ff_effect_from_user(const char __user *buffer, size_t size,
			      struct ff_effect *effect)
{
	if (size != sizeof(struct ff_effect))
		return -EINVAL;

	if (copy_from_user(effect, buffer, sizeof(struct ff_effect)))
		return -EFAULT;

	return 0;
}


EXPORT_SYMBOL_GPL(input_event_from_user);
EXPORT_SYMBOL_GPL(input_event_to_user);
EXPORT_SYMBOL_GPL(input_ff_effect_from_user);
