 
#ifndef _ASM_X86_IRQ_VECTORS_H
#define _ASM_X86_IRQ_VECTORS_H

#include <linux/threads.h>
 


#define FIRST_EXTERNAL_VECTOR		0x20

#define IA32_SYSCALL_VECTOR		0x80

 
#define ISA_IRQ_VECTOR(irq)		(((FIRST_EXTERNAL_VECTOR + 16) & ~15) + irq)

 

#define SPURIOUS_APIC_VECTOR		0xff
 
#if ((SPURIOUS_APIC_VECTOR & 0x0F) != 0x0F)
# error SPURIOUS_APIC_VECTOR definition error
#endif


#define HYPERVISOR_CALLBACK_VECTOR	0xf3

#define NR_VECTORS			 256

#define FIRST_SYSTEM_VECTOR		NR_VECTORS

#define NR_EXTERNAL_VECTORS		(FIRST_SYSTEM_VECTOR - FIRST_EXTERNAL_VECTOR)



#define NR_IRQS_LEGACY			16

#define NR_IRQS				NR_IRQS_LEGACY

#endif  
