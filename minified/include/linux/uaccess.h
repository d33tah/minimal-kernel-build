#ifndef __LINUX_UACCESS_H__
#define __LINUX_UACCESS_H__

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/instrumented.h>
#include <linux/minmax.h>
#include <linux/sched.h>
#include <linux/thread_info.h>

#include <asm/uaccess.h>


/*
 * x86 (asm/uaccess.h, included above) unconditionally #defines
 * INLINE_COPY_TO_USER, so the generic out-of-line extern #else arm was
 * statically dead in every TU; the inline is emitted unconditionally.
 */
static inline __must_check unsigned long
_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	might_fault();
	if (access_ok(to, n)) {
		instrument_copy_to_user(to, from, n);
		n = raw_copy_to_user(to, from, n);
	}
	return n;
}

static __always_inline unsigned long __must_check
copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (likely(check_copy_size(from, n, true)))
		n = _copy_to_user(to, from, n);
	return n;
}

static __always_inline void pagefault_disabled_inc(void)
{
	current->pagefault_disabled++;
}

static __always_inline void pagefault_disabled_dec(void)
{
	current->pagefault_disabled--;
}

static inline void pagefault_disable(void)
{
	pagefault_disabled_inc();
	 
	barrier();
}

static inline void pagefault_enable(void)
{
	 
	barrier();
	pagefault_disabled_dec();
}

static inline bool pagefault_disabled(void)
{
	return current->pagefault_disabled != 0;
}

#define faulthandler_disabled() (pagefault_disabled() || in_atomic())


long notrace copy_to_kernel_nofault(void *dst, const void *src, size_t size);

#ifndef user_read_access_begin
#define user_read_access_begin user_access_begin
#define user_read_access_end user_access_end
#endif


#endif		 
