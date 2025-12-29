#ifndef _LINUX_CPUSET_H
#define _LINUX_CPUSET_H
#include <linux/nodemask.h>
#define cpuset_current_mems_allowed (node_states[N_MEMORY])
#endif
