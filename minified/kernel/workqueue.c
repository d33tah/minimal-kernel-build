/* Stub workqueue - runs work immediately (no threading) */
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/timer.h>

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

bool queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
			   struct delayed_work *dwork, unsigned long delay)
{
	if (delay == 0)
		return queue_work_on(cpu, wq, &dwork->work);

	return queue_work_on(cpu, wq, &dwork->work);
}

/* flush_work moved to workqueue.h as static inline */
/* flush_delayed_work removed - always returns false, single caller removed */

bool cancel_delayed_work(struct delayed_work *dwork)
{
	return test_and_clear_bit(WORK_STRUCT_PENDING_BIT,
				  work_data_bits(&dwork->work));
}

__printf(1, 4) struct workqueue_struct *alloc_workqueue(const char *fmt,
							unsigned int flags,
							int max_active, ...)
{
	struct workqueue_struct *wq;

	wq = kzalloc(sizeof(*wq), GFP_KERNEL);
	if (!wq)
		return NULL;

	wq->name = fmt;
	return wq;
}

static struct workqueue_struct system_wq_storage = { .name = "events" };
static struct workqueue_struct system_unbound_wq_storage = {
	.name = "events_unbound"
};

struct workqueue_struct *system_wq = &system_wq_storage;
struct workqueue_struct *system_unbound_wq = &system_unbound_wq_storage;

void delayed_work_timer_fn(struct timer_list *t)
{
	struct delayed_work *dwork = from_timer(dwork, t, timer);

	queue_work(dwork->wq, &dwork->work);
}
