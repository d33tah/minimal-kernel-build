
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/radix-tree.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/xarray.h>

struct kmem_cache *radix_tree_node_cachep;

#define IDR_INDEX_BITS (8 * sizeof(int) - 1)
#define IDR_MAX_PATH (DIV_ROUND_UP(IDR_INDEX_BITS, RADIX_TREE_MAP_SHIFT))
#define IDR_PRELOAD_SIZE (IDR_MAX_PATH * 2 - 1)

DEFINE_PER_CPU(struct radix_tree_preload, radix_tree_preloads) = {
	.lock = INIT_LOCAL_LOCK(lock),
};

static inline struct radix_tree_node *entry_to_node(void *ptr)
{
	return (void *)((unsigned long)ptr & ~RADIX_TREE_INTERNAL_NODE);
}

static inline void *node_to_entry(void *ptr)
{
	return (void *)((unsigned long)ptr | RADIX_TREE_INTERNAL_NODE);
}

#define RADIX_TREE_RETRY XA_RETRY_ENTRY

static inline unsigned long
get_slot_offset(const struct radix_tree_node *parent, void __rcu **slot)
{
	return parent ? slot - parent->slots : 0;
}

static unsigned int radix_tree_descend(const struct radix_tree_node *parent,
				       struct radix_tree_node **nodep,
				       unsigned long index)
{
	unsigned int offset = (index >> parent->shift) & RADIX_TREE_MAP_MASK;
	void __rcu **entry = rcu_dereference_raw(parent->slots[offset]);

	*nodep = (void *)entry;
	return offset;
}

/* root_gfp_mask inlined - returned root->xa_flags & (__GFP_BITS_MASK & ~GFP_ZONEMASK) */

static inline void tag_set(struct radix_tree_node *node, unsigned int tag,
			   int offset)
{
	__set_bit(offset, node->tags[tag]);
}

static inline void tag_clear(struct radix_tree_node *node, unsigned int tag,
			     int offset)
{
	__clear_bit(offset, node->tags[tag]);
}

static inline int tag_get(const struct radix_tree_node *node, unsigned int tag,
			  int offset)
{
	return test_bit(offset, node->tags[tag]);
}

static inline void root_tag_set(struct radix_tree_root *root, unsigned tag)
{
	root->xa_flags |= (__force gfp_t)(1 << (tag + ROOT_TAG_SHIFT));
}

static inline void root_tag_clear(struct radix_tree_root *root, unsigned tag)
{
	root->xa_flags &= (__force gfp_t) ~(1 << (tag + ROOT_TAG_SHIFT));
}

/* root_tag_clear_all inlined - cleared tags in xa_flags */

static inline int root_tag_get(const struct radix_tree_root *root, unsigned tag)
{
	return (__force int)root->xa_flags & (1 << (tag + ROOT_TAG_SHIFT));
}

/* root_tags_get inlined - returned root->xa_flags >> ROOT_TAG_SHIFT */

static inline bool is_idr(const struct radix_tree_root *root)
{
	return !!(root->xa_flags & ROOT_IS_IDR);
}

static inline void all_tag_set(struct radix_tree_node *node, unsigned int tag)
{
	bitmap_fill(node->tags[tag], RADIX_TREE_MAP_SIZE);
}

static __always_inline unsigned long
radix_tree_find_next_bit(struct radix_tree_node *node, unsigned int tag,
			 unsigned long offset)
{
	const unsigned long *addr = node->tags[tag];

	if (offset < RADIX_TREE_MAP_SIZE) {
		unsigned long tmp;

		addr += offset / BITS_PER_LONG;
		tmp = *addr >> (offset % BITS_PER_LONG);
		if (tmp)
			return __ffs(tmp) + offset;
		offset = (offset + BITS_PER_LONG) & ~(BITS_PER_LONG - 1);
		while (offset < RADIX_TREE_MAP_SIZE) {
			tmp = *++addr;
			if (tmp)
				return __ffs(tmp) + offset;
			offset += BITS_PER_LONG;
		}
	}
	return RADIX_TREE_MAP_SIZE;
}

/* iter_offset inlined - returned iter->index & RADIX_TREE_MAP_MASK */

