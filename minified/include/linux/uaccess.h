#ifndef __LINUX_UACCESS_H__
#define __LINUX_UACCESS_H__

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/minmax.h>
#include <linux/sched.h>
#include <linux/thread_info.h>

#include <linux/compiler.h>
#include <linux/string.h>
#include <asm/asm.h>
#include <asm/page.h>
#include <asm/cpufeatures.h>
#include <asm/alternative.h>

#define __ASM_CLAC	".byte 0x0f,0x01,0xca"
#define __ASM_STAC	".byte 0x0f,0x01,0xcb"

static __always_inline void clac(void)
{
	alternative("", __ASM_CLAC, X86_FEATURE_SMAP);
}
static __always_inline void stac(void)
{
	alternative("", __ASM_STAC, X86_FEATURE_SMAP);
}
#define ASM_CLAC \
	ALTERNATIVE("", __ASM_CLAC, X86_FEATURE_SMAP)
#define ASM_STAC \
	ALTERNATIVE("", __ASM_STAC, X86_FEATURE_SMAP)
#include <asm/extable.h>

# define WARN_ON_IN_IRQ()

#ifndef TASK_SIZE_MAX
#define TASK_SIZE_MAX			TASK_SIZE
#endif

static inline int __access_ok(const void __user *ptr, unsigned long size)
{
	unsigned long limit = TASK_SIZE_MAX;
	unsigned long addr = (unsigned long)ptr;
	return (size <= limit) && (addr <= (limit - size));
}

#define access_ok(addr, size)					\
({									\
	WARN_ON_IN_IRQ();						\
	likely(__access_ok(addr, size));				\
})

extern int __get_user_1(void);
extern int __get_user_2(void);
extern int __get_user_4(void);
extern int __get_user_8(void);
extern int __get_user_nocheck_1(void);
extern int __get_user_nocheck_2(void);
extern int __get_user_nocheck_4(void);
extern int __get_user_nocheck_8(void);
extern int __get_user_bad(void);

#define __uaccess_end()   clac()
#define __uaccess_begin_nospec()	\
({					\
	stac();				\
	barrier_nospec();		\
})

#define __inttype(x) __typeof__(		\
	__typefits(x,char,			\
	  __typefits(x,short,			\
	    __typefits(x,int,			\
	      __typefits(x,long,0ULL)))))

#define __typefits(x,type,not) \
	__builtin_choose_expr(sizeof(x)<=sizeof(type),(unsigned type)0,not)

#define do_get_user_call(fn,x,ptr)					\
({									\
	int __ret_gu;							\
	register __inttype(*(ptr)) __val_gu asm("%"_ASM_DX);		\
	__chk_user_ptr(ptr);						\
	asm volatile("call __" #fn "_%P4"				\
		     : "=a" (__ret_gu), "=r" (__val_gu),		\
			ASM_CALL_CONSTRAINT				\
		     : "0" (ptr), "i" (sizeof(*(ptr))));		\
	(x) = (__force __typeof__(*(ptr))) __val_gu;			\
	__builtin_expect(__ret_gu, 0);					\
})

#define get_user(x,ptr) do_get_user_call(get_user,x,ptr)
#define __get_user(x,ptr) do_get_user_call(get_user_nocheck,x,ptr)

#define __put_user_goto_u64(x, addr, label)			\
	asm_volatile_goto("\n"					\
		     "1:	movl %%eax,0(%1)\n"		\
		     "2:	movl %%edx,4(%1)\n"		\
		     _ASM_EXTABLE_UA(1b, %l2)			\
		     _ASM_EXTABLE_UA(2b, %l2)			\
		     : : "A" (x), "r" (addr)			\
		     : : label)

extern void __put_user_bad(void);
extern void __put_user_1(void);
extern void __put_user_2(void);
extern void __put_user_4(void);
extern void __put_user_8(void);
extern void __put_user_nocheck_1(void);
extern void __put_user_nocheck_2(void);
extern void __put_user_nocheck_4(void);
extern void __put_user_nocheck_8(void);

#define do_put_user_call(fn,x,ptr)					\
({									\
	int __ret_pu;							\
	void __user *__ptr_pu;						\
	register __typeof__(*(ptr)) __val_pu asm("%"_ASM_AX);		\
	__chk_user_ptr(ptr);						\
	__ptr_pu = (ptr);						\
	__val_pu = (x);							\
	asm volatile("call __" #fn "_%P[size]"				\
		     : "=c" (__ret_pu),					\
			ASM_CALL_CONSTRAINT				\
		     : "0" (__ptr_pu),					\
		       "r" (__val_pu),					\
		       [size] "i" (sizeof(*(ptr)))			\
		     :"ebx");						\
	__builtin_expect(__ret_pu, 0);					\
})

