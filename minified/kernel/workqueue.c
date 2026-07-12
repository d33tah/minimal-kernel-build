/* Stub workqueue - runs work immediately (no threading) */
#include <linux/workqueue.h>

struct workqueue_struct {
};

bool queue_work_on(int cpu, struct workqueue_struct *wq, struct work_struct *work)
{
     
    if (test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work)))
        return false;

    work->func(work);
    clear_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work));
    return true;
}

bool queue_delayed_work_on(int cpu, struct workqueue_struct *wq, struct delayed_work *dwork, unsigned long delay)
{
    if (delay == 0)
        return queue_work_on(cpu, wq, &dwork->work);


    return queue_work_on(cpu, wq, &dwork->work);
}


/* cancel_work_sync removed: zero callers on this boot. */

static struct workqueue_struct system_wq_storage = {};
static struct workqueue_struct system_unbound_wq_storage = {};

struct workqueue_struct *system_wq = &system_wq_storage;
struct workqueue_struct *system_unbound_wq = &system_unbound_wq_storage;

void __init workqueue_init_early(void)
{
     
}

void __init workqueue_init(void)
{
     
}


void delayed_work_timer_fn(struct timer_list *t)
{
    struct delayed_work *dwork = from_timer(dwork, t, timer);

    queue_work(dwork->wq, &dwork->work);
}

