 
#ifndef __LINUX_HYPEVISOR_H
#define __LINUX_HYPEVISOR_H

 


#include <asm/jailhouse_para.h>
#include <asm/x86_init.h>

static inline void hypervisor_pin_vcpu(int cpu)
{
	x86_platform.hyper.pin_vcpu(cpu);
}


#endif  
