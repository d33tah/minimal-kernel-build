#ifndef _LINUX_SORT_H
#define _LINUX_SORT_H
#include <linux/types.h>
/* sort_r declaration removed - only called internally from sort() */
void sort(void *base, size_t num, size_t size, cmp_func_t cmp_func, swap_func_t swap_func);
#endif
