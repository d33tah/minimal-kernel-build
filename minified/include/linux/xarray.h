/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef _LINUX_XARRAY_H
#define _LINUX_XARRAY_H

#include <linux/rcupdate.h>

/*
 * xa_is_internal() - Is the entry an internal entry?
 * @entry: XArray entry.
 *
 * Context: Any context.
 * Return: %true if the entry is an internal entry.
 */
static inline bool xa_is_internal(const void *entry)
{
	return ((unsigned long)entry & 3) == 2;
}

enum xa_lock_type {
	XA_LOCK_IRQ = 1,
};

/*
 * Values for xa_flags.  The radix tree stores its GFP flags in the xa_flags,
 * and we remain compatible with that.
 */
#define XA_FLAGS_LOCK_IRQ ((__force gfp_t)XA_LOCK_IRQ)
#define XA_FLAGS_ACCOUNT ((__force gfp_t)32U)

/**
 * struct xarray - The anchor of the XArray.
 * @xa_lock: Lock that protects the contents of the XArray.
 *
 * To use the xarray, define it statically or embed it in your data structure.
 * It is a very small data structure, so it does not usually make sense to
 * allocate it separately and keep a pointer to it in your data structure.
 *
 * You may use the xa_lock to protect your own data structures as well.
 */
/*
 * If all of the entries in the array are NULL, @xa_head is a NULL pointer.
 * If the only non-NULL entry in the array is at index 0, @xa_head is that
 * entry.  If any other entry in the array is non-NULL, @xa_head points
 * to an @xa_node.
 */
struct xarray {
	spinlock_t xa_lock;
	/* private: The rest of the data structure is not to be used directly. */
	gfp_t xa_flags;
	void __rcu *xa_head;
};

/**
 * xa_init_flags() - Initialise an empty XArray with flags.
 * @xa: XArray.
 * @flags: XA_FLAG values.
 *
 * If you need to initialise an XArray with special flags (eg you need
 * to take the lock from interrupt context), use this function instead
 * of xa_init().
 *
 * Context: Any context.
 */
static inline void xa_init_flags(struct xarray *xa, gfp_t flags)
{
	spin_lock_init(&xa->xa_lock);
	xa->xa_flags = flags;
	xa->xa_head = NULL;
}

#define xa_lock_irq(xa) spin_lock_irq(&(xa)->xa_lock)
#define xa_unlock_irq(xa) spin_unlock_irq(&(xa)->xa_lock)

/* Private */
static inline void *xa_head(const struct xarray *xa)
{
	return rcu_dereference_check(xa->xa_head,
				     lockdep_is_held(&xa->xa_lock));
}

/* Private */
static inline void *xa_head_locked(const struct xarray *xa)
{
	return rcu_dereference_protected(xa->xa_head,
					 lockdep_is_held(&xa->xa_lock));
}

/*
 * The xa_state is opaque to its users.  It contains various different pieces
 * of state involved in the current operation on the XArray.  It should be
 * declared on the stack and passed between the various internal routines.
 * The various elements in it should not be accessed directly, but only
 * through the provided accessor functions.  The below documentation is for
 * the benefit of those working on the code, not for users of the XArray.
 *
 * @xa_node usually points to the xa_node containing the slot we're operating
 * on (and @xa_offset is the offset in the slots array).  If there is a
 * single entry in the array at index 0, there are no allocated xa_nodes to
 * point to, and so we store %NULL in @xa_node.  @xa_node is set to
 * the value %XAS_RESTART if the xa_state is not walked to the correct
 * position in the tree of nodes for this operation.  If an error occurs
 * during an operation, it is set to an %XAS_ERROR value.  If we run off the
 * end of the allocated nodes, it is set to %XAS_BOUNDS.
 */
struct xa_state {
	struct xarray *xa;
	unsigned long xa_index;
	struct xa_node *xa_node;
};

#define XAS_BOUNDS ((struct xa_node *)1UL)
#define XAS_RESTART ((struct xa_node *)3UL)

#define __XA_STATE(array, index, shift, sibs) \
	{                                     \
		.xa = array,                  \
		.xa_index = index,            \
		.xa_node = XAS_RESTART,       \
	}

/**
 * XA_STATE() - Declare an XArray operation state.
 * @name: Name of this operation state (usually xas).
 * @array: Array to operate on.
 * @index: Initial index of interest.
 *
 * Declare and initialise an xa_state on the stack.
 */
