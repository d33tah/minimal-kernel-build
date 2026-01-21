#ifndef _LINUX_EXTABLE_H
#define _LINUX_EXTABLE_H

#include <linux/stddef.h>	 
#include <linux/types.h>

/* struct module forward decl removed - unused */
struct exception_table_entry;

const struct exception_table_entry *
search_extable(const struct exception_table_entry *base,
	       const size_t num,
	       unsigned long value);
void sort_extable(struct exception_table_entry *start,
		  struct exception_table_entry *finish);
void sort_main_extable(void);

const struct exception_table_entry *search_exception_tables(unsigned long add);
/* search_kernel_exception_table inlined into search_exception_tables */
/* search_module_extables and search_bpf_extables removed - always NULL */

#endif  
