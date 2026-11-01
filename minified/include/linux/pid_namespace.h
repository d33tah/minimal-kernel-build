/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PID_NS_H
#define _LINUX_PID_NS_H

#include <linux/mm.h>

struct pid_namespace {
	int last_pid;
	unsigned int level;
} __randomize_layout;

extern struct pid_namespace init_pid_ns;

static inline struct pid_namespace *get_pid_ns(struct pid_namespace *ns)
{
	return ns;
}
void pid_idr_init(void);

#endif
