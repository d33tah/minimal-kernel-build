
#include <linux/types.h>
#include <linux/stddef.h>
void sort(void *base, size_t num, size_t size, cmp_func_t cmp_func,
	  swap_func_t swap_func);

void sort(void *base, size_t num, size_t size, cmp_func_t cmp_func,
	  swap_func_t swap_func)
{
	size_t i, j;
	char tmp[32]; /* enough for any struct we sort */

	for (i = 1; i < num; i++) {
		for (j = i; j > 0; j--) {
			void *a = base + (j - 1) * size;
			void *b = base + j * size;
			if (cmp_func(a, b) <= 0)
				break;
			__builtin_memcpy(tmp, a, size);
			__builtin_memcpy(a, b, size);
			__builtin_memcpy(b, tmp, size);
		}
	}
}
