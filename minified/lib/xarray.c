#include <linux/slab.h>
#include <linux/xarray.h>

static void *set_bounds(struct xa_state *xas)
{
	xas->xa_node = XAS_BOUNDS;
	return NULL;
}

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
	rcu_assign_pointer(*slot, entry);
	return first;
}

void *__xas_next(struct xa_state *xas)
{
	if (!xas_frozen(xas->xa_node))
		xas->xa_index++;
	if (!xas->xa_node)
		return set_bounds(xas);
	return xas_load(xas);
}
