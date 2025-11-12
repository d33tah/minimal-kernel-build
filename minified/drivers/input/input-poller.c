// SPDX-License-Identifier: GPL-2.0-only
/*
 * Input polling support - STUBBED for minimal kernel
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/export.h>

/* Stub functions - do nothing or return error */
int input_setup_polling(struct input_dev *dev,
			void (*poll_fn)(struct input_dev *dev))
{
	return 0;
}
EXPORT_SYMBOL(input_setup_polling);

void input_set_poll_interval(struct input_dev *dev, unsigned int interval)
{
}
EXPORT_SYMBOL(input_set_poll_interval);

void input_set_min_poll_interval(struct input_dev *dev, unsigned int interval)
{
}
EXPORT_SYMBOL(input_set_min_poll_interval);

void input_set_max_poll_interval(struct input_dev *dev, unsigned int interval)
{
}
EXPORT_SYMBOL(input_set_max_poll_interval);

int input_get_poll_interval(struct input_dev *dev)
{
	return 0;
}
EXPORT_SYMBOL(input_get_poll_interval);

/* Internal stub functions */
void input_dev_poller_start(struct input_dev_poller *poller)
{
}

void input_dev_poller_stop(struct input_dev_poller *poller)
{
}

void input_dev_poller_finalize(struct input_dev_poller *poller)
{
}

/* Empty attribute group */
const struct attribute_group input_poller_attribute_group = {
	.attrs = NULL,
};
