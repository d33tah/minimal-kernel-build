#define pr_fmt(fmt) "SMP alternatives: " fmt

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/perf_event.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/stringify.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/memory.h>
#include <linux/stop_machine.h>
#include <linux/slab.h>
#include <linux/kdebug.h>
#include <linux/kprobes.h>
#include <linux/mmu_context.h>
#include <asm/sync_core.h>
#include <asm/text-patching.h>
#include <asm/alternative.h>
#include <asm/sections.h>
#include <asm/mce.h>
#include <asm/nmi.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/io.h>
#include <asm/fixmap.h>
#include <asm/paravirt.h>
/* --- 2025-12-07 20:55 --- Inlined asm-prototypes.h */
#include <linux/uaccess.h>
#include <linux/pgtable.h>
#include <asm/string_32.h>
#include <asm/page.h>
/* checksum_32.h removed - not needed */
#include <asm/mce.h>
#include <asm/special_insns.h>
#include <asm/preempt.h>
#include <asm/asm.h>
/* --- end inlined asm-prototypes.h --- */

int __read_mostly alternatives_patched;

__ro_after_init struct mm_struct *poking_mm;
__ro_after_init unsigned long poking_addr;

void text_poke_early(void *addr, const void *opcode, size_t len);

void __init_or_module noinline apply_alternatives(struct alt_instr *start,
						  struct alt_instr *end)
{
}

/* apply_retpolines, apply_returns, apply_ibt_endbr removed - never called */

void __init alternative_instructions(void)
{
	alternatives_patched = 1;
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

void *text_poke(void *addr, const void *opcode, size_t len)
{
	return NULL;
}

/* text_poke_kgdb, text_poke_copy, text_poke_set, text_poke_sync,
   text_poke_queue, text_poke_finish, int3_exception_notify removed - unused */

void __ref text_poke_bp(void *addr, const void *opcode, size_t len, const void *emulate)
{
	text_poke_early(addr, opcode, len);
}

int poke_int3_handler(struct pt_regs *regs)
{
	return 0;
}
