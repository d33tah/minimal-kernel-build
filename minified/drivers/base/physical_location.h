/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Device physical location support
 *
 * Author: Won Chung <wonchung@google.com>
 */

#include <linux/device.h>

static inline bool dev_add_physical_location(struct device *dev) { return false; };
static const struct attribute_group dev_attr_physical_location_group = {};
