// SPDX-License-Identifier: GPL-2.0+
/*
 * XArray implementation
 * Copyright (c) 2017-2018 Microsoft Corporation
 * Copyright (c) 2018-2020 Oracle
 * Author: Matthew Wilcox <willy@infradead.org>
 */

#include <linux/slab.h>
#include <linux/xarray.h>

static void *set_bounds(struct xa_state *xas)
{
	xas->xa_node = XAS_BOUNDS;
	return NULL;
}

/*
 * Starts a walk.  If the @xas is already valid, we assume that it's on
 * the right path and just return where we've got to.  If we're in an
 * error state, return NULL.  If the index is outside the current scope
 * of the xarray, return NULL without changing @xas->xa_node.  Otherwise
 * set @xas->xa_node to NULL and return the current head of the array.
 */
static void *xas_start(struct xa_state *xas)
{
	void *entry;

	if (xas_valid(xas))
		return xas_reload(xas);

	entry = xa_head(xas->xa);
	if (xas->xa_index)
		return set_bounds(xas);

	xas->xa_node = NULL;
	return entry;
}

/**
 * xas_load() - Load an entry from the XArray (advanced).
 * @xas: XArray operation state.
 *
 * Usually walks the @xas to the appropriate state to load the entry
 * stored at xa_index.  However, it will do nothing and return %NULL if
 * @xas is in an error state.  xas_load() will never expand the tree.
 *
 * If the xa_state is set up to operate on a multi-index entry, xas_load()
 * may return %NULL or an internal entry, even if there are entries
 * present within the range specified by @xas.
 *
 * Context: Any context.  The caller should hold the xa_lock or the RCU lock.
 * Return: Usually an entry in the XArray, but see description for exceptions.
 */
void *xas_load(struct xa_state *xas)
{
	void *entry = xas_start(xas);

	return entry;
}

/*
 * A page-cache xarray holds one folio, in the head slot: ramfs files are at
 * most one page (s_maxbytes == PAGE_SIZE), so no node is ever needed and
 * there is no node allocator. A store at index > 0 would need one and is a
 * bug.
 */
static void *xas_create(struct xa_state *xas)
{
	struct xarray *xa = xas->xa;
	void *entry;
	struct xa_node *node = xas->xa_node;

	if (xas_top(node)) {
		entry = xa_head_locked(xa);
		xas->xa_node = NULL;
		BUG_ON(xas->xa_index);
	} else {
		entry = xa_head_locked(xa);
	}

	return entry;
}

/*
 * The only xarrays are page-cache mappings and the page cache only inserts
 * (no truncate, eviction or delete path exists), so a store never removes an
 * entry: no retry entries, no shrinking.
 */
void *xas_store(struct xa_state *xas, void *entry)
{
	void __rcu **slot = &xas->xa->xa_head;
	void *first;

	if (WARN_ON_ONCE(!entry || xa_is_internal(entry)))
		return NULL;
	first = xas_create(xas);
	if (xas_invalid(xas) || first == entry)
		return first;
	/*
		 * Must clear the marks before setting the entry to NULL,
		 * otherwise xas_for_each_marked may find a NULL entry and
		 * stop early.  rcu_assign_pointer contains a release barrier
		 * so the mark clearing will appear to happen before the
		 * entry is set to NULL.
		 */
	rcu_assign_pointer(*slot, entry);
	return first;
}

/*
 * __xas_next() - Find the next entry in the XArray.
 * @xas: XArray operation state.
 *
 * Helper function for xas_next() which handles all the complex cases
 * out of line.
 */
void *__xas_next(struct xa_state *xas)
{
	if (!xas_frozen(xas->xa_node))
		xas->xa_index++;
	if (!xas->xa_node)
		return set_bounds(xas);
	return xas_load(xas);
}
