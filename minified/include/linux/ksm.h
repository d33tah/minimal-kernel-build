#ifndef __LINUX_KSM_H
#define __LINUX_KSM_H

#include <linux/bitops.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/sched.h>
#include <linux/sched/coredump.h>

struct stable_node;
struct mem_cgroup;



static inline void ksm_exit(struct mm_struct *mm)
{
}


#endif  
