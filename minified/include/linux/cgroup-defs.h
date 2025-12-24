#ifndef _LINUX_CGROUP_DEFS_H
#define _LINUX_CGROUP_DEFS_H
#include <linux/types.h>
struct u64_stats_sync {};
typedef struct { u64 v; } u64_stats_t;
struct task_struct;
static inline void cgroup_threadgroup_change_begin(struct task_struct *tsk) {}
static inline void cgroup_threadgroup_change_end(struct task_struct *tsk) {}
#endif
