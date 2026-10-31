
#include <linux/slab.h>
#include <linux/xarray.h>

static unsigned int get_offset(unsigned long index, struct xa_node *node)
{
	return (index >> node->shift) & XA_CHUNK_MASK;
}

static void xas_set_offset(struct xa_state *xas)
{
	xas->xa_offset = get_offset(xas->xa_index, xas->xa_node);
}

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
	if (xas_error(xas))
		return NULL;

	entry = xa_head(xas->xa);
	if (!xa_is_node(entry)) {
		if (xas->xa_index)
			return set_bounds(xas);
	} else {
		if ((xas->xa_index >> xa_to_node(entry)->shift) > XA_CHUNK_MASK)
			return set_bounds(xas);
	}

	xas->xa_node = NULL;
	return entry;
}

static void *xas_descend(struct xa_state *xas, struct xa_node *node)
{
	unsigned int offset = get_offset(xas->xa_index, node);
	void *entry = xa_entry(xas->xa, node, offset);

	xas->xa_node = node;
	/* CONFIG_XARRAY_MULTI off: xa_is_sibling() is compile-time false */
	xas->xa_offset = offset;
	return entry;
}

void *xas_load(struct xa_state *xas)
{
	void *entry = xas_start(xas);

	while (xa_is_node(entry)) {
		struct xa_node *node = xa_to_node(entry);

		if (xas->xa_shift > node->shift)
			break;
		entry = xas_descend(xas, node);
		if (node->shift == 0)
			break;
	}
	return entry;
}

static struct kmem_cache *xa_node_cachep;

static void xa_node_ctor(void *arg)
{
	memset(arg, 0, sizeof(struct xa_node));
}

void __init xarray_init(void)
{
	BUILD_BUG_ON(XA_CHUNK_SIZE > 255);
	xa_node_cachep = kmem_cache_create("xa_node", sizeof(struct xa_node), 0,
					   SLAB_PANIC | SLAB_RECLAIM_ACCOUNT,
					   xa_node_ctor);
}

bool xas_nomem(struct xa_state *xas, gfp_t gfp)
{
	if (xas->xa_node != XA_ERROR(-ENOMEM)) {
		struct xa_node *next, *node = xas->xa_alloc;

		while (node) {
			next = rcu_dereference_raw(node->parent);
			kmem_cache_free(xa_node_cachep, node);
			xas->xa_alloc = node = next;
		}
		return false;
	}
	if (xas->xa->xa_flags & XA_FLAGS_ACCOUNT)
		gfp |= __GFP_ACCOUNT;
	xas->xa_alloc = kmem_cache_alloc(xa_node_cachep, gfp);
	if (!xas->xa_alloc)
		return false;
	xas->xa_alloc->parent = NULL;
	xas->xa_node = XAS_RESTART;
	return true;
}

/*
 * A new node for xas_create/xas_expand: the one xas_nomem preallocated, else
 * a non-sleeping allocation (the caller holds the xa_lock; on failure
 * xas_nomem allocates with the caller's gfp and the store is retried).
 */
static void *xas_alloc(struct xa_state *xas, unsigned int shift)
{
	struct xa_node *parent = xas->xa_node;
	struct xa_node *node = xas->xa_alloc;

	if (xas_invalid(xas))
		return NULL;

	if (node) {
		xas->xa_alloc = NULL;
	} else {
		node = kmem_cache_alloc(xa_node_cachep,
					GFP_NOWAIT | __GFP_NOWARN);
		if (!node) {
			xas_set_err(xas, -ENOMEM);
			return NULL;
		}
	}

	if (parent) {
		node->offset = xas->xa_offset;
		parent->count++;
	}
	node->shift = shift;
	node->count = 0;
	RCU_INIT_POINTER(node->parent, xas->xa_node);
	return node;
}

static unsigned long max_index(void *entry)
{
	if (!xa_is_node(entry))
		return 0;
	return (XA_CHUNK_SIZE << xa_to_node(entry)->shift) - 1;
}

