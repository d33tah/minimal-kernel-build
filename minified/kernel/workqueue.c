
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/kthread.h>
#include <linux/hardirq.h>
#include <linux/mempolicy.h>
#include <linux/freezer.h>
#include <linux/debug_locks.h>
#include <linux/lockdep.h>
#include <linux/idr.h>
#include <linux/rculist.h>
#include <linux/sched/isolation.h>

#include "workqueue_internal.h"

struct workqueue_struct {
    unsigned int flags;
    const char *name;
};

bool queue_work_on(int cpu, struct workqueue_struct *wq, struct work_struct *work)
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

bool mod_delayed_work_on(int cpu, struct workqueue_struct *wq,
                         struct delayed_work *dwork, unsigned long delay)
{
     
    cancel_delayed_work(dwork);
    return queue_delayed_work_on(cpu, wq, dwork, delay);
}

void __flush_workqueue(struct workqueue_struct *wq)
{
     
}

bool flush_work(struct work_struct *work)
{
     
    return false;
}

bool cancel_work_sync(struct work_struct *work)
{
     
    return test_and_clear_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work));
}

bool flush_delayed_work(struct delayed_work *dwork)
{
    return flush_work(&dwork->work);
}

bool cancel_delayed_work(struct delayed_work *dwork)
{
    return cancel_work_sync(&dwork->work);
}

bool cancel_delayed_work_sync(struct delayed_work *dwork)
{
    return cancel_work_sync(&dwork->work);
}

__printf(1, 4) struct workqueue_struct *
alloc_workqueue(const char *fmt, unsigned int flags, int max_active, ...)
{
    struct workqueue_struct *wq;

    wq = kzalloc(sizeof(*wq), GFP_KERNEL);
    if (!wq)
        return NULL;

    wq->flags = flags;
    wq->name = fmt;
    return wq;
}

void destroy_workqueue(struct workqueue_struct *wq)
{
    if (wq)
        kfree(wq);
}

static struct workqueue_struct system_wq_storage = { .name = "events" };
static struct workqueue_struct system_highpri_wq_storage = { .name = "events_highpri" };
static struct workqueue_struct system_long_wq_storage = { .name = "events_long" };
static struct workqueue_struct system_unbound_wq_storage = { .name = "events_unbound" };
static struct workqueue_struct system_freezable_wq_storage = { .name = "events_freezable" };
static struct workqueue_struct system_power_efficient_wq_storage = { .name = "events_power_efficient" };
static struct workqueue_struct system_freezable_power_efficient_wq_storage = { .name = "events_freezable_power_efficient" };

struct workqueue_struct *system_wq = &system_wq_storage;
struct workqueue_struct *system_highpri_wq = &system_highpri_wq_storage;
struct workqueue_struct *system_long_wq = &system_long_wq_storage;
struct workqueue_struct *system_unbound_wq = &system_unbound_wq_storage;
struct workqueue_struct *system_freezable_wq = &system_freezable_wq_storage;
struct workqueue_struct *system_power_efficient_wq = &system_power_efficient_wq_storage;
struct workqueue_struct *system_freezable_power_efficient_wq = &system_freezable_power_efficient_wq_storage;

void __init workqueue_init_early(void)
{
     
}

void __init workqueue_init(void)
{
     
}


void wq_watchdog_set_thresh(unsigned long thresh) {}

int workqueue_prepare_cpu(unsigned int cpu) { return 0; }
int workqueue_online_cpu(unsigned int cpu) { return 0; }
int workqueue_offline_cpu(unsigned int cpu) { return 0; }

void wq_worker_running(struct task_struct *task) {}
void wq_worker_sleeping(struct task_struct *task) {}
void wq_worker_tick(struct task_struct *task) {}

void delayed_work_timer_fn(struct timer_list *t)
{
    struct delayed_work *dwork = from_timer(dwork, t, timer);

    queue_work(dwork->wq, &dwork->work);
}

int schedule_on_each_cpu(work_func_t func)
{

	return 0;
}
