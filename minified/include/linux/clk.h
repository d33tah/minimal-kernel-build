/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/include/linux/clk.h
 *
 *  Copyright (C) 2004 ARM Limited.
 *  Written by Deep Blue Solutions Limited.
 *  Copyright (C) 2011-2012 Linaro Ltd <mturquette@linaro.org>
 */
#ifndef __LINUX_CLK_H
#define __LINUX_CLK_H

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/notifier.h>

struct device;
struct clk;
struct device_node;
struct of_phandle_args;

/**
 * DOC: clk notifier callback types
 *
 * PRE_RATE_CHANGE - called immediately before the clk rate is changed,
 *     to indicate that the rate change will proceed.  Drivers must
 *     immediately terminate any operations that will be affected by the
 *     rate change.  Callbacks may either return NOTIFY_DONE, NOTIFY_OK,
 *     NOTIFY_STOP or NOTIFY_BAD.
 *
 * ABORT_RATE_CHANGE: called if the rate change failed for some reason
 *     after PRE_RATE_CHANGE.  In this case, all registered notifiers on
 *     the clk will be called with ABORT_RATE_CHANGE. Callbacks must
 *     always return NOTIFY_DONE or NOTIFY_OK.
 *
 * POST_RATE_CHANGE - called after the clk rate change has successfully
 *     completed.  Callbacks must always return NOTIFY_DONE or NOTIFY_OK.
 *
 */
#define PRE_RATE_CHANGE			BIT(0)
#define POST_RATE_CHANGE		BIT(1)
#define ABORT_RATE_CHANGE		BIT(2)

/**
 * struct clk_notifier - associate a clk with a notifier
 * @clk: struct clk * to associate the notifier with
 * @notifier_head: a blocking_notifier_head for this clk
 * @node: linked list pointers
 *
 * A list of struct clk_notifier is maintained by the notifier code.
 * An entry is created whenever code registers the first notifier on a
 * particular @clk.  Future notifiers on that @clk are added to the
 * @notifier_head.
 */
struct clk_notifier {
	struct clk			*clk;
	struct srcu_notifier_head	notifier_head;
	struct list_head		node;
};

/**
 * struct clk_notifier_data - rate data to pass to the notifier callback
 * @clk: struct clk * being changed
 * @old_rate: previous rate of this clk
 * @new_rate: new rate of this clk
 *
 * For a pre-notifier, old_rate is the clk's rate before this rate
 * change, and new_rate is what the rate will be in the future.  For a
 * post-notifier, old_rate and new_rate are both set to the clk's
 * current rate (this was done to optimize the implementation).
 */
struct clk_notifier_data {
	struct clk		*clk;
	unsigned long		old_rate;
	unsigned long		new_rate;
};

/**
 * struct clk_bulk_data - Data used for bulk clk operations.
 *
 * @id: clock consumer ID
 * @clk: struct clk * to store the associated clock
 *
 * The CLK APIs provide a series of clk_bulk_() API calls as
 * a convenience to consumers which require multiple clks.  This
 * structure is used to manage data for these calls.
 */
struct clk_bulk_data {
	const char		*id;
	struct clk		*clk;
};


static inline int clk_notifier_register(struct clk *clk,
					struct notifier_block *nb)
{
	return -ENOTSUPP;
}

static inline int clk_notifier_unregister(struct clk *clk,
					  struct notifier_block *nb)
{
	return -ENOTSUPP;
}

static inline int devm_clk_notifier_register(struct device *dev,
					     struct clk *clk,
					     struct notifier_block *nb)
{
	return -ENOTSUPP;
}

static inline long clk_get_accuracy(struct clk *clk)
{
	return -ENOTSUPP;
}

static inline long clk_set_phase(struct clk *clk, int phase)
{
	return -ENOTSUPP;
}

static inline long clk_get_phase(struct clk *clk)
{
	return -ENOTSUPP;
}

static inline int clk_set_duty_cycle(struct clk *clk, unsigned int num,
				     unsigned int den)
{
	return -ENOTSUPP;
}

static inline unsigned int clk_get_scaled_duty_cycle(struct clk *clk,
						     unsigned int scale)
{
	return 0;
}

static inline bool clk_is_match(const struct clk *p, const struct clk *q)
{
	return p == q;
}


static inline int clk_prepare(struct clk *clk)
{
	might_sleep();
	return 0;
}

static inline int __must_check
clk_bulk_prepare(int num_clks, const struct clk_bulk_data *clks)
{
	might_sleep();
	return 0;
}

static inline bool clk_is_enabled_when_prepared(struct clk *clk)
{
	return false;
}

/**
 * clk_unprepare - undo preparation of a clock source
 * @clk: clock source
 *
 * This undoes a previously prepared clock.  The caller must balance
 * the number of prepare and unprepare calls.
 *
 * Must not be called from within atomic context.
 */
static inline void clk_unprepare(struct clk *clk)
{
	might_sleep();
}
static inline void clk_bulk_unprepare(int num_clks,
				      const struct clk_bulk_data *clks)
{
	might_sleep();
}


static inline struct clk *clk_get(struct device *dev, const char *id)
{
	return NULL;
}

static inline int __must_check clk_bulk_get(struct device *dev, int num_clks,
					    struct clk_bulk_data *clks)
{
	return 0;
}

static inline int __must_check clk_bulk_get_optional(struct device *dev,
				int num_clks, struct clk_bulk_data *clks)
{
	return 0;
}