static int xas_expand(struct xa_state *xas, void *head)
{
	struct xarray *xa = xas->xa;
	struct xa_node *node = NULL;
	unsigned int shift = 0;
	unsigned long max = xas->xa_index;

	if (!head) {
		if (max == 0)
			return 0;
		while ((max >> shift) >= XA_CHUNK_SIZE)
			shift += XA_CHUNK_SHIFT;
		return shift + XA_CHUNK_SHIFT;
	} else if (xa_is_node(head)) {
		node = xa_to_node(head);
		shift = node->shift + XA_CHUNK_SHIFT;
	}
	xas->xa_node = NULL;

	while (max > max_index(head)) {
		node = xas_alloc(xas, shift);
		if (!node)
			return -ENOMEM;

		node->count = 1;
		RCU_INIT_POINTER(node->slots[0], head);

		if (xa_is_node(head)) {
			xa_to_node(head)->offset = 0;
			rcu_assign_pointer(xa_to_node(head)->parent, node);
		}
		head = xa_mk_node(node);
		rcu_assign_pointer(xa->xa_head, head);

		shift += XA_CHUNK_SHIFT;
	}

	xas->xa_node = node;
	return shift;
}

static void *xas_create(struct xa_state *xas)
{
	struct xarray *xa = xas->xa;
	void *entry;
	void __rcu **slot;
	struct xa_node *node = xas->xa_node;
	int shift;
	unsigned int order = xas->xa_shift;

	if (xas_top(node)) {
		entry = xa_head_locked(xa);
		xas->xa_node = NULL;
		shift = xas_expand(xas, entry);
		if (shift < 0)
			return NULL;
		entry = xa_head_locked(xa);
		slot = &xa->xa_head;
	} else if (xas_error(xas)) {
		return NULL;
	} else if (node) {
		unsigned int offset = xas->xa_offset;

		shift = node->shift;
		entry = xa_entry_locked(xa, node, offset);
		slot = &node->slots[offset];
	} else {
		shift = 0;
		entry = xa_head_locked(xa);
		slot = &xa->xa_head;
	}

	while (shift > order) {
		shift -= XA_CHUNK_SHIFT;
		if (!entry) {
			node = xas_alloc(xas, shift);
			if (!node)
				break;
			rcu_assign_pointer(*slot, xa_mk_node(node));
		} else if (xa_is_node(entry)) {
			node = xa_to_node(entry);
		} else {
			break;
		}
		entry = xas_descend(xas, node);
		slot = &node->slots[xas->xa_offset];
	}

	return entry;
}

/*
 * The only xarrays are page-cache mappings and the page cache only inserts
 * (no truncate, eviction or delete path exists), so a store never removes an
 * entry and nodes are never freed: no retry entries, no shrinking.
 */
void *xas_store(struct xa_state *xas, void *entry)
{
	struct xa_node *node;
	void __rcu **slot = &xas->xa->xa_head;
	void *first;

	if (WARN_ON_ONCE(!entry || xa_is_internal(entry)))
		return NULL;
	first = xas_create(xas);
	if (xas_invalid(xas) || first == entry)
		return first;
	node = xas->xa_node;
	if (node)
		slot = &node->slots[xas->xa_offset];
	rcu_assign_pointer(*slot, entry);
	if (node && !first)
		node->count++;
	return first;
}

void *__xas_next(struct xa_state *xas)
{
	void *entry;

	if (!xas_frozen(xas->xa_node))
		xas->xa_index++;
	if (!xas->xa_node)
		return set_bounds(xas);
	if (xas_not_node(xas->xa_node))
		return xas_load(xas);

	if (xas->xa_offset != get_offset(xas->xa_index, xas->xa_node))
		xas->xa_offset++;

	while (xas->xa_offset == XA_CHUNK_SIZE) {
		xas->xa_offset = xas->xa_node->offset + 1;
		xas->xa_node = xa_parent(xas->xa, xas->xa_node);
		if (!xas->xa_node)
			return set_bounds(xas);
	}

	for (;;) {
		entry = xa_entry(xas->xa, xas->xa_node, xas->xa_offset);
		if (!xa_is_node(entry))
			return entry;

		xas->xa_node = xa_to_node(entry);
		xas_set_offset(xas);
	}
}

/* Removed: xa_load, __xa_store, xa_store, __xa_cmpxchg, __xa_insert, __xa_alloc - no callers */
