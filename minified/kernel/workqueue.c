/* Stub workqueue - runs work immediately (no threading) */
#include <linux/workqueue.h>
#include <linux/slab.h>

struct workqueue_struct {
	const char *name;
};

bool queue_work_on(int cpu, struct workqueue_struct *wq,
		   struct work_struct *work)
{
	if (test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work)))
		return false;

	work->func(work);
	clear_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work));
	return true;
}

static struct workqueue_struct system_wq_storage = { .name = "events" };

struct workqueue_struct *system_wq = &system_wq_storage;