static inline unsigned long shift_maxindex(unsigned int shift)
{
	return (RADIX_TREE_MAP_SIZE << shift) - 1;
}

static inline unsigned long node_maxindex(const struct radix_tree_node *node)
{
	return shift_maxindex(node->shift);
}

/* next_index inlined - returned (index & ~node_maxindex(node)) + (offset << node->shift) */

static struct radix_tree_node *
radix_tree_node_alloc(gfp_t gfp_mask, struct radix_tree_node *parent,
		      struct radix_tree_root *root, unsigned int shift,
		      unsigned int offset, unsigned int count,
		      unsigned int nr_values)
{
	struct radix_tree_node *ret = NULL;

	if (!gfpflags_allow_blocking(gfp_mask) && !in_interrupt()) {
		struct radix_tree_preload *rtp;

		ret = kmem_cache_alloc(radix_tree_node_cachep,
				       gfp_mask | __GFP_NOWARN);
		if (ret)
			goto out;

		rtp = this_cpu_ptr(&radix_tree_preloads);
		if (rtp->nr) {
			ret = rtp->nodes;
			rtp->nodes = ret->parent;
			rtp->nr--;
		}
		goto out;
	}
	ret = kmem_cache_alloc(radix_tree_node_cachep, gfp_mask);
out:
	BUG_ON(radix_tree_is_internal_node(ret));
	if (ret) {
		ret->shift = shift;
		ret->offset = offset;
		ret->count = count;
		ret->nr_values = nr_values;
		ret->parent = parent;
		ret->array = root;
	}
	return ret;
}

void radix_tree_node_rcu_free(struct rcu_head *head)
{
	struct radix_tree_node *node =
		container_of(head, struct radix_tree_node, rcu_head);

	memset(node->slots, 0, sizeof(node->slots));
	memset(node->tags, 0, sizeof(node->tags));
	INIT_LIST_HEAD(&node->private_list);

	kmem_cache_free(radix_tree_node_cachep, node);
}

static inline void radix_tree_node_free(struct radix_tree_node *node)
{
	call_rcu(&node->rcu_head, radix_tree_node_rcu_free);
}

static unsigned radix_tree_load_root(const struct radix_tree_root *root,
				     struct radix_tree_node **nodep,
				     unsigned long *maxindex)
{
	struct radix_tree_node *node = rcu_dereference_raw(root->xa_head);

	*nodep = node;

	if (likely(radix_tree_is_internal_node(node))) {
		node = entry_to_node(node);
		*maxindex = node_maxindex(node);
		return node->shift + RADIX_TREE_MAP_SHIFT;
	}

	*maxindex = 0;
	return 0;
}

static int radix_tree_extend(struct radix_tree_root *root, gfp_t gfp,
			     unsigned long index, unsigned int shift)
{
	void *entry;
	unsigned int maxshift;
	int tag;

	maxshift = shift;
	while (index > shift_maxindex(maxshift))
		maxshift += RADIX_TREE_MAP_SHIFT;

	entry = rcu_dereference_raw(root->xa_head);
	if (!entry && (!is_idr(root) || root_tag_get(root, IDR_FREE)))
		goto out;

	do {
		struct radix_tree_node *node =
			radix_tree_node_alloc(gfp, NULL, root, shift, 0, 1, 0);
		if (!node)
			return -ENOMEM;

		if (is_idr(root)) {
			all_tag_set(node, IDR_FREE);
			if (!root_tag_get(root, IDR_FREE)) {
				tag_clear(node, IDR_FREE, 0);
				root_tag_set(root, IDR_FREE);
			}
		} else {
			for (tag = 0; tag < RADIX_TREE_MAX_TAGS; tag++) {
				if (root_tag_get(root, tag))
					tag_set(node, tag, 0);
			}
		}

		BUG_ON(shift > BITS_PER_LONG);
		if (radix_tree_is_internal_node(entry)) {
			entry_to_node(entry)->parent = node;
		} else if (xa_is_value(entry)) {
			node->nr_values = 1;
		}

		node->slots[0] = (void __rcu *)entry;
		entry = node_to_entry(node);
		rcu_assign_pointer(root->xa_head, entry);
		shift += RADIX_TREE_MAP_SHIFT;
	} while (shift <= maxshift);
out:
	return maxshift + RADIX_TREE_MAP_SHIFT;
}

