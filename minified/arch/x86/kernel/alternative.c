#define pr_fmt(fmt) "SMP alternatives: " fmt

#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/stringify.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/memory.h>
#include <linux/stop_machine.h>
#include <linux/mmu_context.h>
#include <asm/sync_core.h>
#include <asm/text-patching.h>
#include <asm/alternative.h>
#include <asm/sections.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/io.h>
#include <asm/fixmap.h>
/* --- 2025-12-07 20:55 --- Inlined asm-prototypes.h */
#include <linux/pgtable.h>
#include <asm/string_32.h>
#include <asm/page.h>
#include <asm/special_insns.h>
#include <asm/preempt.h>
#include <asm/asm.h>

int __read_mostly alternatives_patched;

__ro_after_init struct mm_struct *poking_mm;
__ro_after_init unsigned long poking_addr;

void __init alternative_instructions(void)
{
	alternatives_patched = 1;
}

/* text_poke_early, text_poke, text_poke_bp, text_poke_kgdb, text_poke_copy,
   text_poke_set, text_poke_sync, text_poke_queue, text_poke_finish,
   int3_exception_notify removed - unused */
