/* Minimal includes for freq_qos stubs */
#include <linux/pm_qos.h>

int freq_qos_add_request(struct freq_constraints *qos,
			 struct freq_qos_request *req,
			 enum freq_qos_req_type type, s32 value)
{
	return 0;
}

int freq_qos_update_request(struct freq_qos_request *req, s32 new_value)
{
	return 0;
}

int freq_qos_remove_request(struct freq_qos_request *req)
{
	return 0;
}

int freq_qos_add_notifier(struct freq_constraints *qos,
			   enum freq_qos_req_type type,
			   struct notifier_block *notifier)
{
	return 0;
}

int freq_qos_remove_notifier(struct freq_constraints *qos,
			      enum freq_qos_req_type type,
			      struct notifier_block *notifier)
{
	return 0;
}
