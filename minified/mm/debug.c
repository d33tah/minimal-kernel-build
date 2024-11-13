// SPDX-License-Identifier: GPL-2.0
/*
 * mm/debug.c
 *
 * mm/ specific debug routines.
 *
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/trace_events.h>
#include <linux/memcontrol.h>
#include <trace/events/mmflags.h>
#include <linux/migrate.h>
#include <linux/page_owner.h>
#include <linux/ctype.h>

#include "internal.h"
#include <trace/events/migrate.h>

/*
 * Define EM() and EMe() so that MIGRATE_REASON from trace/events/migrate.h can
 * be used to populate migrate_reason_names[].
 */
#undef EM
#undef EMe
#define EM(a, b)	b,
#define EMe(a, b)	b

const char *migrate_reason_names[MR_TYPES] = {
	MIGRATE_REASON
};

const struct trace_print_flags pageflag_names[] = {
	__def_pageflag_names,
	{0, NULL}
};

const struct trace_print_flags gfpflag_names[] = {
	__def_gfpflag_names,
	{0, NULL}
};

const struct trace_print_flags vmaflag_names[] = {
	__def_vmaflag_names,
	{0, NULL}
};

static void __dump_page(struct page *page)
{
	struct folio *folio = page_folio(page);
	struct page *head = &folio->page;
	struct address_space *mapping;
	bool compound = PageCompound(page);
	/*
	 * Accessing the pageblock without the zone lock. It could change to
	 * "isolate" again in the meantime, but since we are just dumping the
	 * state for debugging, it should be fine to accept a bit of
	 * inaccuracy here due to racing.
	 */
	bool page_cma = is_migrate_cma_page(page);
	int mapcount;
	char *type = "";

	if (page < head || (page >= head + MAX_ORDER_NR_PAGES)) {
		/*
		 * Corrupt page, so we cannot call page_mapping. Instead, do a
		 * safe subset of the steps that page_mapping() does. Caution:
		 * this will be misleading for tail pages, PageSwapCache pages,
		 * and potentially other situations. (See the page_mapping()
		 * implementation for what's missing here.)
		 */
		unsigned long tmp = (unsigned long)page->mapping;

		if (tmp & PAGE_MAPPING_ANON)
			mapping = NULL;
		else
			mapping = (void *)(tmp & ~PAGE_MAPPING_FLAGS);
		head = page;
		folio = (struct folio *)page;
		compound = false;
	} else {
		mapping = page_mapping(page);
	}

	/*
	 * Avoid VM_BUG_ON() in page_mapcount().
	 * page->_mapcount space in struct page is used by sl[aou]b pages to
	 * encode own info.
	 */
	mapcount = PageSlab(head) ? 0 : page_mapcount(page);

	pr_warn("page:%p refcount:%d mapcount:%d mapping:%p index:%#lx pfn:%#lx\n",
			page, page_ref_count(head), mapcount, mapping,
			page_to_pgoff(page), page_to_pfn(page));
	if (compound) {
		pr_warn("head:%p order:%u compound_mapcount:%d compound_pincount:%d\n",
				head, compound_order(head),
				folio_entire_mapcount(folio),
				head_compound_pincount(head));
	}

#ifdef CONFIG_MEMCG
	if (head->memcg_data)
		pr_warn("memcg:%lx\n", head->memcg_data);
#endif
	if (PageKsm(page))
		type = "ksm ";
	else if (PageAnon(page))
		type = "anon ";
	else if (mapping)
		dump_mapping(mapping);
	BUILD_BUG_ON(ARRAY_SIZE(pageflag_names) != __NR_PAGEFLAGS + 1);

	pr_warn("%sflags: %pGp%s\n", type, &head->flags,
		page_cma ? " CMA" : "");
	print_hex_dump(KERN_WARNING, "raw: ", DUMP_PREFIX_NONE, 32,
			sizeof(unsigned long), page,
			sizeof(struct page), false);
	if (head != page)
		print_hex_dump(KERN_WARNING, "head: ", DUMP_PREFIX_NONE, 32,
			sizeof(unsigned long), head,
			sizeof(struct page), false);
}

void dump_page(struct page *page, const char *reason)
{
	if (PagePoisoned(page))
		pr_warn("page:%p is uninitialized and poisoned", page);
	else
		__dump_page(page);
	if (reason)
		pr_warn("page dumped because: %s\n", reason);
	dump_page_owner(page);
}
EXPORT_SYMBOL(dump_page);

