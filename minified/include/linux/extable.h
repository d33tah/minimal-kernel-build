/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_EXTABLE_H
#define _LINUX_EXTABLE_H

struct exception_table_entry;

/* Given an address, look for it in the exception tables */
const struct exception_table_entry *search_exception_tables(unsigned long add);

#endif