static inline bool radix_tree_shrink(struct radix_tree_root *root)
{
	bool shrunk = false;

	for (;;) {
		struct radix_tree_node *node =
			rcu_dereference_raw(root->xa_head);
		struct radix_tree_node *child;

		if (!radix_tree_is_internal_node(node))
			break;
		node = entry_to_node(node);

		if (node->count != 1)
			break;
		child = rcu_dereference_raw(node->slots[0]);
		if (!child)
			break;

		if (!node->shift && is_idr(root))
			break;

		if (radix_tree_is_internal_node(child))
			entry_to_node(child)->parent = NULL;

		root->xa_head = (void __rcu *)child;
		if (is_idr(root) && !tag_get(node, IDR_FREE, 0))
			root_tag_clear(root, IDR_FREE);

		node->count = 0;
		if (!radix_tree_is_internal_node(child)) {
			node->slots[0] = (void __rcu *)RADIX_TREE_RETRY;
		}

		WARN_ON_ONCE(!list_empty(&node->private_list));
		radix_tree_node_free(node);
		shrunk = true;
	}

	return shrunk;
}

static bool delete_node(struct radix_tree_root *root,
			struct radix_tree_node *node)
{
	bool deleted = false;

	do {
		struct radix_tree_node *parent;

		if (node->count) {
			if (node_to_entry(node) ==
			    rcu_dereference_raw(root->xa_head))
				deleted |= radix_tree_shrink(root);
			return deleted;
		}

		parent = node->parent;
		if (parent) {
			parent->slots[node->offset] = NULL;
			parent->count--;
		} else {
			if (!is_idr(root))
				root->xa_flags &= (__force gfp_t)(
					(1 << ROOT_TAG_SHIFT) - 1);
			root->xa_head = NULL;
		}

		WARN_ON_ONCE(!list_empty(&node->private_list));
		radix_tree_node_free(node);
		deleted = true;

		node = parent;
	} while (node);

	return deleted;
}

int radix_tree_insert(struct radix_tree_root *root, unsigned long index,
		      void *item)
{
	struct radix_tree_node *node = NULL, *child;
	void __rcu **slot = (void __rcu **)&root->xa_head;
	unsigned long maxindex;
	unsigned int shift, offset = 0;
	gfp_t gfp = root->xa_flags & (__GFP_BITS_MASK & ~GFP_ZONEMASK);

	BUG_ON(radix_tree_is_internal_node(item));

	shift = radix_tree_load_root(root, &child, &maxindex);
	if (index > maxindex) {
		int error = radix_tree_extend(root, gfp, index, shift);
		if (error < 0)
			return error;
		shift = error;
		child = rcu_dereference_raw(root->xa_head);
	}

	while (shift > 0) {
		shift -= RADIX_TREE_MAP_SHIFT;
		if (child == NULL) {
			child = radix_tree_node_alloc(gfp, node, root, shift,
						      offset, 0, 0);
			if (!child)
				return -ENOMEM;
			rcu_assign_pointer(*slot, node_to_entry(child));
			if (node)
				node->count++;
		} else if (!radix_tree_is_internal_node(child))
			break;
		node = entry_to_node(child);
		offset = radix_tree_descend(node, &child, index);
		slot = &node->slots[offset];
	}

	if (*slot)
		return -EEXIST;
	rcu_assign_pointer(*slot, item);
	if (node) {
		node->count++;
		if (xa_is_value(item))
			node->nr_values++;
	}

	return 0;
}

