// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/list_sort.h>
#include <linux/list.h>

void list_sort(void *priv, struct list_head *head, list_cmp_func_t cmp)
{
	/* Stub - do nothing */
}
EXPORT_SYMBOL(list_sort);
