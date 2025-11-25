
#include <linux/bitmap.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/xarray.h>

static inline unsigned int xa_lock_type(const struct xarray *xa)
{
	return (__force unsigned int)xa->xa_flags & 3;
}

static inline void xas_lock_type(struct xa_state *xas, unsigned int lock_type)
{
	if (lock_type == XA_LOCK_IRQ)
		xas_lock_irq(xas);
	else if (lock_type == XA_LOCK_BH)
		xas_lock_bh(xas);
	else
		xas_lock(xas);
}

static inline void xas_unlock_type(struct xa_state *xas, unsigned int lock_type)
{
	if (lock_type == XA_LOCK_IRQ)
		xas_unlock_irq(xas);
	else if (lock_type == XA_LOCK_BH)
		xas_unlock_bh(xas);
	else
		xas_unlock(xas);
}

static inline bool xa_track_free(const struct xarray *xa)
{
	return xa->xa_flags & XA_FLAGS_TRACK_FREE;
}

static inline bool xa_zero_busy(const struct xarray *xa)
{
	return xa->xa_flags & XA_FLAGS_ZERO_BUSY;
}

static inline void xa_mark_set(struct xarray *xa, xa_mark_t mark)
{
	if (!(xa->xa_flags & XA_FLAGS_MARK(mark)))
		xa->xa_flags |= XA_FLAGS_MARK(mark);
}

static inline void xa_mark_clear(struct xarray *xa, xa_mark_t mark)
{
	if (xa->xa_flags & XA_FLAGS_MARK(mark))
		xa->xa_flags &= ~(XA_FLAGS_MARK(mark));
}

static inline unsigned long *node_marks(struct xa_node *node, xa_mark_t mark)
{
	return node->marks[(__force unsigned)mark];
}

static inline bool node_get_mark(struct xa_node *node,
		unsigned int offset, xa_mark_t mark)
{
	return test_bit(offset, node_marks(node, mark));
}

static inline bool node_set_mark(struct xa_node *node, unsigned int offset,
				xa_mark_t mark)
{
	return __test_and_set_bit(offset, node_marks(node, mark));
}

static inline bool node_clear_mark(struct xa_node *node, unsigned int offset,
				xa_mark_t mark)
{
	return __test_and_clear_bit(offset, node_marks(node, mark));
}

static inline bool node_any_mark(struct xa_node *node, xa_mark_t mark)
{
	return !bitmap_empty(node_marks(node, mark), XA_CHUNK_SIZE);
}

static inline void node_mark_all(struct xa_node *node, xa_mark_t mark)
{
	bitmap_fill(node_marks(node, mark), XA_CHUNK_SIZE);
}

#define mark_inc(mark) do { \
	mark = (__force xa_mark_t)((__force unsigned)(mark) + 1); \
} while (0)

static void xas_squash_marks(const struct xa_state *xas)
{
	unsigned int mark = 0;
	unsigned int limit = xas->xa_offset + xas->xa_sibs + 1;

	if (!xas->xa_sibs)
		return;

	do {
		unsigned long *marks = xas->xa_node->marks[mark];
		if (find_next_bit(marks, limit, xas->xa_offset + 1) == limit)
			continue;
		__set_bit(xas->xa_offset, marks);
		bitmap_clear(marks, xas->xa_offset + 1, xas->xa_sibs);
	} while (mark++ != (__force unsigned)XA_MARK_MAX);
}

static unsigned int get_offset(unsigned long index, struct xa_node *node)
{
	return (index >> node->shift) & XA_CHUNK_MASK;
}

static void xas_set_offset(struct xa_state *xas)
{
	xas->xa_offset = get_offset(xas->xa_index, xas->xa_node);
}

static void xas_move_index(struct xa_state *xas, unsigned long offset)
{
	unsigned int shift = xas->xa_node->shift;
	xas->xa_index &= ~XA_CHUNK_MASK << shift;
	xas->xa_index += offset << shift;
}

