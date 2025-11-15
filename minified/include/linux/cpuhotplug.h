
#ifndef __CPUHOTPLUG_H
#define __CPUHOTPLUG_H

#include <linux/types.h>




enum cpuhp_state {
	CPUHP_INVALID = -1,


	CPUHP_OFFLINE = 0,
	CPUHP_SLUB_DEAD,
	CPUHP_MM_WRITEBACK_DEAD,
	CPUHP_SOFTIRQ_DEAD,
	CPUHP_PRINTK_DEAD,
	CPUHP_RADIX_DEAD,
	CPUHP_PAGE_ALLOC,
	CPUHP_BP_PREPARE_DYN,
	CPUHP_BP_PREPARE_DYN_END		= CPUHP_BP_PREPARE_DYN + 20,


	CPUHP_AP_ONLINE_DYN,
	CPUHP_AP_ONLINE_DYN_END		= CPUHP_AP_ONLINE_DYN + 30,

	CPUHP_ONLINE,
};

int __cpuhp_setup_state(enum cpuhp_state state,	const char *name, bool invoke,
			int (*startup)(unsigned int cpu),
			int (*teardown)(unsigned int cpu), bool multi_instance);

int __cpuhp_setup_state_cpuslocked(enum cpuhp_state state, const char *name,
				   bool invoke,
				   int (*startup)(unsigned int cpu),
				   int (*teardown)(unsigned int cpu),
				   bool multi_instance);

static inline int cpuhp_setup_state(enum cpuhp_state state,
				    const char *name,
				    int (*startup)(unsigned int cpu),
				    int (*teardown)(unsigned int cpu))
{
	return __cpuhp_setup_state(state, name, true, startup, teardown, false);
}


static inline int cpuhp_setup_state_cpuslocked(enum cpuhp_state state,
					       const char *name,
					       int (*startup)(unsigned int cpu),
					       int (*teardown)(unsigned int cpu))
{
	return __cpuhp_setup_state_cpuslocked(state, name, true, startup,
					      teardown, false);
}


static inline int cpuhp_setup_state_nocalls(enum cpuhp_state state,
					    const char *name,
					    int (*startup)(unsigned int cpu),
					    int (*teardown)(unsigned int cpu))
{
	return __cpuhp_setup_state(state, name, false, startup, teardown,
				   false);
}


static inline int cpuhp_setup_state_nocalls_cpuslocked(enum cpuhp_state state,
						     const char *name,
						     int (*startup)(unsigned int cpu),
						     int (*teardown)(unsigned int cpu))
{
	return __cpuhp_setup_state_cpuslocked(state, name, false, startup,
					    teardown, false);
}


static inline int cpuhp_setup_state_multi(enum cpuhp_state state,
					  const char *name,
					  int (*startup)(unsigned int cpu,
							 struct hlist_node *node),
					  int (*teardown)(unsigned int cpu,
							  struct hlist_node *node))
{
	return __cpuhp_setup_state(state, name, false,
				   (void *) startup,
				   (void *) teardown, true);
}

int __cpuhp_state_add_instance(enum cpuhp_state state, struct hlist_node *node,
			       bool invoke);
int __cpuhp_state_add_instance_cpuslocked(enum cpuhp_state state,
					  struct hlist_node *node, bool invoke);


static inline int cpuhp_state_add_instance(enum cpuhp_state state,
					   struct hlist_node *node)
{
	return __cpuhp_state_add_instance(state, node, true);
}


static inline int cpuhp_state_add_instance_nocalls(enum cpuhp_state state,
						   struct hlist_node *node)
{
	return __cpuhp_state_add_instance(state, node, false);
}


static inline int
cpuhp_state_add_instance_nocalls_cpuslocked(enum cpuhp_state state,
					    struct hlist_node *node)
{
	return __cpuhp_state_add_instance_cpuslocked(state, node, false);
}

void __cpuhp_remove_state(enum cpuhp_state state, bool invoke);
void __cpuhp_remove_state_cpuslocked(enum cpuhp_state state, bool invoke);


static inline void cpuhp_remove_state(enum cpuhp_state state)
{
	__cpuhp_remove_state(state, true);
}


static inline void cpuhp_remove_state_nocalls(enum cpuhp_state state)
{
	__cpuhp_remove_state(state, false);
}


static inline void cpuhp_remove_state_nocalls_cpuslocked(enum cpuhp_state state)
{
	__cpuhp_remove_state_cpuslocked(state, false);
}


static inline void cpuhp_remove_multi_state(enum cpuhp_state state)
{
	__cpuhp_remove_state(state, false);
}

int __cpuhp_state_remove_instance(enum cpuhp_state state,
				  struct hlist_node *node, bool invoke);


static inline int cpuhp_state_remove_instance(enum cpuhp_state state,
					      struct hlist_node *node)
{
	return __cpuhp_state_remove_instance(state, node, true);
}


static inline int cpuhp_state_remove_instance_nocalls(enum cpuhp_state state,
						      struct hlist_node *node)
{
	return __cpuhp_state_remove_instance(state, node, false);
}

static inline void cpuhp_online_idle(enum cpuhp_state state) { }

#endif
