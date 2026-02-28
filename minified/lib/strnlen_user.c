#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/mm.h>

static __always_inline long
do_strnlen_user(const char __user *src, unsigned long count, unsigned long max)
{
	unsigned long res = 0;
	char c;

	while (res < max) {
		unsafe_get_user(c, src + res, efault);
		res++;
		if (!c)
			return res;
	}
	if (res >= count)
		return count + 1;
efault:
	return 0;
}

long strnlen_user(const char __user *str, long count)
{
	unsigned long max_addr, src_addr;

	if (unlikely(count <= 0))
		return 0;

	max_addr = TASK_SIZE_MAX;
	src_addr = (unsigned long)untagged_addr(str);
	if (likely(src_addr < max_addr)) {
		unsigned long max = max_addr - src_addr;
		long retval;

		if (max > count)
			max = count;

		if (user_read_access_begin(str, max)) {
			retval = do_strnlen_user(str, count, max);
			user_read_access_end();
			return retval;
		}
	}
	return 0;
}
