 
 
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/kthread.h>
#include <linux/smpboot.h>

#include "smpboot.h"

 
int smpboot_create_threads(unsigned int cpu)
{
	return 0;
}

 
int smpboot_unpark_threads(unsigned int cpu)
{
	return 0;
}

 
int smpboot_park_threads(unsigned int cpu)
{
	return 0;
}

 
int smpboot_register_percpu_thread(struct smp_hotplug_thread *plug_thread)
{
	return 0;
}

 
void smpboot_unregister_percpu_thread(struct smp_hotplug_thread *plug_thread)
{
}

 
int cpu_report_state(int cpu)
{
	return 0;
}

 
int cpu_check_up_prepare(int cpu)
{
	return 0;
}

 
void cpu_set_state_online(int cpu)
{
}

 
void cpu_report_death(void)
{
}

 
bool cpu_wait_death(unsigned int cpu, int seconds)
{
	return true;
}

 
bool cpu_reports_death(unsigned int cpu)
{
	return true;
}
