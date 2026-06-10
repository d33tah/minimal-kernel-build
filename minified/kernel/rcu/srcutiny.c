
#include <linux/export.h>
#include <linux/init.h>
#include <linux/cache.h>

#include "rcu.h"

int rcu_scheduler_active __read_mostly;

void __init rcu_scheduler_starting(void)
{
	rcu_scheduler_active = RCU_SCHEDULER_RUNNING;
}

void __init srcu_init(void)
{
}
