#ifndef _LINUX_RMAP_H
#define _LINUX_RMAP_H

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/memcontrol.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>

void page_add_new_anon_rmap(struct page *, struct vm_area_struct *,
			    unsigned long address);
void page_add_file_rmap(struct page *, struct vm_area_struct *, bool compound);

#endif
