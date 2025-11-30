 
#include <linux/sched/mm.h>
#include <linux/proc_fs.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/sched/signal.h>

#include <linux/sched/isolation.h>
#include <linux/sched/task.h>
#include <linux/sched/smt.h>
#include <linux/unistd.h>
#include <linux/cpu.h>
#include <linux/oom.h>
#include <linux/rcupdate.h>
#include <linux/export.h>
#include <linux/bug.h>
#include <linux/kthread.h>
#include <linux/stop_machine.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <linux/suspend.h>
#include <linux/lockdep.h>
#include <linux/tick.h>
#include <linux/irq.h>
#include <linux/nmi.h>
#include <linux/smpboot.h>

#include <linux/slab.h>
#include <linux/scs.h>
#include <linux/percpu-rwsem.h>
#include <linux/cpuset.h>
#include <linux/random.h>
#include <linux/cc_platform.h>


#include "smpboot.h"

 
struct cpuhp_cpu_state {
	enum cpuhp_state	state;
	enum cpuhp_state	target;
	enum cpuhp_state	fail;
};

static DEFINE_PER_CPU(struct cpuhp_cpu_state, cpuhp_state) = {
	.fail = CPUHP_INVALID,
};



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
	 
	bool			cant_stop;
	bool			multi_instance;
};

static DEFINE_MUTEX(cpuhp_state_mutex);
static struct cpuhp_step cpuhp_hp_states[];

static struct cpuhp_step *cpuhp_get_step(enum cpuhp_state state)
{
	return cpuhp_hp_states + state;
}

static bool cpuhp_step_empty(bool bringup, struct cpuhp_step *step)
{
	return bringup ? !step->startup.single : !step->teardown.single;
}

 
static int cpuhp_invoke_callback(unsigned int cpu, enum cpuhp_state state,
				 bool bringup, struct hlist_node *node,
				 struct hlist_node **lastp)
{
	struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
	struct cpuhp_step *step = cpuhp_get_step(state);
	int (*cbm)(unsigned int cpu, struct hlist_node *node);
	int (*cb)(unsigned int cpu);
	int ret, cnt;

	if (st->fail == state) {
		st->fail = CPUHP_INVALID;
		return -EAGAIN;
	}

	if (cpuhp_step_empty(bringup, step)) {
		WARN_ON_ONCE(1);
		return 0;
	}

	if (!step->multi_instance) {
		WARN_ON_ONCE(lastp && *lastp);
		cb = bringup ? step->startup.single : step->teardown.single;

		 
		ret = cb(cpu);
		 
		return ret;
	}
	cbm = bringup ? step->startup.multi : step->teardown.multi;

	 
	if (node) {
		WARN_ON_ONCE(lastp && *lastp);
		 
		ret = cbm(cpu, node);
		 
		return ret;
	}

	 
	cnt = 0;
	hlist_for_each(node, &step->list) {
		if (lastp && node == *lastp)
			break;

		 
		ret = cbm(cpu, node);
		 
		if (ret) {
			if (!lastp)
				goto err;

			*lastp = node;
			return ret;
		}
		cnt++;
	}
	if (lastp)
		*lastp = NULL;
	return 0;
err:
	 
	cbm = !bringup ? step->startup.multi : step->teardown.multi;
	if (!cbm)
		return ret;

