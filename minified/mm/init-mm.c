#include <linux/mm_types.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/mman.h>

#include <linux/user_namespace.h>

#include <asm/mmu.h>

#ifndef INIT_MM_CONTEXT
#define INIT_MM_CONTEXT(name)
#endif

struct mm_struct init_mm = {
	.mm_rb		= RB_ROOT,
	.pgd		= swapper_pg_dir,
	.mm_users	= ATOMIC_INIT(2),
	.mm_count	= ATOMIC_INIT(1),
	MMAP_LOCK_INITIALIZER(init_mm)
	.page_table_lock =  __SPIN_LOCK_UNLOCKED(init_mm.page_table_lock),
	.user_ns	= &init_user_ns,
	.cpu_bitmap	= CPU_BITS_NONE,
	INIT_MM_CONTEXT(init_mm)
};

void setup_initial_init_mm(void *start_code, void *end_code,
			   void *end_data, void *brk)
{
	init_mm.brk = (unsigned long)brk;
}
