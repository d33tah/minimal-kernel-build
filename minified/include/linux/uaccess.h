#ifndef __LINUX_UACCESS_H__
#define __LINUX_UACCESS_H__

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/instrumented.h>
#include <linux/minmax.h>
#include <linux/sched.h>
#include <linux/thread_info.h>

#include <asm/uaccess.h>


/* __copy_from_user_inatomic, __copy_from_user, __copy_to_user removed - never called */
/* copy_from_user, _copy_from_user removed - never called */

extern __must_check unsigned long
_copy_to_user(void __user *, const void *, unsigned long);

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


/* copy_from_kernel_nofault_allowed, copy_from_kernel_nofault removed - never called */
long notrace copy_to_kernel_nofault(void *dst, const void *src, size_t size);

/* Removed uncalled: copy_from_user_nofault, copy_to_user_nofault,
 * strncpy_from_kernel_nofault, strncpy_from_user_nofault, strnlen_user_nofault */

/* __get_kernel_nofault, get_kernel_nofault removed - never called */

#ifndef __put_kernel_nofault
#define __put_kernel_nofault(dst, src, type, label)	\
do {							\
	type __user *p = (type __force __user *)(dst);	\
	type data = *(type *)src;			\
	if (__put_user(data, p))			\
		goto label;				\
} while (0)
#endif

#ifndef user_access_begin
#define user_access_begin(ptr,len) access_ok(ptr, len)
#define user_access_end() do { } while (0)
#define unsafe_op_wrap(op, err) do { if (unlikely(op)) goto err; } while (0)
#define unsafe_get_user(x,p,e) unsafe_op_wrap(__get_user(x,p),e)
#define unsafe_put_user(x,p,e) unsafe_op_wrap(__put_user(x,p),e)
/* unsafe_copy_to_user, unsafe_copy_from_user removed - unused */
#endif
/* user_write_access_begin/end removed - unused */
#ifndef user_read_access_begin
#define user_read_access_begin user_access_begin
#define user_read_access_end user_access_end
#endif


#endif		 