static void xas_next_offset(struct xa_state *xas)
{
	xas->xa_offset++;
	xas_move_index(xas, xas->xa_offset);
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
	if (xa_is_sibling(entry)) {
		offset = xa_to_sibling(entry);
		entry = xa_entry(xas->xa, node, offset);
		if (node->shift && xa_is_node(entry))
			entry = XA_RETRY_ENTRY;
	}

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

extern struct kmem_cache *radix_tree_node_cachep;
extern void radix_tree_node_rcu_free(struct rcu_head *head);

#define XA_RCU_FREE	((struct xarray *)1)

static void xa_node_free(struct xa_node *node)
{
	XA_NODE_BUG_ON(node, !list_empty(&node->private_list));
	node->array = XA_RCU_FREE;
	call_rcu(&node->rcu_head, radix_tree_node_rcu_free);
}

void xas_destroy(struct xa_state *xas)
{
	struct xa_node *next, *node = xas->xa_alloc;

	while (node) {
		XA_NODE_BUG_ON(node, !list_empty(&node->private_list));
		next = rcu_dereference_raw(node->parent);
		radix_tree_node_rcu_free(&node->rcu_head);
		xas->xa_alloc = node = next;
	}
}

bool xas_nomem(struct xa_state *xas, gfp_t gfp)
{
	if (xas->xa_node != XA_ERROR(-ENOMEM)) {
		xas_destroy(xas);
		return false;
	}
	if (xas->xa->xa_flags & XA_FLAGS_ACCOUNT)
		gfp |= __GFP_ACCOUNT;
	xas->xa_alloc = kmem_cache_alloc_lru(radix_tree_node_cachep, xas->xa_lru, gfp);
	if (!xas->xa_alloc)
		return false;
	xas->xa_alloc->parent = NULL;
	XA_NODE_BUG_ON(xas->xa_alloc, !list_empty(&xas->xa_alloc->private_list));
	xas->xa_node = XAS_RESTART;
	return true;
}

static void xas_update(struct xa_state *xas, struct xa_node *node)
{
	if (xas->xa_update)
		xas->xa_update(node);
	else
		XA_NODE_BUG_ON(node, !list_empty(&node->private_list));
}

static void *xas_alloc(struct xa_state *xas, unsigned int shift)
{
	struct xa_node *parent = xas->xa_node;
	struct xa_node *node = xas->xa_alloc;

	if (xas_invalid(xas))
		return NULL;

	if (node) {
		xas->xa_alloc = NULL;
	} else {
		gfp_t gfp = GFP_NOWAIT | __GFP_NOWARN;

		if (xas->xa->xa_flags & XA_FLAGS_ACCOUNT)
			gfp |= __GFP_ACCOUNT;

		node = kmem_cache_alloc_lru(radix_tree_node_cachep, xas->xa_lru, gfp);
		if (!node) {
			xas_set_err(xas, -ENOMEM);
			return NULL;
		}
	}

	if (parent) {
		node->offset = xas->xa_offset;
		parent->count++;
		XA_NODE_BUG_ON(node, parent->count > XA_CHUNK_SIZE);
		xas_update(xas, parent);
	}
	XA_NODE_BUG_ON(node, shift > BITS_PER_LONG);
	XA_NODE_BUG_ON(node, !list_empty(&node->private_list));
	node->shift = shift;
	node->count = 0;
	node->nr_values = 0;
	RCU_INIT_POINTER(node->parent, xas->xa_node);
	node->array = xas->xa;

	return node;
}

static unsigned long xas_max(struct xa_state *xas)
{
	unsigned long max = xas->xa_index;

	return max;
}

static unsigned long max_index(void *entry)
{
	if (!xa_is_node(entry))
		return 0;
	return (XA_CHUNK_SIZE << xa_to_node(entry)->shift) - 1;
}

static void xas_shrink(struct xa_state *xas)
{
	struct xarray *xa = xas->xa;
	struct xa_node *node = xas->xa_node;

	for (;;) {
		void *entry;

		XA_NODE_BUG_ON(node, node->count > XA_CHUNK_SIZE);
		if (node->count != 1)
			break;
		entry = xa_entry_locked(xa, node, 0);
		if (!entry)
			break;
		if (!xa_is_node(entry) && node->shift)
			break;
		if (xa_is_zero(entry) && xa_zero_busy(xa))
			entry = NULL;
		xas->xa_node = XAS_BOUNDS;

		RCU_INIT_POINTER(xa->xa_head, entry);
		if (xa_track_free(xa) && !node_get_mark(node, 0, XA_FREE_MARK))
			xa_mark_clear(xa, XA_FREE_MARK);

		node->count = 0;
		node->nr_values = 0;
		if (!xa_is_node(entry))
			RCU_INIT_POINTER(node->slots[0], XA_RETRY_ENTRY);
		xas_update(xas, node);
		xa_node_free(node);
		if (!xa_is_node(entry))
			break;
		node = xa_to_node(entry);
		node->parent = NULL;
	}
}

static void xas_delete_node(struct xa_state *xas)
{
	struct xa_node *node = xas->xa_node;

	for (;;) {
		struct xa_node *parent;

		XA_NODE_BUG_ON(node, node->count > XA_CHUNK_SIZE);
		if (node->count)
			break;

		parent = xa_parent_locked(xas->xa, node);
		xas->xa_node = parent;
		xas->xa_offset = node->offset;
		xa_node_free(node);

		if (!parent) {
			xas->xa->xa_head = NULL;
			xas->xa_node = XAS_BOUNDS;
			return;
		}

		parent->slots[xas->xa_offset] = NULL;
		parent->count--;
		XA_NODE_BUG_ON(parent, parent->count > XA_CHUNK_SIZE);
		node = parent;
		xas_update(xas, node);
	}

	if (!node->parent)
		xas_shrink(xas);
}

static void xas_free_nodes(struct xa_state *xas, struct xa_node *top)
{
	unsigned int offset = 0;
	struct xa_node *node = top;

	for (;;) {
		void *entry = xa_entry_locked(xas->xa, node, offset);

		if (node->shift && xa_is_node(entry)) {
			node = xa_to_node(entry);
			offset = 0;
			continue;
		}
		if (entry)
			RCU_INIT_POINTER(node->slots[offset], XA_RETRY_ENTRY);
		offset++;
		while (offset == XA_CHUNK_SIZE) {
			struct xa_node *parent;

			parent = xa_parent_locked(xas->xa, node);
			offset = node->offset + 1;
			node->count = 0;
			node->nr_values = 0;
			xas_update(xas, node);
			xa_node_free(node);
			if (node == top)
				return;
			node = parent;
		}
	}
}

static int xas_expand(struct xa_state *xas, void *head)
{
	struct xarray *xa = xas->xa;
	struct xa_node *node = NULL;
	unsigned int shift = 0;
	unsigned long max = xas_max(xas);

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
		xa_mark_t mark = 0;

		XA_NODE_BUG_ON(node, shift > BITS_PER_LONG);
		node = xas_alloc(xas, shift);
		if (!node)
			return -ENOMEM;

		node->count = 1;
		if (xa_is_value(head))
			node->nr_values = 1;
		RCU_INIT_POINTER(node->slots[0], head);

		for (;;) {
			if (xa_track_free(xa) && mark == XA_FREE_MARK) {
				node_mark_all(node, XA_FREE_MARK);
				if (!xa_marked(xa, XA_FREE_MARK)) {
					node_clear_mark(node, 0, XA_FREE_MARK);
					xa_mark_set(xa, XA_FREE_MARK);
				}
			} else if (xa_marked(xa, mark)) {
				node_set_mark(node, 0, mark);
			}
			if (mark == XA_MARK_MAX)
				break;
			mark_inc(mark);
		}

		if (xa_is_node(head)) {
			xa_to_node(head)->offset = 0;
			rcu_assign_pointer(xa_to_node(head)->parent, node);
		}
		head = xa_mk_node(node);
		rcu_assign_pointer(xa->xa_head, head);
		xas_update(xas, node);

		shift += XA_CHUNK_SHIFT;
	}

	xas->xa_node = node;
	return shift;
}

