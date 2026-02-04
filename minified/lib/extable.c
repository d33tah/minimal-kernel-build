
/* linux/module.h removed - no module features used */
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

/* cmp_ex_search inlined into search_extable - single caller */

/* bsearch inlined into search_extable - was its only caller */
const struct exception_table_entry *
search_extable(const struct exception_table_entry *base, const size_t num,
	       unsigned long value)
{
	const struct exception_table_entry *pivot;
	const struct exception_table_entry *b = base;
	size_t n = num;

	while (n > 0) {
		pivot = b + (n >> 1);
		/* cmp_ex_search inlined */
		if (value > ex_to_insn(pivot)) {
			b = pivot + 1;
			n--;
		} else if (value < ex_to_insn(pivot)) {
			/* fall through */
		} else {
			return pivot;
		}
		n >>= 1;
	}

	return NULL;
}
