#include <linux/bitops.h>
#include <linux/fault-inject-usercopy.h>
#include <linux/instrumented.h>
#include <linux/uaccess.h>


#ifndef INLINE_COPY_FROM_USER
unsigned long _copy_from_user(void *to, const void __user *from, unsigned long n)
{
	unsigned long res = n;
	might_fault();
	if (!should_fail_usercopy() && likely(access_ok(from, n))) {
		instrument_copy_from_user(to, from, n);
		res = raw_copy_from_user(to, from, n);
	}
	if (unlikely(res))
		memset(to + (n - res), 0, res);
	return res;
}
#endif

#ifndef INLINE_COPY_TO_USER
unsigned long _copy_to_user(void __user *to, const void *from, unsigned long n)
{
	might_fault();
	if (should_fail_usercopy())
		return n;
	if (likely(access_ok(to, n))) {
		instrument_copy_to_user(to, from, n);
		n = raw_copy_to_user(to, from, n);
	}
	return n;
}
#endif

int check_zeroed_user(const void __user *from, size_t size)
{
	 
	return 0;
}
