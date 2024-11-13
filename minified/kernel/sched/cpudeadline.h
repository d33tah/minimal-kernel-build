/* SPDX-License-Identifier: GPL-2.0 */

#define IDX_INVALID		-1

struct cpudl_item {
	u64			dl;
	int			cpu;
	int			idx;
};

struct cpudl {
	raw_spinlock_t		lock;
	int			size;
	cpumask_var_t		free_cpus;
	struct cpudl_item	*elements;
};

