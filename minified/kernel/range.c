#include <linux/minmax.h>
#include <linux/string.h>
struct range {
	u64 start;
	u64 end;
};
void sort(void *base, size_t num, size_t size, cmp_func_t cmp_func,
	  swap_func_t swap_func);

int add_range_with_merge(struct range *range, int az, int nr_range, u64 start,
			 u64 end)
{
	if (start >= end || nr_range >= az)
		return nr_range;

	range[nr_range].start = start;
	range[nr_range].end = end;
	return nr_range + 1;
}

static int cmp_range(const void *x1, const void *x2)
{
	const struct range *r1 = x1;
	const struct range *r2 = x2;

	if (r1->start < r2->start)
		return -1;
	if (r1->start > r2->start)
		return 1;
	return 0;
}

int clean_sort_range(struct range *range, int az)
{
	int i, nr_range = 0;

	for (i = 0; i < az; i++) {
		if (range[i].end)
			nr_range = i + 1;
	}
	sort(range, nr_range, sizeof(struct range), cmp_range, NULL);
	return nr_range;
}
