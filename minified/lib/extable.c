
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sort.h>
#include <linux/uaccess.h>
#include <linux/extable.h>

/* ARCH_HAS_RELATIVE_EXTABLE is defined on x86 - using relative extable versions */
static inline unsigned long ex_to_insn(const struct exception_table_entry *x)
{
	return (unsigned long)&x->insn + x->insn;
}

static void swap_ex(void *a, void *b, int size)
{
	struct exception_table_entry *x = a, *y = b, tmp;
	int delta = b - a;

	tmp = *x;
	x->insn = y->insn + delta;
	y->insn = tmp.insn - delta;

	swap_ex_entry_fixup(x, y, tmp, delta);
}

static int cmp_ex_sort(const void *a, const void *b)
{
	const struct exception_table_entry *x = a, *y = b;

	if (ex_to_insn(x) > ex_to_insn(y))
		return 1;
	if (ex_to_insn(x) < ex_to_insn(y))
		return -1;
	return 0;
}

void sort_extable(struct exception_table_entry *start,
		  struct exception_table_entry *finish)
{
	sort(start, finish - start, sizeof(struct exception_table_entry),
	     cmp_ex_sort, swap_ex);
}

static int cmp_ex_search(const void *key, const void *elt)
{
	const struct exception_table_entry *_elt = elt;
	unsigned long _key = *(unsigned long *)key;

	if (_key > ex_to_insn(_elt))
		return 1;
	if (_key < ex_to_insn(_elt))
		return -1;
	return 0;
}

/* Merged from bsearch.c and bsearch.h */
#define NOKPROBE_SYMBOL(fname) /* stub for disabled kprobes */
static __always_inline void *__inline_bsearch(const void *key, const void *base,
					      size_t num, size_t size,
					      cmp_func_t cmp)
{
	const char *pivot;
	int result;

	while (num > 0) {
		pivot = base + (num >> 1) * size;
		result = cmp(key, pivot);

		if (result == 0)
			return (void *)pivot;

		if (result > 0) {
			base = pivot + size;
			num--;
		}
		num >>= 1;
	}

	return NULL;
}

void *bsearch(const void *key, const void *base, size_t num, size_t size,
	      cmp_func_t cmp)
{
	return __inline_bsearch(key, base, num, size, cmp);
}
NOKPROBE_SYMBOL(bsearch);

const struct exception_table_entry *
search_extable(const struct exception_table_entry *base, const size_t num,
	       unsigned long value)
{
	return bsearch(&value, base, num, sizeof(struct exception_table_entry),
		       cmp_ex_search);
}
