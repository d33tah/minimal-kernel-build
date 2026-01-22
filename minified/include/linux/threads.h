#ifndef _LINUX_THREADS_H
#define _LINUX_THREADS_H

/* CONFIG_NR_CPUS always 1 in our minimal config */
#define NR_CPUS		1

/* CONFIG_BASE_SMALL=1 in minimal config */
#define PID_MAX_DEFAULT 0x1000
#define PID_MAX_LIMIT (PAGE_SIZE * 8)

#define PIDS_PER_CPU_DEFAULT	1024
#define PIDS_PER_CPU_MIN	8

#endif
