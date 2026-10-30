#define pr_fmt(fmt) "SMP alternatives: " fmt

#include <linux/mm.h>

void __init alternative_instructions(void)
{
}

/* text_poke_early, text_poke, text_poke_bp, text_poke_kgdb, text_poke_copy,
   text_poke_set, text_poke_sync, text_poke_queue, text_poke_finish,
   int3_exception_notify removed - unused */
