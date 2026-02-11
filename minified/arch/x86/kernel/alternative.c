#include <linux/module.h>
#include <linux/sched.h>
/* linux/mutex.h removed - no mutex functions */
#include <linux/list.h>
#include <linux/stringify.h>
/* linux/highmem.h, linux/vmalloc.h removed - unused */
#include <linux/mm.h>
/* linux/stop_machine.h, linux/slab.h, linux/mmu_context.h removed - unused */
#include <asm/sync_core.h>
/* Inlined from text-patching.h */
#define __parainstructions NULL
#define __parainstructions_end NULL
extern void text_poke_early(void *addr, const void *opcode, size_t len);
extern int after_bootmem;
extern __ro_after_init struct mm_struct *poking_mm;
extern __ro_after_init unsigned long poking_addr;
#include <asm/alternative.h>
#include <asm/sections.h>
/* mce.h removed - header is empty */
#include <asm/nmi.h>
#include <asm/tlbflush.h>
#include <asm/io.h>
#include <asm/fixmap.h>
/* paravirt.h removed - header is empty */
/* --- 2025-12-07 20:55 --- Inlined asm-prototypes.h */
#include <linux/uaccess.h>
#include <linux/pgtable.h>
#include <asm/string_32.h>
#include <asm/page.h>
/* mce.h removed - header is empty */
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
/* alternative_instructions removed - inlined into check_bugs (~4 LOC) */

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

/* text_poke, text_poke_kgdb, text_poke_copy, text_poke_set, text_poke_sync,
   text_poke_queue, text_poke_finish, int3_exception_notify, text_poke_bp,
   poke_int3_handler removed - never true or unused */