static void *xas_create(struct xa_state *xas, bool allow_root)
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
		if (!entry && xa_zero_busy(xa))
			entry = XA_ZERO_ENTRY;
		shift = xas_expand(xas, entry);
		if (shift < 0)
			return NULL;
		if (!shift && !allow_root)
			shift = XA_CHUNK_SHIFT;
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
			if (xa_track_free(xa))
				node_mark_all(node, XA_FREE_MARK);
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

void xas_create_range(struct xa_state *xas)
{
}

static void update_node(struct xa_state *xas, struct xa_node *node,
		int count, int values)
{
	if (!node || (!count && !values))
		return;

	node->count += count;
	node->nr_values += values;
	XA_NODE_BUG_ON(node, node->count > XA_CHUNK_SIZE);
	XA_NODE_BUG_ON(node, node->nr_values > XA_CHUNK_SIZE);
	xas_update(xas, node);
	if (count < 0)
		xas_delete_node(xas);
}

void *xas_store(struct xa_state *xas, void *entry)
{
	struct xa_node *node;
	void __rcu **slot = &xas->xa->xa_head;
	unsigned int offset, max;
	int count = 0;
	int values = 0;
	void *first, *next;
	bool value = xa_is_value(entry);

	if (entry) {
		bool allow_root = !xa_is_node(entry) && !xa_is_zero(entry);
		first = xas_create(xas, allow_root);
	} else {
		first = xas_load(xas);
	}

	if (xas_invalid(xas))
		return first;
	node = xas->xa_node;
	if (node && (xas->xa_shift < node->shift))
		xas->xa_sibs = 0;
	if ((first == entry) && !xas->xa_sibs)
		return first;

	next = first;
	offset = xas->xa_offset;
	max = xas->xa_offset + xas->xa_sibs;
	if (node) {
		slot = &node->slots[offset];
		if (xas->xa_sibs)
			xas_squash_marks(xas);
	}
	if (!entry)
		xas_init_marks(xas);

	for (;;) {
		
		rcu_assign_pointer(*slot, entry);
		if (xa_is_node(next) && (!node || node->shift))
			xas_free_nodes(xas, xa_to_node(next));
		if (!node)
			break;
		count += !next - !entry;
		values += !xa_is_value(first) - !value;
		if (entry) {
			if (offset == max)
				break;
			if (!xa_is_sibling(entry))
				entry = xa_mk_sibling(xas->xa_offset);
		} else {
			if (offset == XA_CHUNK_MASK)
				break;
		}
		next = xa_entry_locked(xas->xa, node, ++offset);
		if (!xa_is_sibling(next)) {
			if (!entry && (offset > max))
				break;
			first = next;
		}
		slot++;
	}

	update_node(xas, node, count, values);
	return first;
}

