// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal stub - PM QoS not needed for minimal kernel
 */
#include <linux/pm_qos.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/export.h>

/* Stubbed exports */
int freq_qos_add_request(struct freq_constraints *qos,
			 struct freq_qos_request *req,
			 enum freq_qos_req_type type, s32 value)
{
	return 0;
}
EXPORT_SYMBOL_GPL(freq_qos_add_request);

int freq_qos_update_request(struct freq_qos_request *req, s32 new_value)
{
	return 0;
}
EXPORT_SYMBOL_GPL(freq_qos_update_request);

int freq_qos_remove_request(struct freq_qos_request *req)
{
	return 0;
}
EXPORT_SYMBOL_GPL(freq_qos_remove_request);

int freq_qos_add_notifier(struct freq_constraints *qos,
			   enum freq_qos_req_type type,
			   struct notifier_block *notifier)
{
	return 0;
}
EXPORT_SYMBOL_GPL(freq_qos_add_notifier);

int freq_qos_remove_notifier(struct freq_constraints *qos,
			      enum freq_qos_req_type type,
			      struct notifier_block *notifier)
{
	return 0;
}
EXPORT_SYMBOL_GPL(freq_qos_remove_notifier);
