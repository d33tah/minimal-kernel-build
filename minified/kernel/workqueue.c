// SPDX-License-Identifier: GPL-2.0-only
// Stubbed workqueue.c for minimal kernel - execute work synchronously

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
#include <linux/jhash.h>
#include <linux/hashtable.h>
#include <linux/rculist.h>
#include <linux/nodemask.h>
#include <linux/moduleparam.h>
#include <linux/uaccess.h>
#include <linux/sched/isolation.h>
#include <linux/nmi.h>

#include "workqueue_internal.h"

// Define the workqueue structure
struct workqueue_struct {
    unsigned int flags;
    const char *name;
};

// Stub: Execute work synchronously instead of queuing
bool queue_work_on(int cpu, struct workqueue_struct *wq, struct work_struct *work)
{
    // Execute work immediately if not already pending
    if (test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work)))
        return false;

    work->func(work);
    clear_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work));
    return true;
}

bool queue_work_node(int node, struct workqueue_struct *wq, struct work_struct *work)
{
    return queue_work_on(WORK_CPU_UNBOUND, wq, work);
}

bool queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
                           struct delayed_work *dwork, unsigned long delay)
{
    if (delay == 0)
        return queue_work_on(cpu, wq, &dwork->work);

    // For delayed work, just execute immediately (ignore delay)
    return queue_work_on(cpu, wq, &dwork->work);
}

bool mod_delayed_work_on(int cpu, struct workqueue_struct *wq,
                         struct delayed_work *dwork, unsigned long delay)
{
    // Cancel any pending work and queue new one
    cancel_delayed_work(dwork);
    return queue_delayed_work_on(cpu, wq, dwork, delay);
}

void __flush_workqueue(struct workqueue_struct *wq)
{
    // Stub: Nothing to flush in synchronous execution
}

bool flush_work(struct work_struct *work)
{
    // Stub: Work is already done synchronously
    return false;
}

bool cancel_work_sync(struct work_struct *work)
{
    // Stub: Clear pending bit
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

void drain_workqueue(struct workqueue_struct *wq)
{
    // Stub: Nothing to drain
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

int execute_in_process_context(work_func_t fn, struct execute_work *ew)
{
    // Execute immediately
    fn(&ew->work);
    return 0;
}

// System workqueues (allocated statically)
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
    // Stub: Minimal initialization
}

void __init workqueue_init(void)
{
    // Stub: Minimal initialization
}

// Dummy functions for compatibility
void show_all_workqueues(void) {}
void show_one_workqueue(struct workqueue_struct *wq) {}
void wq_watchdog_set_thresh(unsigned long thresh) {}

int workqueue_prepare_cpu(unsigned int cpu) { return 0; }
int workqueue_online_cpu(unsigned int cpu) { return 0; }
int workqueue_offline_cpu(unsigned int cpu) { return 0; }

void print_worker_info(const char *log_lvl, struct task_struct *task) {}

void wq_worker_running(struct task_struct *task) {}
void wq_worker_sleeping(struct task_struct *task) {}
void wq_worker_tick(struct task_struct *task) {}

void delayed_work_timer_fn(struct timer_list *t)
{
    struct delayed_work *dwork = from_timer(dwork, t, timer);
    // Execute immediately in our stub implementation
    queue_work(dwork->wq, &dwork->work);
}
