// SPDX-License-Identifier: GPL-2.0-or-later
/* Function to determine if a thread group is single threaded - STUBBED */
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/sched/mm.h>

/*
 * Returns true if the task does not share ->mm with another thread/process.
 */
bool current_is_single_threaded(void)
{
	return true;
}
