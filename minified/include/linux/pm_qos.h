/* pm_qos.h - minimal stub, all functions unused */
#ifndef _LINUX_PM_QOS_H
#define _LINUX_PM_QOS_H

#define PM_QOS_LATENCY_ANY	S32_MAX
#define PM_QOS_RESUME_LATENCY_NO_CONSTRAINT	PM_QOS_LATENCY_ANY
#define PM_QOS_LATENCY_TOLERANCE_NO_CONSTRAINT	(-1)

/* pm_qos_type, pm_qos_req_action enums removed - unused */
/* pm_qos_constraints, pm_qos_flags_request, pm_qos_flags structs removed - unused */
/* pm_qos_read_value, pm_qos_update_target, pm_qos_update_flags removed - never called */

#endif
