#include <linux/uaccess.h>

#ifndef INLINE_COPY_TO_USER
unsigned long _copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (likely(access_ok(to, n)))
		n = raw_copy_to_user(to, from, n);
	return n;
}
#endif

/* check_zeroed_user moved to inline in uaccess.h */
