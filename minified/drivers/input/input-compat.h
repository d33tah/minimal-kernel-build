/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _INPUT_COMPAT_H
#define _INPUT_COMPAT_H

/*
 * 32bit compatibility wrappers for the input subsystem.
 *
 * Very heavily based on evdev.c - Copyright (c) 1999-2002 Vojtech Pavlik
 */

#include <linux/compiler.h>
#include <linux/compat.h>
#include <linux/input.h>


static inline size_t input_event_size(void)
{
	return sizeof(struct input_event);
}


int input_event_from_user(const char __user *buffer,
			 struct input_event *event);

int input_event_to_user(char __user *buffer,
			const struct input_event *event);

int input_ff_effect_from_user(const char __user *buffer, size_t size,
			      struct ff_effect *effect);

#endif /* _INPUT_COMPAT_H */
