#ifndef _LINUX_MMU_CONTEXT_H
#define _LINUX_MMU_CONTEXT_H
#include <asm/mmu_context.h>
#include <asm/mmu.h>
#ifndef switch_mm_irqs_off
#define switch_mm_irqs_off switch_mm
#endif
/* leave_mm, task_cpu_possible_mask, task_cpu_possible removed - unused */
#endif