bool xas_get_mark(const struct xa_state *xas, xa_mark_t mark)
{
	if (xas_invalid(xas))
		return false;
	if (!xas->xa_node)
		return xa_marked(xas->xa, mark);
	return node_get_mark(xas->xa_node, xas->xa_offset, mark);
}

void xas_set_mark(const struct xa_state *xas, xa_mark_t mark)
{
	struct xa_node *node = xas->xa_node;
	unsigned int offset = xas->xa_offset;

	if (xas_invalid(xas))
		return;

	while (node) {
		if (node_set_mark(node, offset, mark))
			return;
		offset = node->offset;
		node = xa_parent_locked(xas->xa, node);
	}

	if (!xa_marked(xas->xa, mark))
		xa_mark_set(xas->xa, mark);
}

void xas_clear_mark(const struct xa_state *xas, xa_mark_t mark)
{
	struct xa_node *node = xas->xa_node;
	unsigned int offset = xas->xa_offset;

	if (xas_invalid(xas))
		return;

	while (node) {
		if (!node_clear_mark(node, offset, mark))
			return;
		if (node_any_mark(node, mark))
			return;

		offset = node->offset;
		node = xa_parent_locked(xas->xa, node);
	}

	if (xa_marked(xas->xa, mark))
		xa_mark_clear(xas->xa, mark);
}

void xas_init_marks(const struct xa_state *xas)
{
	xa_mark_t mark = 0;

	for (;;) {
		if (xa_track_free(xas->xa) && mark == XA_FREE_MARK)
			xas_set_mark(xas, mark);
		else
			xas_clear_mark(xas, mark);
		if (mark == XA_MARK_MAX)
			break;
		mark_inc(mark);
	}
}

