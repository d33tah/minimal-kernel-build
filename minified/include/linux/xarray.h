#ifndef _LINUX_XARRAY_H
#define _LINUX_XARRAY_H

#include <linux/rcupdate.h>

static inline void *xa_mk_internal(unsigned long v)
{
	return (void *)((v << 2) | 2);
}

static inline bool xa_is_internal(const void *entry)
{
	return ((unsigned long)entry & 3) == 2;
}

static inline int xa_err(void *entry)
{
	if (xa_is_internal(entry) && entry >= xa_mk_internal(-MAX_ERRNO))
		return (long)entry >> 2;
	return 0;
}

enum xa_lock_type {
	XA_LOCK_IRQ = 1,
};

#define XA_FLAGS_LOCK_IRQ ((__force gfp_t)XA_LOCK_IRQ)
#define XA_FLAGS_ACCOUNT ((__force gfp_t)32U)

struct xarray {
	spinlock_t xa_lock;
	gfp_t xa_flags;
	void __rcu *xa_head;
};

static inline void xa_init_flags(struct xarray *xa, gfp_t flags)
{
	spin_lock_init(&xa->xa_lock);
	xa->xa_flags = flags;
	xa->xa_head = NULL;
}

#define xa_lock_irq(xa) spin_lock_irq(&(xa)->xa_lock)
#define xa_unlock_irq(xa) spin_unlock_irq(&(xa)->xa_lock)

#define XA_CHUNK_SHIFT (CONFIG_BASE_SMALL ? 4 : 6)
#define XA_CHUNK_SIZE (1UL << XA_CHUNK_SHIFT)
#define XA_CHUNK_MASK (XA_CHUNK_SIZE - 1)

struct xa_node {
	unsigned char shift;
	unsigned char offset;
	unsigned char count;
	struct xa_node __rcu *parent;
	void __rcu *slots[XA_CHUNK_SIZE];
};

void radix_tree_init(void);

static inline void *xa_head(const struct xarray *xa)
{
	return rcu_dereference_check(xa->xa_head,
				     lockdep_is_held(&xa->xa_lock));
}

static inline void *xa_head_locked(const struct xarray *xa)
{
	return rcu_dereference_protected(xa->xa_head,
					 lockdep_is_held(&xa->xa_lock));
}

static inline void *xa_entry(const struct xarray *xa,
			     const struct xa_node *node, unsigned int offset)
{
	return rcu_dereference_check(node->slots[offset],
				     lockdep_is_held(&xa->xa_lock));
}

static inline void *xa_entry_locked(const struct xarray *xa,
				    const struct xa_node *node,
				    unsigned int offset)
{
	return rcu_dereference_protected(node->slots[offset],
					 lockdep_is_held(&xa->xa_lock));
}

static inline struct xa_node *xa_parent(const struct xarray *xa,
					const struct xa_node *node)
{
	return rcu_dereference_check(node->parent,
				     lockdep_is_held(&xa->xa_lock));
}

static inline void *xa_mk_node(const struct xa_node *node)
{
	return (void *)((unsigned long)node | 2);
}

static inline struct xa_node *xa_to_node(const void *entry)
{
	return (struct xa_node *)((unsigned long)entry - 2);
}

static inline bool xa_is_node(const void *entry)
{
	return xa_is_internal(entry) && (unsigned long)entry > 4096;
}

struct xa_state {
	struct xarray *xa;
	unsigned long xa_index;
	unsigned char xa_shift;
	unsigned char xa_offset;
	struct xa_node *xa_node;
	struct xa_node *xa_alloc;
};

#define XA_ERROR(errno) ((struct xa_node *)(((unsigned long)errno << 2) | 2UL))
#define XAS_BOUNDS ((struct xa_node *)1UL)
#define XAS_RESTART ((struct xa_node *)3UL)

#define __XA_STATE(array, index, shift, sibs) \
	{                                     \
		.xa = array,                  \
		.xa_index = index,            \
		.xa_shift = shift,            \
		.xa_offset = 0,               \
		.xa_node = XAS_RESTART,       \
		.xa_alloc = NULL,             \
	}

#define XA_STATE(name, array, index) \
	struct xa_state name = __XA_STATE(array, index, 0, 0)

#define xas_lock_irq(xas) xa_lock_irq((xas)->xa)
#define xas_unlock_irq(xas) xa_unlock_irq((xas)->xa)

static inline int xas_error(const struct xa_state *xas)
{
	return xa_err(xas->xa_node);
}

static inline void xas_set_err(struct xa_state *xas, long err)
{
	xas->xa_node = XA_ERROR(err);
}

static inline bool xas_invalid(const struct xa_state *xas)
{
	return (unsigned long)xas->xa_node & 3;
}

static inline bool xas_valid(const struct xa_state *xas)
{
	return !xas_invalid(xas);
}

static inline bool xas_is_node(const struct xa_state *xas)
{
	return xas_valid(xas) && xas->xa_node;
}

static inline bool xas_not_node(struct xa_node *node)
{
	return ((unsigned long)node & 3) || !node;
}

static inline bool xas_frozen(struct xa_node *node)
{
	return (unsigned long)node & 2;
}

static inline bool xas_top(struct xa_node *node)
{
	return node <= XAS_RESTART;
}

static inline void xas_reset(struct xa_state *xas)
{
	xas->xa_node = XAS_RESTART;
}

void *xas_load(struct xa_state *);
void *xas_store(struct xa_state *, void *entry);

bool xas_nomem(struct xa_state *, gfp_t);

static inline void *xas_reload(struct xa_state *xas)
{
	struct xa_node *node = xas->xa_node;
	char offset;

	if (!node)
		return xa_head(xas->xa);
	offset = xas->xa_offset;
	return xa_entry(xas->xa, node, offset);
}

static inline void xas_set(struct xa_state *xas, unsigned long index)
{
	xas->xa_index = index;
	xas->xa_node = XAS_RESTART;
}

static inline void xas_advance(struct xa_state *xas, unsigned long index)
{
	unsigned char shift = xas_is_node(xas) ? xas->xa_node->shift : 0;

	xas->xa_index = index;
	xas->xa_offset = (index >> shift) & XA_CHUNK_MASK;
}

static inline void xas_set_order(struct xa_state *xas, unsigned long index,
				 unsigned int order)
{
	BUG_ON(order > 0);
	xas_set(xas, index);
}

void *__xas_next(struct xa_state *);

static inline void *xas_next(struct xa_state *xas)
{
	struct xa_node *node = xas->xa_node;

	if (unlikely(xas_not_node(node) || node->shift ||
		     xas->xa_offset == XA_CHUNK_MASK))
		return __xas_next(xas);

	xas->xa_index++;
	xas->xa_offset++;
	return xa_entry(xas->xa, node, xas->xa_offset);
}

#endif
