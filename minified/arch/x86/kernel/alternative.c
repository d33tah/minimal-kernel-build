#include <linux/module.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/stringify.h>
#include <linux/mm.h>
static inline void iret_to_self(void)
{
	asm volatile("pushfl\n\t"
		     "pushl %%cs\n\t"
		     "pushl $1f\n\t"
		     "iret\n\t"
		     "1:"
		     : ASM_CALL_CONSTRAINT
		     :
		     : "memory");
}
static inline void sync_core(void)
{
	if (static_cpu_has(X86_FEATURE_SERIALIZE)) {
		serialize();
		return;
	}
	iret_to_self();
}
/* end sync_core.h */
#define __parainstructions NULL
#define __parainstructions_end NULL
extern void text_poke_early(void *addr, const void *opcode, size_t len);
extern int after_bootmem;
extern __ro_after_init struct mm_struct *poking_mm;
extern __ro_after_init unsigned long poking_addr;
#include <asm/alternative.h>
#include <asm/sections.h>
#include <asm/nmi.h>
#include <asm/tlbflush.h>
#include <asm/io.h>
#include <asm/fixmap.h>
#include <linux/uaccess.h>
#include <linux/pgtable.h>
#include <asm/page.h>
#include <asm/special_insns.h>
#include <asm/preempt.h>
#include <asm/asm.h>

int __read_mostly alternatives_patched;

__ro_after_init struct mm_struct *poking_mm;
__ro_after_init unsigned long poking_addr;

void text_poke_early(void *addr, const void *opcode, size_t len);

void __init_or_module noinline apply_alternatives(struct alt_instr *start,
						  struct alt_instr *end)
{
}

void __init_or_module text_poke_early(void *addr, const void *opcode,
				      size_t len)
{
	unsigned long flags;

	if (len > sizeof(long))
		len = sizeof(long);

	local_irq_save(flags);
	memcpy(addr, opcode, len);
	local_irq_restore(flags);
	sync_core();
}
