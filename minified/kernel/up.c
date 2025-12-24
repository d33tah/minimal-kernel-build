#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/smp.h>
int smp_call_function_single(int cpu, void (*func)(void *info), void *info,
			     int wait)
{
	unsigned long flags;
	if (cpu != 0)
		return -ENXIO;
	local_irq_save(flags);
	func(info);
	local_irq_restore(flags);
	return 0;
}
void on_each_cpu_cond_mask(smp_cond_func_t cond_func, smp_call_func_t func,
			   void *info, bool wait, const struct cpumask *mask)
{
	unsigned long flags;
	preempt_disable();
	if ((!cond_func || cond_func(0, info)) && cpumask_test_cpu(0, mask)) {
		local_irq_save(flags);
		func(info);
		local_irq_restore(flags);
	}
	preempt_enable();
}