void *__radix_tree_lookup(const struct radix_tree_root *root,
			  unsigned long index, struct radix_tree_node **nodep,
			  void __rcu ***slotp)
{
	struct radix_tree_node *node, *parent;
	unsigned long maxindex;
	void __rcu **slot;

restart:
	parent = NULL;
	slot = (void __rcu **)&root->xa_head;
	radix_tree_load_root(root, &node, &maxindex);
	if (index > maxindex)
		return NULL;

	while (radix_tree_is_internal_node(node)) {
		unsigned offset;

		parent = entry_to_node(node);
		offset = radix_tree_descend(parent, &node, index);
		slot = parent->slots + offset;
		if (node == RADIX_TREE_RETRY)
			goto restart;
		if (parent->shift == 0)
			break;
	}

	if (nodep)
		*nodep = parent;
	if (slotp)
		*slotp = slot;
	return node;
}

void *radix_tree_lookup(const struct radix_tree_root *root, unsigned long index)
{
	return __radix_tree_lookup(root, index, NULL, NULL);
}

static void replace_slot(void __rcu **slot, void *item,
			 struct radix_tree_node *node, int count, int values)
{
	if (node && (count || values)) {
		node->count += count;
		node->nr_values += values;
	}

	rcu_assign_pointer(*slot, item);
}

static bool node_tag_get(const struct radix_tree_root *root,
			 const struct radix_tree_node *node, unsigned int tag,
			 unsigned int offset)
{
	if (node)
		return tag_get(node, tag, offset);
	return root_tag_get(root, tag);
}

static int calculate_count(struct radix_tree_root *root,
			   struct radix_tree_node *node, void __rcu **slot,
			   void *item, void *old)
{
	if (is_idr(root)) {
		unsigned offset = get_slot_offset(node, slot);
		bool free = node_tag_get(root, node, IDR_FREE, offset);
		if (!free)
			return 0;
		if (!old)
			return 1;
	}
	return !!item - !!old;
}

void __radix_tree_replace(struct radix_tree_root *root,
			  struct radix_tree_node *node, void __rcu **slot,
			  void *item)
{
	void *old = rcu_dereference_raw(*slot);
	int values = !!xa_is_value(item) - !!xa_is_value(old);
	int count = calculate_count(root, node, slot, item, old);

	WARN_ON_ONCE(!node && (slot != (void __rcu **)&root->xa_head) &&
		     (count || values));
	replace_slot(slot, item, node, count, values);

	if (!node)
		return;

	delete_node(root, node);
}

void radix_tree_iter_replace(struct radix_tree_root *root,
			     const struct radix_tree_iter *iter,
			     void __rcu **slot, void *item)
{
	__radix_tree_replace(root, iter->node, slot, item);
}

static void node_tag_clear(struct radix_tree_root *root,
			   struct radix_tree_node *node, unsigned int tag,
			   unsigned int offset)
{
	while (node) {
		unsigned idx;
		bool any_set;

		if (!tag_get(node, tag, offset))
			return;
		tag_clear(node, tag, offset);
		any_set = false;
		for (idx = 0; idx < RADIX_TREE_TAG_LONGS; idx++) {
			if (node->tags[tag][idx]) {
				any_set = true;
				break;
			}
		}
		if (any_set)
			return;

		offset = node->offset;
		node = node->parent;
	}

	if (root_tag_get(root, tag))
		root_tag_clear(root, tag);
}

void radix_tree_iter_tag_clear(struct radix_tree_root *root,
			       const struct radix_tree_iter *iter,
			       unsigned int tag)
{
	node_tag_clear(root, iter->node, tag,
		       iter->index & RADIX_TREE_MAP_MASK);
}

int radix_tree_tag_get(const struct radix_tree_root *root, unsigned long index,
		       unsigned int tag)
{
	struct radix_tree_node *node, *parent;
	unsigned long maxindex;

	if (!root_tag_get(root, tag))
		return 0;

	radix_tree_load_root(root, &node, &maxindex);
	if (index > maxindex)
		return 0;

	while (radix_tree_is_internal_node(node)) {
		unsigned offset;

		parent = entry_to_node(node);
		offset = radix_tree_descend(parent, &node, index);

		if (!tag_get(parent, tag, offset))
			return 0;
		if (node == RADIX_TREE_RETRY)
			break;
	}

	return 1;
}

/* __radix_tree_delete + radix_tree_delete_item removed:
 * idr_remove was stubbed (PIDs never freed in hello-world kernel) */

