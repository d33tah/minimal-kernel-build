#ifndef _LINUX_EXTABLE_H
#define _LINUX_EXTABLE_H

struct exception_table_entry;

const struct exception_table_entry *search_exception_tables(unsigned long add);

#endif
