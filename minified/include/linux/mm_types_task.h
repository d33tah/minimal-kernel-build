#ifndef _LINUX_MM_TYPES_TASK_H
#define _LINUX_MM_TYPES_TASK_H


#include <linux/types.h>
#include <linux/threads.h>
#include <linux/atomic.h>
#include <linux/cpumask.h>

#include <asm/page.h>

enum { MM_FILEPAGES, MM_ANONPAGES, MM_SHMEMPAGES, };

#endif