static inline int __must_check clk_bulk_get_all(struct device *dev,
					 struct clk_bulk_data **clks)
{
	return 0;
}

static inline struct clk *devm_clk_get(struct device *dev, const char *id)
{
	return NULL;
}

static inline struct clk *devm_clk_get_optional(struct device *dev,
						const char *id)
{
	return NULL;
}

static inline int __must_check devm_clk_bulk_get(struct device *dev, int num_clks,
						 struct clk_bulk_data *clks)
{
	return 0;
}

static inline int __must_check devm_clk_bulk_get_optional(struct device *dev,
				int num_clks, struct clk_bulk_data *clks)
{
	return 0;
}

static inline int __must_check devm_clk_bulk_get_all(struct device *dev,
						     struct clk_bulk_data **clks)
{

	return 0;
}

static inline struct clk *devm_get_clk_from_child(struct device *dev,
				struct device_node *np, const char *con_id)
{
	return NULL;
}

static inline void clk_put(struct clk *clk) {}

static inline void clk_bulk_put(int num_clks, struct clk_bulk_data *clks) {}

static inline void clk_bulk_put_all(int num_clks, struct clk_bulk_data *clks) {}

static inline void devm_clk_put(struct device *dev, struct clk *clk) {}


static inline int clk_rate_exclusive_get(struct clk *clk)
{
	return 0;
}

static inline void clk_rate_exclusive_put(struct clk *clk) {}

static inline int clk_enable(struct clk *clk)
{
	return 0;
}

static inline int __must_check clk_bulk_enable(int num_clks,
					       const struct clk_bulk_data *clks)
{
	return 0;
}

static inline void clk_disable(struct clk *clk) {}


static inline void clk_bulk_disable(int num_clks,
				    const struct clk_bulk_data *clks) {}

static inline unsigned long clk_get_rate(struct clk *clk)
{
	return 0;
}

static inline int clk_set_rate(struct clk *clk, unsigned long rate)
{
	return 0;
}

static inline int clk_set_rate_exclusive(struct clk *clk, unsigned long rate)
{
	return 0;
}

static inline long clk_round_rate(struct clk *clk, unsigned long rate)
{
	return 0;
}

static inline bool clk_has_parent(struct clk *clk, struct clk *parent)
{
	return true;
}

static inline int clk_set_rate_range(struct clk *clk, unsigned long min,
				     unsigned long max)
{
	return 0;
}

static inline int clk_set_min_rate(struct clk *clk, unsigned long rate)
{
	return 0;
}

static inline int clk_set_max_rate(struct clk *clk, unsigned long rate)
{
	return 0;
}

static inline int clk_set_parent(struct clk *clk, struct clk *parent)
{
	return 0;
}

static inline struct clk *clk_get_parent(struct clk *clk)
{
	return NULL;
}

static inline struct clk *clk_get_sys(const char *dev_id, const char *con_id)
{
	return NULL;
}

static inline int clk_save_context(void)
{
	return 0;
}

static inline void clk_restore_context(void) {}


/* clk_prepare_enable helps cases using clk_enable in non-atomic context. */
static inline int clk_prepare_enable(struct clk *clk)
{
	int ret;

	ret = clk_prepare(clk);
	if (ret)
		return ret;
	ret = clk_enable(clk);
	if (ret)
		clk_unprepare(clk);

	return ret;
}

/* clk_disable_unprepare helps cases using clk_disable in non-atomic context. */
static inline void clk_disable_unprepare(struct clk *clk)
{
	clk_disable(clk);
	clk_unprepare(clk);
}

static inline int __must_check
clk_bulk_prepare_enable(int num_clks, const struct clk_bulk_data *clks)
{
	int ret;

	ret = clk_bulk_prepare(num_clks, clks);
	if (ret)
		return ret;
	ret = clk_bulk_enable(num_clks, clks);
	if (ret)
		clk_bulk_unprepare(num_clks, clks);

	return ret;
}

static inline void clk_bulk_disable_unprepare(int num_clks,
					      const struct clk_bulk_data *clks)
{
	clk_bulk_disable(num_clks, clks);
	clk_bulk_unprepare(num_clks, clks);
}

/**
 * clk_drop_range - Reset any range set on that clock
 * @clk: clock source
 *
 * Returns success (0) or negative errno.
 */
static inline int clk_drop_range(struct clk *clk)
{
	return clk_set_rate_range(clk, 0, ULONG_MAX);
}

/**
 * clk_get_optional - lookup and obtain a reference to an optional clock
 *		      producer.
 * @dev: device for clock "consumer"
 * @id: clock consumer ID
 *
 * Behaves the same as clk_get() except where there is no clock producer. In
 * this case, instead of returning -ENOENT, the function returns NULL.
 */
static inline struct clk *clk_get_optional(struct device *dev, const char *id)
{
	struct clk *clk = clk_get(dev, id);

	if (clk == ERR_PTR(-ENOENT))
		return NULL;

	return clk;
}

static inline struct clk *of_clk_get(struct device_node *np, int index)
{
	return ERR_PTR(-ENOENT);
}
static inline struct clk *of_clk_get_by_name(struct device_node *np,
					     const char *name)
{
	return ERR_PTR(-ENOENT);
}
static inline struct clk *of_clk_get_from_provider(struct of_phandle_args *clkspec)
{
	return ERR_PTR(-ENOENT);
}

#endif
