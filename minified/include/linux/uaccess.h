#ifndef __LINUX_UACCESS_H__
#define __LINUX_UACCESS_H__

#include <linux/types.h>
#include <linux/stddef.h>
static inline bool should_fail_usercopy(void) { return false; }
#include <linux/instrumented.h>
#include <linux/minmax.h>
#include <linux/sched.h>
#include <linux/thread_info.h>

#include <asm/uaccess.h>


static __always_inline __must_check unsigned long
__copy_from_user_inatomic(void *to, const void __user *from, unsigned long n)
{
	instrument_copy_from_user(to, from, n);
	check_object_size(to, n, false);
	return raw_copy_from_user(to, from, n);
}

static __always_inline __must_check unsigned long
__copy_from_user(void *to, const void __user *from, unsigned long n)
{
	might_fault();
	if (should_fail_usercopy())
		return n;
	instrument_copy_from_user(to, from, n);
	check_object_size(to, n, false);
	return raw_copy_from_user(to, from, n);
}

static __always_inline __must_check unsigned long
__copy_to_user_inatomic(void __user *to, const void *from, unsigned long n)
{
	if (should_fail_usercopy())
		return n;
	instrument_copy_to_user(to, from, n);
	check_object_size(from, n, true);
	return raw_copy_to_user(to, from, n);
}

static __always_inline __must_check unsigned long
__copy_to_user(void __user *to, const void *from, unsigned long n)
{
	might_fault();
	if (should_fail_usercopy())
		return n;
	instrument_copy_to_user(to, from, n);
	check_object_size(from, n, true);
	return raw_copy_to_user(to, from, n);
}

/* INLINE_COPY_* not defined - use extern declarations */
extern __must_check unsigned long
_copy_from_user(void *, const void __user *, unsigned long);
extern __must_check unsigned long
_copy_to_user(void __user *, const void *, unsigned long);

static __always_inline unsigned long __must_check
copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (likely(check_copy_size(to, n, false)))
		n = _copy_from_user(to, from, n);
	return n;
}

static __always_inline unsigned long __must_check
copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (likely(check_copy_size(from, n, true)))
		n = _copy_to_user(to, from, n);
	return n;
}

#ifndef copy_mc_to_kernel
static inline unsigned long __must_check
copy_mc_to_kernel(void *dst, const void *src, size_t cnt)
{
	memcpy(dst, src, cnt);
	return 0;
}
#endif

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


/* __copy_from_user_inatomic_nocache fallback removed - ARCH_HAS_NOCACHE_UACCESS defined and never called */

static inline __must_check int check_zeroed_user(const void __user *from, size_t size) { return 1; }

static __always_inline __must_check int
copy_struct_from_user(void *dst, size_t ksize, const void __user *src,
		      size_t usize)
{
	size_t size = min(ksize, usize);
	size_t rest = max(ksize, usize) - size;

	 
	if (usize < ksize) {
		memset(dst + size, 0, rest);
	} else if (usize > ksize) {
		int ret = check_zeroed_user(src + size, rest);
		if (ret <= 0)
			return ret ?: -E2BIG;
	}
	 
	if (copy_from_user(dst, src, size))
		return -EFAULT;
	return 0;
}

bool copy_from_kernel_nofault_allowed(const void *unsafe_src, size_t size);

long copy_from_kernel_nofault(void *dst, const void *src, size_t size);
long notrace copy_to_kernel_nofault(void *dst, const void *src, size_t size);

/* Removed uncalled: copy_from_user_nofault, copy_to_user_nofault,
 * strncpy_from_kernel_nofault, strncpy_from_user_nofault, strnlen_user_nofault */

#ifndef __get_kernel_nofault
#define __get_kernel_nofault(dst, src, type, label)	\
do {							\
	type __user *p = (type __force __user *)(src);	\
	type data;					\
	if (__get_user(data, p))			\
		goto label;				\
	*(type *)dst = data;				\
} while (0)

#define __put_kernel_nofault(dst, src, type, label)	\
do {							\
	type __user *p = (type __force __user *)(dst);	\
	type data = *(type *)src;			\
	if (__put_user(data, p))			\
		goto label;				\
} while (0)
#endif

#define get_kernel_nofault(val, ptr) ({				\
	const typeof(val) *__gk_ptr = (ptr);			\
	copy_from_kernel_nofault(&(val), __gk_ptr, sizeof(val));\
})

#ifndef user_access_begin
#define user_access_begin(ptr,len) access_ok(ptr, len)
#define user_access_end() do { } while (0)
#define unsafe_op_wrap(op, err) do { if (unlikely(op)) goto err; } while (0)
#define unsafe_get_user(x,p,e) unsafe_op_wrap(__get_user(x,p),e)
#define unsafe_put_user(x,p,e) unsafe_op_wrap(__put_user(x,p),e)
#define unsafe_copy_to_user(d,s,l,e) unsafe_op_wrap(__copy_to_user(d,s,l),e)
#define unsafe_copy_from_user(d,s,l,e) unsafe_op_wrap(__copy_from_user(d,s,l),e)
#endif
#ifndef user_write_access_begin
#define user_write_access_begin user_access_begin
#define user_write_access_end user_access_end
#endif
#ifndef user_read_access_begin
#define user_read_access_begin user_access_begin
#define user_read_access_end user_access_end
#endif


#endif		 
