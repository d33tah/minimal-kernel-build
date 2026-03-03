#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/mm.h>

long strnlen_user(const char __user *str, long count)
{
	unsigned long max_addr, src_addr;

	if (unlikely(count <= 0))
		return 0;

	max_addr = TASK_SIZE_MAX;
	src_addr = (unsigned long)untagged_addr(str);
	if (likely(src_addr < max_addr)) {
		unsigned long max = max_addr - src_addr;
		unsigned long res = 0;
		char c;

		if (max > count)
			max = count;

		if (user_read_access_begin(str, max)) {
			while (res < max) {
				unsafe_get_user(c, str + res, efault);
				res++;
				if (!c) {
					user_read_access_end();
					return res;
				}
			}
			user_read_access_end();
			if (res >= count)
				return count + 1;
		}
	}
	return 0;
efault:
	user_read_access_end();
	return 0;
}
