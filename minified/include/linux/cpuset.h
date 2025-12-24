#ifndef _LINUX_CPUSET_H
#define _LINUX_CPUSET_H
#include <linux/nodemask.h>
static inline bool cpusets_enabled(void) { return false; }
static inline int cpuset_init(void) { return 0; }
static inline void cpuset_init_smp(void) {}
#define cpuset_current_mems_allowed (node_states[N_MEMORY])
static inline void cpuset_init_current_mems_allowed(void) {}
static inline void set_mems_allowed(nodemask_t nodemask) {}
#endif  
