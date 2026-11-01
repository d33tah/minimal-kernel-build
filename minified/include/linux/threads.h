/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_THREADS_H
#define _LINUX_THREADS_H

/*
 * This controls the default maximum pid allocated to a process
 */
#define PID_MAX_DEFAULT (CONFIG_BASE_SMALL ? 0x1000 : 0x8000)

#define PID_MAX_LIMIT            \
	(CONFIG_BASE_SMALL ?     \
		 PAGE_SIZE * 8 : \
		 (sizeof(long) > 4 ? 4 * 1024 * 1024 : PID_MAX_DEFAULT))

/*
 * Define a minimum number of pids per cpu.  Heuristically based
 * on original pid max of 32k for 32 cpus.  Also, increase the
 * minimum settable value for pid_max on the running system based
 * on similar defaults.  See kernel/pid.c:pid_idr_init() for details.
 */
#define PIDS_PER_CPU_DEFAULT 1024

#endif
