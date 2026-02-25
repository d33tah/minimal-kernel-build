
#include <linux/types.h>
#include <linux/stddef.h>
void sort(void *base, size_t num, size_t size, cmp_func_t cmp_func,
	  swap_func_t swap_func);

static void swap_bytes(void *a, void *b, size_t n)
{
	do {
		char t = ((char *)a)[--n];
		((char *)a)[n] = ((char *)b)[n];
		((char *)b)[n] = t;
	} while (n);
}

static void swap_words(void *a, void *b, size_t n)
{
	do {
		u32 t = *(u32 *)(a + (n -= 4));
		*(u32 *)(a + n) = *(u32 *)(b + n);
		*(u32 *)(b + n) = t;
	} while (n);
}

__attribute_const__ __always_inline static size_t
parent(size_t i, unsigned int lsbit, size_t size)
{
	i -= size;
	i -= size & -(i & lsbit);
	return i / 2;
}

void sort(void *base, size_t num, size_t size, cmp_func_t cmp_func,
	  swap_func_t swap_func)
{
	size_t n = num * size, a = (num / 2) * size;
	const unsigned int lsbit = size & -size;

	if (!a)
		return;

	for (;;) {
		size_t b, c, d;

		if (a)
			a -= size;
		else if (n -= size) {
			if (swap_func)
				swap_func(base, base + n, (int)size);
			else if (size % 4 == 0)
				swap_words(base, base + n, size);
			else
				swap_bytes(base, base + n, size);
		} else
			break;

		for (b = a; c = 2 * b + size, (d = c + size) < n;)
			b = cmp_func(base + c, base + d) >= 0 ? c : d;
		if (d == n)
			b = c;

		while (b != a && cmp_func(base + a, base + b) >= 0)
			b = parent(b, lsbit, size);
		c = b;
		while (b != a) {
			b = parent(b, lsbit, size);
			if (swap_func)
				swap_func(base + b, base + c, (int)size);
			else if (size % 4 == 0)
				swap_words(base + b, base + c, size);
			else
				swap_bytes(base + b, base + c, size);
		}
	}
}