void idr_preload(gfp_t gfp_mask)
{
	struct radix_tree_preload *rtp;
	struct radix_tree_node *node;
	int ok = 0;

	gfp_mask &= ~__GFP_ACCOUNT;
	local_lock(&radix_tree_preloads.lock);
	rtp = this_cpu_ptr(&radix_tree_preloads);
	while (rtp->nr < IDR_PRELOAD_SIZE) {
		local_unlock(&radix_tree_preloads.lock);
		node = kmem_cache_alloc(radix_tree_node_cachep, gfp_mask);
		if (node == NULL)
			goto done;
		local_lock(&radix_tree_preloads.lock);
		rtp = this_cpu_ptr(&radix_tree_preloads);
		if (rtp->nr < IDR_PRELOAD_SIZE) {
			node->parent = rtp->nodes;
			rtp->nodes = node;
			rtp->nr++;
		} else {
			kmem_cache_free(radix_tree_node_cachep, node);
		}
	}
	ok = 1;
done:
	if (!ok)
		local_lock(&radix_tree_preloads.lock);
}

void __rcu **idr_get_free(struct radix_tree_root *root,
			  struct radix_tree_iter *iter, gfp_t gfp,
			  unsigned long max)
{
	struct radix_tree_node *node = NULL, *child;
	void __rcu **slot = (void __rcu **)&root->xa_head;
	unsigned long maxindex, start = iter->next_index;
	unsigned int shift, offset = 0;

grow:
	shift = radix_tree_load_root(root, &child, &maxindex);
	if (!root_tag_get(root, IDR_FREE))
		start = max(start, maxindex + 1);
	if (start > max)
		return ERR_PTR(-ENOSPC);

	if (start > maxindex) {
		int error = radix_tree_extend(root, gfp, start, shift);
		if (error < 0)
			return ERR_PTR(error);
		shift = error;
		child = rcu_dereference_raw(root->xa_head);
	}
	if (start == 0 && shift == 0)
		shift = RADIX_TREE_MAP_SHIFT;

	while (shift) {
		shift -= RADIX_TREE_MAP_SHIFT;
		if (child == NULL) {
			child = radix_tree_node_alloc(gfp, node, root, shift,
						      offset, 0, 0);
			if (!child)
				return ERR_PTR(-ENOMEM);
			all_tag_set(child, IDR_FREE);
			rcu_assign_pointer(*slot, node_to_entry(child));
			if (node)
				node->count++;
		} else if (!radix_tree_is_internal_node(child))
			break;

		node = entry_to_node(child);
		offset = radix_tree_descend(node, &child, start);
		if (!tag_get(node, IDR_FREE, offset)) {
			offset = radix_tree_find_next_bit(node, IDR_FREE,
							  offset + 1);
			start = (start & ~node_maxindex(node)) +
				(offset << node->shift);
			if (start > max || start == 0)
				return ERR_PTR(-ENOSPC);
			while (offset == RADIX_TREE_MAP_SIZE) {
				offset = node->offset + 1;
				node = node->parent;
				if (!node)
					goto grow;
				shift = node->shift;
			}
			child = rcu_dereference_raw(node->slots[offset]);
		}
		slot = &node->slots[offset];
	}

	iter->index = start;
	if (node)
		iter->next_index = 1 + min(max, (start | node_maxindex(node)));
	else
		iter->next_index = 1;
	iter->node = node;

	return slot;
}

static void radix_tree_node_ctor(void *arg)
{
	struct radix_tree_node *node = arg;

	memset(node, 0, sizeof(*node));
	INIT_LIST_HEAD(&node->private_list);
}

void __init radix_tree_init(void)
{
	BUILD_BUG_ON(RADIX_TREE_MAX_TAGS + __GFP_BITS_SHIFT > 32);
	BUILD_BUG_ON(ROOT_IS_IDR & ~GFP_ZONEMASK);
	BUILD_BUG_ON(XA_CHUNK_SIZE > 255);
	radix_tree_node_cachep = kmem_cache_create(
		"radix_tree_node", sizeof(struct radix_tree_node), 0,
		SLAB_PANIC | SLAB_RECLAIM_ACCOUNT, radix_tree_node_ctor);
}