#define XA_STATE(name, array, index) \
	struct xa_state name = __XA_STATE(array, index, 0, 0)

#define xas_lock_irq(xas) xa_lock_irq((xas)->xa)
#define xas_unlock_irq(xas) xa_unlock_irq((xas)->xa)

/**
 * xas_invalid() - Is the xas in a retry or error state?
 * @xas: XArray operation state.
 *
 * Return: %true if the xas cannot be used for operations.
 */
static inline bool xas_invalid(const struct xa_state *xas)
{
	return (unsigned long)xas->xa_node & 3;
}

/**
 * xas_valid() - Is the xas a valid cursor into the array?
 * @xas: XArray operation state.
 *
 * Return: %true if the xas can be used for operations.
 */
static inline bool xas_valid(const struct xa_state *xas)
{
	return !xas_invalid(xas);
}

/* True if the node represents RESTART or an error */
static inline bool xas_frozen(struct xa_node *node)
{
	return (unsigned long)node & 2;
}

/* True if the node represents head-of-tree, RESTART or BOUNDS */
static inline bool xas_top(struct xa_node *node)
{
	return node <= XAS_RESTART;
}

/**
 * xas_reset() - Reset an XArray operation state.
 * @xas: XArray operation state.
 *
 * Resets the error or walk state of the @xas so future walks of the
 * array will start from the root.  Use this if you have dropped the
 * xarray lock and want to reuse the xa_state.
 *
 * Context: Any context.
 */
static inline void xas_reset(struct xa_state *xas)
{
	xas->xa_node = XAS_RESTART;
}

void *xas_load(struct xa_state *);
void *xas_store(struct xa_state *, void *entry);

/**
 * xas_reload() - Refetch an entry from the xarray.
 * @xas: XArray operation state.
 *
 * Use this function to check that a previously loaded entry still has
 * the same value.  This is useful for the lockless pagecache lookup where
 * we walk the array with only the RCU lock to protect us, lock the page,
 * then check that the page hasn't moved since we looked it up.
 *
 * The caller guarantees that @xas is still valid.  If it may be in an
 * error or restart state, call xas_load() instead.
 *
 * Return: The entry at this location in the xarray.
 */
static inline void *xas_reload(struct xa_state *xas)
{
	return xa_head(xas->xa);
}

/**
 * xas_set() - Set up XArray operation state for a different index.
 * @xas: XArray operation state.
 * @index: New index into the XArray.
 *
 * Move the operation state to refer to a different index.  This will
 * have the effect of starting a walk from the top; see xas_next()
 * to move to an adjacent index.
 */
static inline void xas_set(struct xa_state *xas, unsigned long index)
{
	xas->xa_index = index;
	xas->xa_node = XAS_RESTART;
}

/**
 * xas_advance() - Skip over sibling entries.
 * @xas: XArray operation state.
 * @index: Index of last sibling entry.
 *
 * Move the operation state to refer to the last sibling entry.
 * This is useful for loops that normally want to see sibling
 * entries but sometimes want to skip them.  Use xas_set() if you
 * want to move to an index which is not part of this entry.
 */
static inline void xas_advance(struct xa_state *xas, unsigned long index)
{
	xas->xa_index = index;
}

/**
 * xas_set_order() - Set up XArray operation state for a multislot entry.
 * @xas: XArray operation state.
 * @index: Target of the operation.
 * @order: Entry occupies 2^@order indices.
 */
static inline void xas_set_order(struct xa_state *xas, unsigned long index,
				 unsigned int order)
{
	BUG_ON(order > 0);
	xas_set(xas, index);
}

void *__xas_next(struct xa_state *);

/**
 * xas_next() - Move state to next index.
 * @xas: XArray operation state.
 *
 * If the @xas was in an error state, it will remain in an error state
 * and this function will return %NULL.  If the @xas has never been walked,
 * it will have the effect of calling xas_load().  Otherwise one will be
 * added to the index and the state will be walked to the correct
 * location in the array for the next operation.
 *
 * If the iterator was referencing index %ULONG_MAX, this function wraps
 * around to 0.
 *
 * Return: The entry at the new index.  This may be %NULL or an internal
 * entry.
 */
static inline void *xas_next(struct xa_state *xas)
{
	return __xas_next(xas);
}

#endif /* _LINUX_XARRAY_H */
