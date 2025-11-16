 
#ifndef __LINUX_SMP_H
#define __LINUX_SMP_H

 

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/smp_types.h>

typedef void (*smp_call_func_t)(void *info);
typedef bool (*smp_cond_func_t)(int cpu, void *info);

 
struct __call_single_data {
	struct __call_single_node node;
	smp_call_func_t func;
	void *info;
};

#define CSD_INIT(_func, _info) \
	(struct __call_single_data){ .func = (_func), .info = (_info), }

 
typedef struct __call_single_data call_single_data_t
	__aligned(sizeof(struct __call_single_data));

#define INIT_CSD(_csd, _func, _info)		\
do {						\
	*(_csd) = CSD_INIT((_func), (_info));	\
} while (0)

 
extern void __smp_call_single_queue(int cpu, struct llist_node *node);

 
extern unsigned int total_cpus;

int smp_call_function_single(int cpuid, smp_call_func_t func, void *info,
			     int wait);

void on_each_cpu_cond_mask(smp_cond_func_t cond_func, smp_call_func_t func,
			   void *info, bool wait, const struct cpumask *mask);

int smp_call_function_single_async(int cpu, struct __call_single_data *csd);

 
void panic_smp_self_stop(void);
void nmi_panic_self_stop(struct pt_regs *regs);
void crash_smp_send_stop(void);

 
static inline void on_each_cpu(smp_call_func_t func, void *info, int wait)
{
	on_each_cpu_cond_mask(NULL, func, info, wait, cpu_online_mask);
}

 
static inline void on_each_cpu_mask(const struct cpumask *mask,
				    smp_call_func_t func, void *info, bool wait)
{
	on_each_cpu_cond_mask(NULL, func, info, wait, mask);
}

 
static inline void on_each_cpu_cond(smp_cond_func_t cond_func,
				    smp_call_func_t func, void *info, bool wait)
{
	on_each_cpu_cond_mask(cond_func, func, info, wait, cpu_online_mask);
}


static inline void smp_send_stop(void) { }

 
#define raw_smp_processor_id()			0
static inline void up_smp_call_function(smp_call_func_t func, void *info)
{
}
#define smp_call_function(func, info, wait) \
			(up_smp_call_function(func, info))

static inline void smp_send_reschedule(int cpu) { }
#define smp_prepare_boot_cpu()			do {} while (0)
#define smp_call_function_many(mask, func, info, wait) \
			(up_smp_call_function(func, info))
static inline void call_function_init(void) { }

static inline int
smp_call_function_any(const struct cpumask *mask, smp_call_func_t func,
		      void *info, int wait)
{
	return smp_call_function_single(0, func, info, wait);
}

static inline void kick_all_cpus_sync(void) {  }
static inline void wake_up_all_idle_cpus(void) {  }

static inline void smp_init(void) { }

static inline int get_boot_cpu_id(void)
{
	return 0;
}


 

 

 
#ifndef __smp_processor_id
#define __smp_processor_id(x) raw_smp_processor_id(x)
#endif

# define smp_processor_id() __smp_processor_id()

#define get_cpu()		({ preempt_disable(); __smp_processor_id(); })
#define put_cpu()		preempt_enable()

 
extern void arch_disable_smp_support(void);

extern void arch_thaw_secondary_cpus_begin(void);
extern void arch_thaw_secondary_cpus_end(void);

void smp_setup_processor_id(void);

int smp_call_on_cpu(unsigned int cpu, int (*func)(void *), void *par,
		    bool phys);

 
int smpcfd_prepare_cpu(unsigned int cpu);
int smpcfd_dead_cpu(unsigned int cpu);
int smpcfd_dying_cpu(unsigned int cpu);

#endif  
