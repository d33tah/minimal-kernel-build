/* pm_qos.h - minimal stub, all functions unused */
#ifndef _LINUX_PM_QOS_H
#define _LINUX_PM_QOS_H

#include <linux/plist.h>
#include <linux/notifier.h>
#include <linux/device.h>

#define PM_QOS_LATENCY_ANY	S32_MAX
#define PM_QOS_RESUME_LATENCY_NO_CONSTRAINT	PM_QOS_LATENCY_ANY
#define PM_QOS_LATENCY_TOLERANCE_NO_CONSTRAINT	(-1)
#define FREQ_QOS_MAX_DEFAULT_VALUE	S32_MAX

enum pm_qos_type { PM_QOS_UNITIALIZED };
enum pm_qos_req_action { PM_QOS_ADD_REQ };

struct pm_qos_constraints {
	struct plist_head list;
	s32 target_value;
	s32 default_value;
	s32 no_constraint_value;
	enum pm_qos_type type;
	struct blocking_notifier_head *notifiers;
};

struct pm_qos_flags_request {
	struct list_head node;
	s32 flags;
};

struct pm_qos_flags {
	struct list_head list;
	s32 effective_flags;
};

s32 pm_qos_read_value(struct pm_qos_constraints *c);
int pm_qos_update_target(struct pm_qos_constraints *c, struct plist_node *node,
			 enum pm_qos_req_action action, int value);
bool pm_qos_update_flags(struct pm_qos_flags *pqf,
			 struct pm_qos_flags_request *req,
			 enum pm_qos_req_action action, s32 val);

#endif
