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

struct xa_state {
	struct xarray *xa;
	unsigned long xa_index;
	struct xa_node *xa_node;
};

#define XA_ERROR(errno) ((struct xa_node *)(((unsigned long)errno << 2) | 2UL))
#define XAS_BOUNDS ((struct xa_node *)1UL)
#define XAS_RESTART ((struct xa_node *)3UL)

#define __XA_STATE(array, index, shift, sibs) \
	{                                     \
		.xa = array,                  \
		.xa_index = index,            \
		.xa_node = XAS_RESTART,       \
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

static inline void *xas_reload(struct xa_state *xas)
{
	return xa_head(xas->xa);
}

static inline void xas_set(struct xa_state *xas, unsigned long index)
{
	xas->xa_index = index;
	xas->xa_node = XAS_RESTART;
}

static inline void xas_advance(struct xa_state *xas, unsigned long index)
{
	xas->xa_index = index;
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
	return __xas_next(xas);
}

#endif
