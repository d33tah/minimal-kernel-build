/* Minimal includes for CPU hotplug */
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <linux/mutex.h>
#include <linux/bug.h>
#include <linux/list.h>

#include "smpboot.h"

struct cpuhp_cpu_state {
	enum cpuhp_state	state;
};

static DEFINE_PER_CPU(struct cpuhp_cpu_state, cpuhp_state);



struct cpuhp_step {
	const char		*name;
	union {
		int		(*single)(unsigned int cpu);
		int		(*multi)(unsigned int cpu,
					 struct hlist_node *node);
	} startup;
	union {
		int		(*single)(unsigned int cpu);
		int		(*multi)(unsigned int cpu,
					 struct hlist_node *node);
	} teardown;
	 
	struct hlist_head	list;

	bool			multi_instance;
};

static DEFINE_MUTEX(cpuhp_state_mutex);
static struct cpuhp_step cpuhp_hp_states[];

static struct cpuhp_step *cpuhp_get_step(enum cpuhp_state state)
{
	return cpuhp_hp_states + state;
}

static struct cpuhp_step cpuhp_hp_states[] = {
	[CPUHP_OFFLINE] = {
		.name			= "offline",
		.startup.single		= NULL,
		.teardown.single	= NULL,
	},
	 


	 
	[CPUHP_ONLINE] = {
		.name			= "online",
		.startup.single		= NULL,
		.teardown.single	= NULL,
	},
};

static int cpuhp_cb_check(enum cpuhp_state state)
{
	if (state <= CPUHP_OFFLINE || state >= CPUHP_ONLINE)
		return -EINVAL;
	return 0;
}

int __cpuhp_setup_state_cpuslocked(enum cpuhp_state state,
				   const char *name, bool invoke,
				   int (*startup)(unsigned int cpu),
				   int (*teardown)(unsigned int cpu),
				   bool multi_instance)
{
	struct cpuhp_step *sp;

	lockdep_assert_cpus_held();

	if (cpuhp_cb_check(state) || !name)
		return -EINVAL;

	mutex_lock(&cpuhp_state_mutex);

	sp = cpuhp_get_step(state);
	sp->startup.single = startup;
	sp->teardown.single = teardown;
	sp->name = name;
	sp->multi_instance = multi_instance;
	INIT_HLIST_HEAD(&sp->list);

	mutex_unlock(&cpuhp_state_mutex);
	return 0;
}

int __cpuhp_setup_state(enum cpuhp_state state,
			const char *name, bool invoke,
			int (*startup)(unsigned int cpu),
			int (*teardown)(unsigned int cpu),
			bool multi_instance)
{
	int ret;

	cpus_read_lock();
	ret = __cpuhp_setup_state_cpuslocked(state, name, invoke, startup,
					     teardown, multi_instance);
	cpus_read_unlock();
	return ret;
}



#define MASK_DECLARE_1(x)	[x+1][0] = (1UL << (x))
#define MASK_DECLARE_2(x)	MASK_DECLARE_1(x), MASK_DECLARE_1(x+1)
#define MASK_DECLARE_4(x)	MASK_DECLARE_2(x), MASK_DECLARE_2(x+2)
#define MASK_DECLARE_8(x)	MASK_DECLARE_4(x), MASK_DECLARE_4(x+4)

const unsigned long cpu_bit_bitmap[BITS_PER_LONG+1][BITS_TO_LONGS(NR_CPUS)] = {

	MASK_DECLARE_8(0),	MASK_DECLARE_8(8),
	MASK_DECLARE_8(16),	MASK_DECLARE_8(24),
#if BITS_PER_LONG > 32
	MASK_DECLARE_8(32),	MASK_DECLARE_8(40),
	MASK_DECLARE_8(48),	MASK_DECLARE_8(56),
#endif
};

const DECLARE_BITMAP(cpu_all_bits, NR_CPUS) = CPU_BITS_ALL;

struct cpumask __cpu_possible_mask __read_mostly;

struct cpumask __cpu_online_mask __read_mostly;

struct cpumask __cpu_present_mask __read_mostly;

struct cpumask __cpu_active_mask __read_mostly;


atomic_t __num_online_cpus __read_mostly;

void set_cpu_online(unsigned int cpu, bool online)
{
	 
	if (online) {
		if (!cpumask_test_and_set_cpu(cpu, &__cpu_online_mask))
			atomic_inc(&__num_online_cpus);
	} else {
		if (cpumask_test_and_clear_cpu(cpu, &__cpu_online_mask))
			atomic_dec(&__num_online_cpus);
	}
}

void __init boot_cpu_init(void)
{
	int cpu = smp_processor_id();

	 
	set_cpu_online(cpu, true);
	set_cpu_active(cpu, true);
	set_cpu_present(cpu, true);
	set_cpu_possible(cpu, true);

}

void __init boot_cpu_hotplug_init(void)
{
	this_cpu_write(cpuhp_state.state, CPUHP_ONLINE);
}


