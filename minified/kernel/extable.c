#include <linux/elf.h>
#include <linux/memory.h>
#include <linux/extable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/hardirq.h>
#include <linux/kallsyms.h>

#include <asm/sections.h>
#include <linux/uaccess.h>

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

const struct exception_table_entry *search_exception_tables(unsigned long addr)
{
	const struct exception_table_entry *e;

	e = search_extable(__start___ex_table,
			   __stop___ex_table - __start___ex_table, addr);
	return e;
}

