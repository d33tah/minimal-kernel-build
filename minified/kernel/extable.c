#include <linux/elf.h>
#include <linux/memory.h>
#include <linux/extable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/filter.h>
#include <linux/hardirq.h>

#include <asm/sections.h>
#include <linux/uaccess.h>

static inline bool is_bpf_text_address(unsigned long addr)
{
	return false;
}

DEFINE_MUTEX(text_mutex);

extern struct exception_table_entry __start___ex_table[];
extern struct exception_table_entry __stop___ex_table[];

u32 __initdata __visible main_extable_sort_needed = 1;

void __init sort_main_extable(void)
{
	if (main_extable_sort_needed &&
	    &__stop___ex_table > &__start___ex_table) {
		sort_extable(__start___ex_table, __stop___ex_table);
	}
}

const
struct exception_table_entry *search_kernel_exception_table(unsigned long addr)
{
	return search_extable(__start___ex_table,
			      __stop___ex_table - __start___ex_table, addr);
}

const struct exception_table_entry *search_exception_tables(unsigned long addr)
{
	const struct exception_table_entry *e;

	e = search_kernel_exception_table(addr);
	if (!e)
		e = search_module_extables(addr);
	if (!e)
		e = search_bpf_extables(addr);
	return e;
}

int notrace core_kernel_text(unsigned long addr)
{
	if (is_kernel_text(addr))
		return 1;

	if (system_state < SYSTEM_FREEING_INITMEM &&
	    is_kernel_inittext(addr))
		return 1;
	return 0;
}

int __kernel_text_address(unsigned long addr)
{
	if (kernel_text_address(addr))
		return 1;
	 
	if (is_kernel_inittext(addr))
		return 1;
	return 0;
}

int kernel_text_address(unsigned long addr)
{
	bool no_rcu;
	int ret = 1;

	if (core_kernel_text(addr))
		return 1;

	 
	no_rcu = !rcu_is_watching();

	 
	if (no_rcu)
		rcu_nmi_enter();

	if (is_module_text_address(addr))
		goto out;
	if (is_ftrace_trampoline(addr))
		goto out;
	if (is_kprobe_optinsn_slot(addr) || is_kprobe_insn_slot(addr))
		goto out;
	if (is_bpf_text_address(addr))
		goto out;
	ret = 0;
out:
	if (no_rcu)
		rcu_nmi_exit();

	return ret;
}

/* Stub: func_ptr_is_kernel_text not used in minimal kernel */
int func_ptr_is_kernel_text(void *ptr)
{
	return 1;
}