	hlist_for_each(node, &step->list) {
		if (!cnt--)
			break;

		 
		ret = cbm(cpu, node);
		 
		 
		WARN_ON_ONCE(ret);
	}
	return ret;
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

 
static int cpuhp_reserve_state(enum cpuhp_state state)
{
	enum cpuhp_state i, end;
	struct cpuhp_step *step;

	switch (state) {
	case CPUHP_AP_ONLINE_DYN:
		step = cpuhp_hp_states + CPUHP_AP_ONLINE_DYN;
		end = CPUHP_AP_ONLINE_DYN_END;
		break;
	case CPUHP_BP_PREPARE_DYN:
		step = cpuhp_hp_states + CPUHP_BP_PREPARE_DYN;
		end = CPUHP_BP_PREPARE_DYN_END;
		break;
	default:
		return -EINVAL;
	}

	for (i = state; i <= end; i++, step++) {
		if (!step->name)
			return i;
	}
	WARN(1, "No more dynamic states available for CPU hotplug\n");
	return -ENOSPC;
}

static int cpuhp_store_callbacks(enum cpuhp_state state, const char *name,
				 int (*startup)(unsigned int cpu),
				 int (*teardown)(unsigned int cpu),
				 bool multi_instance)
{
	 
	struct cpuhp_step *sp;
	int ret = 0;

	 
	if (name && (state == CPUHP_AP_ONLINE_DYN ||
		     state == CPUHP_BP_PREPARE_DYN)) {
		ret = cpuhp_reserve_state(state);
		if (ret < 0)
			return ret;
		state = ret;
	}
	sp = cpuhp_get_step(state);
	if (name && sp->name)
		return -EBUSY;

	sp->startup.single = startup;
	sp->teardown.single = teardown;
	sp->name = name;
	sp->multi_instance = multi_instance;
	INIT_HLIST_HEAD(&sp->list);
	return ret;
}

static void *cpuhp_get_teardown_cb(enum cpuhp_state state)
{
	return cpuhp_get_step(state)->teardown.single;
}

 
static int cpuhp_issue_call(int cpu, enum cpuhp_state state, bool bringup,
			    struct hlist_node *node)
{
	struct cpuhp_step *sp = cpuhp_get_step(state);
	int ret;

	 
	if (cpuhp_step_empty(bringup, sp))
		return 0;
	 
