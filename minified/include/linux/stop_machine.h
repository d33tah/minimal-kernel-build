#ifndef _LINUX_STOP_MACHINE
#define _LINUX_STOP_MACHINE

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/smp.h>
#include <linux/list.h>

typedef int (*cpu_stop_fn_t)(void *arg);


#include <linux/workqueue.h>

struct cpu_stop_work {
	struct work_struct	work;
	cpu_stop_fn_t		fn;
	void			*arg;
};

static inline int stop_one_cpu(unsigned int cpu, cpu_stop_fn_t fn, void *arg)
{
	int ret = -ENOENT;
	preempt_disable();
	if (cpu == smp_processor_id())
		ret = fn(arg);
	preempt_enable();
	return ret;
}

static void stop_one_cpu_nowait_workfn(struct work_struct *work)
{
	struct cpu_stop_work *stwork =
		container_of(work, struct cpu_stop_work, work);
	preempt_disable();
	stwork->fn(stwork->arg);
	preempt_enable();
}

static inline bool stop_one_cpu_nowait(unsigned int cpu,
				       cpu_stop_fn_t fn, void *arg,
				       struct cpu_stop_work *work_buf)
{
	if (cpu == smp_processor_id()) {
		INIT_WORK(&work_buf->work, stop_one_cpu_nowait_workfn);
		work_buf->fn = fn;
		work_buf->arg = arg;
		schedule_work(&work_buf->work);
		return true;
	}

	return false;
}

static inline void print_stop_info(const char *log_lvl, struct task_struct *task) { }



static __always_inline int stop_machine_cpuslocked(cpu_stop_fn_t fn, void *data,
					  const struct cpumask *cpus)
{
	unsigned long flags;
	int ret;
	local_irq_save(flags);
	ret = fn(data);
	local_irq_restore(flags);
	return ret;
}

static __always_inline int
stop_machine(cpu_stop_fn_t fn, void *data, const struct cpumask *cpus)
{
	return stop_machine_cpuslocked(fn, data, cpus);
}

static __always_inline int
stop_machine_from_inactive_cpu(cpu_stop_fn_t fn, void *data,
			       const struct cpumask *cpus)
{
	return stop_machine(fn, data, cpus);
}

#endif	 
