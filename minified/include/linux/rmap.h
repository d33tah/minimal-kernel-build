/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RMAP_H
#define _LINUX_RMAP_H

#include <linux/pagemap.h>

void page_add_new_anon_rmap(struct page *, struct vm_area_struct *,
			    unsigned long address);
void page_add_file_rmap(struct page *, struct vm_area_struct *, bool compound);
void page_remove_rmap(struct page *);

#endif
