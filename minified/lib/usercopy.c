#include <linux/instrumented.h>
#include <linux/uaccess.h>

/* _copy_from_user removed - copy_from_user never called */

#ifndef INLINE_COPY_TO_USER
unsigned long _copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (likely(access_ok(to, n))) {
		instrument_copy_to_user(to, from, n);
		n = raw_copy_to_user(to, from, n);
	}
	return n;
}
#endif

/* check_zeroed_user moved to inline in uaccess.h */
