/* Binary search */
#include <linux/bsearch.h>

#define NOKPROBE_SYMBOL(fname) /* stub for disabled kprobes */

void *bsearch(const void *key, const void *base, size_t num, size_t size,
	      cmp_func_t cmp)
{
	return __inline_bsearch(key, base, num, size, cmp);
}
NOKPROBE_SYMBOL(bsearch);