void xas_pause(struct xa_state *xas)
{
}

void *__xas_prev(struct xa_state *xas)
{
	void *entry;

	if (!xas_frozen(xas->xa_node))
		xas->xa_index--;
	if (!xas->xa_node)
		return set_bounds(xas);
	if (xas_not_node(xas->xa_node))
		return xas_load(xas);

	if (xas->xa_offset != get_offset(xas->xa_index, xas->xa_node))
		xas->xa_offset--;

	while (xas->xa_offset == 255) {
		xas->xa_offset = xas->xa_node->offset - 1;
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

void *xas_find(struct xa_state *xas, unsigned long max)
{
	void *entry;

	if (xas_error(xas) || xas->xa_node == XAS_BOUNDS)
		return NULL;
	if (xas->xa_index > max)
		return set_bounds(xas);

	if (!xas->xa_node) {
		xas->xa_index = 1;
		return set_bounds(xas);
	} else if (xas->xa_node == XAS_RESTART) {
		entry = xas_load(xas);
		if (entry || xas_not_node(xas->xa_node))
			return entry;
	} else if (!xas->xa_node->shift &&
		    xas->xa_offset != (xas->xa_index & XA_CHUNK_MASK)) {
		xas->xa_offset = ((xas->xa_index - 1) & XA_CHUNK_MASK) + 1;
	}

	xas_next_offset(xas);

	while (xas->xa_node && (xas->xa_index <= max)) {
		if (unlikely(xas->xa_offset == XA_CHUNK_SIZE)) {
			xas->xa_offset = xas->xa_node->offset + 1;
			xas->xa_node = xa_parent(xas->xa, xas->xa_node);
			continue;
		}

		entry = xa_entry(xas->xa, xas->xa_node, xas->xa_offset);
		if (xa_is_node(entry)) {
			xas->xa_node = xa_to_node(entry);
			xas->xa_offset = 0;
			continue;
		}
		if (entry && !xa_is_sibling(entry))
			return entry;

		xas_next_offset(xas);
	}

	if (!xas->xa_node)
		xas->xa_node = XAS_BOUNDS;
	return NULL;
}

void *xas_find_marked(struct xa_state *xas, unsigned long max, xa_mark_t mark)
{
	bool advance = true;
	unsigned int offset;
	void *entry;

	if (xas_error(xas))
		return NULL;
	if (xas->xa_index > max)
		goto max;

	if (!xas->xa_node) {
		xas->xa_index = 1;
		goto out;
	} else if (xas_top(xas->xa_node)) {
		advance = false;
		entry = xa_head(xas->xa);
		xas->xa_node = NULL;
		if (xas->xa_index > max_index(entry))
			goto out;
		if (!xa_is_node(entry)) {
			if (xa_marked(xas->xa, mark))
				return entry;
			xas->xa_index = 1;
			goto out;
		}
		xas->xa_node = xa_to_node(entry);
		xas->xa_offset = xas->xa_index >> xas->xa_node->shift;
	}

	while (xas->xa_index <= max) {
		if (unlikely(xas->xa_offset == XA_CHUNK_SIZE)) {
			xas->xa_offset = xas->xa_node->offset + 1;
			xas->xa_node = xa_parent(xas->xa, xas->xa_node);
			if (!xas->xa_node)
				break;
			advance = false;
			continue;
		}

		if (!advance) {
			entry = xa_entry(xas->xa, xas->xa_node, xas->xa_offset);
			if (xa_is_sibling(entry)) {
				xas->xa_offset = xa_to_sibling(entry);
				xas_move_index(xas, xas->xa_offset);
			}
		}

		offset = xas_find_chunk(xas, advance, mark);
		if (offset > xas->xa_offset) {
			advance = false;
			xas_move_index(xas, offset);
			
			if ((xas->xa_index - 1) >= max)
				goto max;
			xas->xa_offset = offset;
			if (offset == XA_CHUNK_SIZE)
				continue;
		}

		entry = xa_entry(xas->xa, xas->xa_node, xas->xa_offset);
		if (!entry && !(xa_track_free(xas->xa) && mark == XA_FREE_MARK))
			continue;
		if (!xa_is_node(entry))
			return entry;
		xas->xa_node = xa_to_node(entry);
		xas_set_offset(xas);
	}

out:
	if (xas->xa_index > max)
		goto max;
	return set_bounds(xas);
max:
	xas->xa_node = XAS_RESTART;
	return NULL;
}

void *xas_find_conflict(struct xa_state *xas)
{
	return NULL;
}

void *xa_load(struct xarray *xa, unsigned long index)
{
	XA_STATE(xas, xa, index);
	void *entry;

	rcu_read_lock();
	do {
		entry = xas_load(&xas);
		if (xa_is_zero(entry))
			entry = NULL;
	} while (xas_retry(&xas, entry));
	rcu_read_unlock();

	return entry;
}

static void *xas_result(struct xa_state *xas, void *curr)
{
	if (xa_is_zero(curr))
		return NULL;
	if (xas_error(xas))
		curr = xas->xa_node;
	return curr;
}

void *__xa_erase(struct xarray *xa, unsigned long index)
{
	XA_STATE(xas, xa, index);
	return xas_result(&xas, xas_store(&xas, NULL));
}

void *xa_erase(struct xarray *xa, unsigned long index)
{
	void *entry;

	xa_lock(xa);
	entry = __xa_erase(xa, index);
	xa_unlock(xa);

	return entry;
}

/* Stubbed - not used externally */
void *__xa_store(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
	return NULL;
}

/* Stubbed - not used externally */
void *xa_store(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
	return NULL;
}

/* Stubbed - not used externally */
void *__xa_cmpxchg(struct xarray *xa, unsigned long index,
			void *old, void *entry, gfp_t gfp)
{
	return NULL;
}

/* Stubbed - not used externally */
int __xa_insert(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
	return -EINVAL;
}

/* Stubbed - not used externally */
int __xa_alloc(struct xarray *xa, u32 *id, void *entry,
		struct xa_limit limit, gfp_t gfp)
{
	return -EINVAL;
}

/* Stubbed - not used externally */
int __xa_alloc_cyclic(struct xarray *xa, u32 *id, void *entry,
		struct xa_limit limit, u32 *next, gfp_t gfp)
{
	return -EINVAL;
}

/* Stubbed - not used externally */
void __xa_set_mark(struct xarray *xa, unsigned long index, xa_mark_t mark)
{
}

/* Stubbed - not used externally */
void __xa_clear_mark(struct xarray *xa, unsigned long index, xa_mark_t mark)
{
}

/* Stubbed - not used externally */
bool xa_get_mark(struct xarray *xa, unsigned long index, xa_mark_t mark)
{
	return false;
}

/* Stubbed - not used externally */
void xa_set_mark(struct xarray *xa, unsigned long index, xa_mark_t mark)
{
}

/* Stubbed - not used externally */
void xa_clear_mark(struct xarray *xa, unsigned long index, xa_mark_t mark)
{
}

/* Stubbed - not used externally */
void *xa_find(struct xarray *xa, unsigned long *indexp,
			unsigned long max, xa_mark_t filter)
{
	return NULL;
}

/* Stubbed - not used externally */
void *xa_find_after(struct xarray *xa, unsigned long *indexp,
			unsigned long max, xa_mark_t filter)
{
	return NULL;
}

unsigned int xa_extract(struct xarray *xa, void **dst, unsigned long start,
			unsigned long max, unsigned int n, xa_mark_t filter)
{
	return 0;
}

/* Stubbed - not used externally */
void xa_delete_node(struct xa_node *node, xa_update_node_t update)
{
}

/* Stubbed - not used externally */
void xa_destroy(struct xarray *xa)
{
}

#ifdef XA_DEBUG
void xa_dump_node(const struct xa_node *node)
{
	
}

void xa_dump_index(unsigned long index, unsigned int shift)
{
	
}

void xa_dump_entry(const void *entry, unsigned long index, unsigned long shift)
{
	
}

void xa_dump(const struct xarray *xa)
{
	
}
#endif
