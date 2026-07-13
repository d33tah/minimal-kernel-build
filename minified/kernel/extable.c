#include <linux/extable.h>
#include <linux/init.h>

#include <linux/uaccess.h>

extern struct exception_table_entry __start___ex_table[];
extern struct exception_table_entry __stop___ex_table[];

void __init sort_main_extable(void) {
	if (&__stop___ex_table > &__start___ex_table) {
		sort_extable(__start___ex_table, __stop___ex_table); } }

const struct exception_table_entry *search_exception_tables(unsigned long addr) {
	const struct exception_table_entry *e;

	e = search_extable(__start___ex_table, __stop___ex_table - __start___ex_table, addr);
	return e; }

