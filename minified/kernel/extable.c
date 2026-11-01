// SPDX-License-Identifier: GPL-2.0-or-later
/* Rewritten by Rusty Russell, on the backs of many others...
   Copyright (C) 2001 Rusty Russell, 2002 Rusty Russell IBM.

*/
#include <linux/extable.h>

#include <linux/uaccess.h>

extern struct exception_table_entry __start___ex_table[];
extern struct exception_table_entry __stop___ex_table[];

/*
 * Linear scan: the table has a few dozen entries (63 today) and is only
 * searched when a kernel instruction faults, so it is neither sorted at boot
 * nor binary-searched. insn is a self-relative offset.
 */
const struct exception_table_entry *search_exception_tables(unsigned long addr)
{
	const struct exception_table_entry *e;

	for (e = __start___ex_table; e < __stop___ex_table; e++)
		if ((unsigned long)&e->insn + e->insn == addr)
			return e;
	return NULL;
}
