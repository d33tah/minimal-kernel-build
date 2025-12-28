#ifndef _LINUX_CPUSET_H
#define _LINUX_CPUSET_H
#include <linux/nodemask.h>
static inline bool cpusets_enabled(void) { return false; }
static inline int cpuset_init(void) { return 0; }
#define cpuset_current_mems_allowed (node_states[N_MEMORY])
#endif
