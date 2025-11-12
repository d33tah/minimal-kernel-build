// SPDX-License-Identifier: GPL-2.0-only
/*
 * Input Multitouch Library - STUBBED for minimal kernel
 */

#include <linux/input/mt.h>
#include <linux/export.h>
#include <linux/slab.h>

/* Stub functions - all return error or do nothing */
int input_mt_init_slots(struct input_dev *dev, unsigned int num_slots,
			unsigned int flags)
{
	return -ENOSYS;
}
EXPORT_SYMBOL(input_mt_init_slots);

void input_mt_destroy_slots(struct input_dev *dev)
{
}
EXPORT_SYMBOL(input_mt_destroy_slots);

bool input_mt_report_slot_state(struct input_dev *dev,
				unsigned int tool_type, bool active)
{
	return false;
}
EXPORT_SYMBOL(input_mt_report_slot_state);

void input_mt_report_finger_count(struct input_dev *dev, int count)
{
}
EXPORT_SYMBOL(input_mt_report_finger_count);

void input_mt_report_pointer_emulation(struct input_dev *dev, bool use_count)
{
}
EXPORT_SYMBOL(input_mt_report_pointer_emulation);

void input_mt_drop_unused(struct input_dev *dev)
{
}
EXPORT_SYMBOL(input_mt_drop_unused);

void input_mt_sync_frame(struct input_dev *dev)
{
}
EXPORT_SYMBOL(input_mt_sync_frame);

int input_mt_assign_slots(struct input_dev *dev, int *slots,
			   const struct input_mt_pos *pos, int num_pos,
			   int dmax)
{
	return 0;
}
EXPORT_SYMBOL(input_mt_assign_slots);

int input_mt_get_slot_by_key(struct input_dev *dev, int key)
{
	return -1;
}
EXPORT_SYMBOL(input_mt_get_slot_by_key);