	ret = cpuhp_invoke_callback(cpu, state, bringup, node, NULL);
	BUG_ON(ret && !bringup);
	return ret;
}

 
static void cpuhp_rollback_install(int failedcpu, enum cpuhp_state state,
				   struct hlist_node *node)
{
	int cpu;

	 
	for_each_present_cpu(cpu) {
		struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
		int cpustate = st->state;

		if (cpu >= failedcpu)
			break;

		 
		if (cpustate >= state)
			cpuhp_issue_call(cpu, state, false, node);
	}
}

int __cpuhp_state_add_instance_cpuslocked(enum cpuhp_state state,
					  struct hlist_node *node,
					  bool invoke)
{
	struct cpuhp_step *sp;
	int cpu;
	int ret;

	lockdep_assert_cpus_held();

	sp = cpuhp_get_step(state);
	if (sp->multi_instance == false)
		return -EINVAL;

	mutex_lock(&cpuhp_state_mutex);

	if (!invoke || !sp->startup.multi)
		goto add_node;

	 
	for_each_present_cpu(cpu) {
		struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
		int cpustate = st->state;

		if (cpustate < state)
			continue;

		ret = cpuhp_issue_call(cpu, state, true, node);
		if (ret) {
			if (sp->teardown.multi)
				cpuhp_rollback_install(cpu, state, node);
			goto unlock;
		}
	}
add_node:
	ret = 0;
	hlist_add_head(node, &sp->list);
unlock:
	mutex_unlock(&cpuhp_state_mutex);
	return ret;
}

int __cpuhp_state_add_instance(enum cpuhp_state state, struct hlist_node *node,
			       bool invoke)
{
	int ret;

	cpus_read_lock();
	ret = __cpuhp_state_add_instance_cpuslocked(state, node, invoke);
	cpus_read_unlock();
	return ret;
}

 
int __cpuhp_setup_state_cpuslocked(enum cpuhp_state state,
				   const char *name, bool invoke,
				   int (*startup)(unsigned int cpu),
				   int (*teardown)(unsigned int cpu),
				   bool multi_instance)
{
	int cpu, ret = 0;
	bool dynstate;

	lockdep_assert_cpus_held();

	if (cpuhp_cb_check(state) || !name)
		return -EINVAL;

	mutex_lock(&cpuhp_state_mutex);

	ret = cpuhp_store_callbacks(state, name, startup, teardown,
				    multi_instance);

	dynstate = state == CPUHP_AP_ONLINE_DYN;
	if (ret > 0 && dynstate) {
		state = ret;
		ret = 0;
	}

	if (ret || !invoke || !startup)
		goto out;

	 
	for_each_present_cpu(cpu) {
		struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
		int cpustate = st->state;

		if (cpustate < state)
			continue;

		ret = cpuhp_issue_call(cpu, state, true, NULL);
		if (ret) {
			if (teardown)
				cpuhp_rollback_install(cpu, state, NULL);
			cpuhp_store_callbacks(state, NULL, NULL, NULL, false);
			goto out;
		}
	}
out:
	mutex_unlock(&cpuhp_state_mutex);
	 
	if (!ret && dynstate)
		return state;
	return ret;
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

int __cpuhp_state_remove_instance(enum cpuhp_state state,
				  struct hlist_node *node, bool invoke)
{
	struct cpuhp_step *sp = cpuhp_get_step(state);
	int cpu;

	BUG_ON(cpuhp_cb_check(state));

	if (!sp->multi_instance)
		return -EINVAL;

	cpus_read_lock();
	mutex_lock(&cpuhp_state_mutex);

	if (!invoke || !cpuhp_get_teardown_cb(state))
		goto remove;
	 
	for_each_present_cpu(cpu) {
		struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
		int cpustate = st->state;

		if (cpustate >= state)
			cpuhp_issue_call(cpu, state, false, node);
	}

remove:
	hlist_del(node);
	mutex_unlock(&cpuhp_state_mutex);
	cpus_read_unlock();

	return 0;
}

 
void __cpuhp_remove_state_cpuslocked(enum cpuhp_state state, bool invoke)
{
	struct cpuhp_step *sp = cpuhp_get_step(state);
	int cpu;

	BUG_ON(cpuhp_cb_check(state));

	lockdep_assert_cpus_held();

	mutex_lock(&cpuhp_state_mutex);
	if (sp->multi_instance) {
		WARN(!hlist_empty(&sp->list),
		     "Error: Removing state %d which has instances left.\n",
		     state);
		goto remove;
	}

	if (!invoke || !cpuhp_get_teardown_cb(state))
		goto remove;

	 
	for_each_present_cpu(cpu) {
		struct cpuhp_cpu_state *st = per_cpu_ptr(&cpuhp_state, cpu);
		int cpustate = st->state;

		if (cpustate >= state)
			cpuhp_issue_call(cpu, state, false, NULL);
	}
remove:
	cpuhp_store_callbacks(state, NULL, NULL, NULL, false);
	mutex_unlock(&cpuhp_state_mutex);
}

void __cpuhp_remove_state(enum cpuhp_state state, bool invoke)
{
	cpus_read_lock();
	__cpuhp_remove_state_cpuslocked(state, invoke);
	cpus_read_unlock();
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

struct cpumask __cpu_dying_mask __read_mostly;

atomic_t __num_online_cpus __read_mostly;

void init_cpu_present(const struct cpumask *src)
{
	cpumask_copy(&__cpu_present_mask, src);
}

void init_cpu_possible(const struct cpumask *src)
{
	cpumask_copy(&__cpu_possible_mask, src);
}

void init_cpu_online(const struct cpumask *src)
{
	cpumask_copy(&__cpu_online_mask, src);
}

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

 
/* Stub: mitigations= cmdline option not needed for minimal kernel */
static int __init mitigations_parse_cmdline(char *arg) { return 0; }
early_param("mitigations", mitigations_parse_cmdline);

/* Stub: cpu_mitigations_off not used in minimal kernel */
bool cpu_mitigations_off(void)
{
	return false;
}

/* Stub: cpu_mitigations_auto_nosmt not used in minimal kernel */
bool cpu_mitigations_auto_nosmt(void)
{
	return false;
}