#define put_user(x, ptr) do_put_user_call(put_user,x,ptr)
#define __put_user(x, ptr) do_put_user_call(put_user_nocheck,x,ptr)

#define __put_user_size(x, ptr, size, label)				\
do {									\
	__chk_user_ptr(ptr);						\
	switch (size) {							\
	case 1:								\
		__put_user_goto(x, ptr, "b", "iq", label);		\
		break;							\
	case 2:								\
		__put_user_goto(x, ptr, "w", "ir", label);		\
		break;							\
	case 4:								\
		__put_user_goto(x, ptr, "l", "ir", label);		\
		break;							\
	case 8:								\
		__put_user_goto_u64(x, ptr, label);			\
		break;							\
	default:							\
		__put_user_bad();					\
	}								\
} while (0)

#define __get_user_asm_u64(x, ptr, label) do {				\
	unsigned int __gu_low, __gu_high;				\
	const unsigned int __user *__gu_ptr;				\
	__gu_ptr = (const void __user *)(ptr);				\
	__get_user_asm(__gu_low, __gu_ptr, "l", "=r", label);		\
	__get_user_asm(__gu_high, __gu_ptr+1, "l", "=r", label);	\
	(x) = ((unsigned long long)__gu_high << 32) | __gu_low;		\
} while (0)

#define __get_user_size(x, ptr, size, label)				\
do {									\
	__chk_user_ptr(ptr);						\
	switch (size) {							\
	case 1:	{							\
		unsigned char x_u8__;					\
		__get_user_asm(x_u8__, ptr, "b", "=q", label);		\
		(x) = x_u8__;						\
		break;							\
	}								\
	case 2:								\
		__get_user_asm(x, ptr, "w", "=r", label);		\
		break;							\
	case 4:								\
		__get_user_asm(x, ptr, "l", "=r", label);		\
		break;							\
	case 8:								\
		__get_user_asm_u64(x, ptr, label);			\
		break;							\
	default:							\
		(x) = __get_user_bad();					\
	}								\
} while (0)

#define __get_user_asm(x, addr, itype, ltype, label)			\
	asm_volatile_goto("\n"						\
		     "1:	mov"itype" %[umem],%[output]\n"		\
		     _ASM_EXTABLE_UA(1b, %l2)				\
		     : [output] ltype(x)				\
		     : [umem] "m" (__m(addr))				\
		     : : label)

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct __user *)(x))

#define __put_user_goto(x, addr, itype, ltype, label)			\
	asm_volatile_goto("\n"						\
		"1:	mov"itype" %0,%1\n"				\
		_ASM_EXTABLE_UA(1b, %l2)				\
		: : ltype(x), "m" (__m(addr))				\
		: : label)

extern __must_check long strnlen_user(const char __user *str, long n);
unsigned long __must_check clear_user(void __user *mem, unsigned long len);

unsigned long __must_check __copy_user_ll
		(void *to, const void *from, unsigned long n);

static __always_inline unsigned long __must_check
raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return __copy_user_ll((__force void *)to, from, n);
}

static __must_check __always_inline bool user_access_begin(const void __user *ptr, size_t len)
{
	if (unlikely(!access_ok(ptr,len)))
		return 0;
	__uaccess_begin_nospec();
	return 1;
}
#define user_access_begin(a,b)	user_access_begin(a,b)
#define user_access_end()	__uaccess_end()

#define unsafe_put_user(x, ptr, label)	\
	__put_user_size((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)), label)

#define unsafe_get_user(x, ptr, err_label)					\
do {										\
	__inttype(*(ptr)) __gu_val;						\
	__get_user_size(__gu_val, (ptr), sizeof(*(ptr)), err_label);		\
	(x) = (__force __typeof__(*(ptr)))__gu_val;				\
} while (0)

#define __put_kernel_nofault(dst, src, type, err_label)			\
	__put_user_size(*((type *)(src)), (__force type __user *)(dst),	\
			sizeof(type), err_label)

/* End inlined from asm/uaccess.h */

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

long notrace copy_to_kernel_nofault(void *dst, const void *src, size_t size);

#ifndef user_read_access_begin
#define user_read_access_begin user_access_begin
#define user_read_access_end user_access_end
#endif

#endif
