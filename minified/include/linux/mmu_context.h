#ifndef _LINUX_MMU_CONTEXT_H
#define _LINUX_MMU_CONTEXT_H
#include <asm/mmu_context.h>
#include <asm/mmu.h>
#ifndef switch_mm_irqs_off
#define switch_mm_irqs_off switch_mm
#endif
#ifndef leave_mm
static inline void leave_mm(int cpu) { }
#endif
#define task_cpu_possible_mask(p) cpu_possible_mask
#define task_cpu_possible(cpu, p) true
#endif
