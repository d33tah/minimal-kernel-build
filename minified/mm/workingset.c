// SPDX-License-Identifier: GPL-2.0
/*
 * Workingset detection
 *
 * Copyright (C) 2013 Red Hat, Inc., Johannes Weiner
 *
 * STUBBED: Minimal implementation for tiny kernel
 */

#include <linux/memcontrol.h>
#include <linux/mm_inline.h>
#include <linux/writeback.h>
#include <linux/shmem_fs.h>
#include <linux/pagemap.h>
#include <linux/atomic.h>
#include <linux/module.h>
#include <linux/swap.h>
#include <linux/dax.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/list_lru.h>

/* Stub: No-op working set tracking */
void workingset_age_nonresident(struct lruvec *lruvec, unsigned long nr_pages)
{
	/* Do nothing */
}

void *workingset_eviction(struct folio *folio, struct mem_cgroup *target_memcg)
{
	return NULL;
}

void workingset_refault(struct folio *folio, void *shadow)
{
	/* Do nothing */
}

void workingset_activation(struct folio *folio)
{
	/* Do nothing */
}

void workingset_update_node(struct xa_node *node)
{
	/* Do nothing */
}

/* Global shadow nodes list - referenced by mm/filemap.c */
struct list_lru shadow_nodes;

static int __init workingset_init(void)
{
	int ret;

	ret = list_lru_init(&shadow_nodes);
	if (ret)
		return ret;

	return 0;
}
module_init(workingset_init);
