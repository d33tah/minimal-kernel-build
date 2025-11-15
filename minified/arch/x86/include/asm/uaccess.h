 
#ifndef _ASM_X86_UACCESS_H
#define _ASM_X86_UACCESS_H
 
#include <linux/compiler.h>
#include <linux/kasan-checks.h>
#include <linux/string.h>
#include <asm/asm.h>
#include <asm/page.h>
#include <asm/smap.h>
#include <asm/extable.h>

# define WARN_ON_IN_IRQ()

 
#define access_ok(addr, size)					\
({									\
	WARN_ON_IN_IRQ();						\
	likely(__access_ok(addr, size));				\
})

#include <asm-generic/access_ok.h>

extern int __get_user_1(void);
extern int __get_user_2(void);
extern int __get_user_4(void);
extern int __get_user_8(void);
extern int __get_user_nocheck_1(void);
extern int __get_user_nocheck_2(void);
extern int __get_user_nocheck_4(void);
extern int __get_user_nocheck_8(void);
extern int __get_user_bad(void);

#define __uaccess_begin() stac()
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

 
#define get_user(x,ptr) ({ might_fault(); do_get_user_call(get_user,x,ptr); })

 
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

 
#define put_user(x, ptr) ({ might_fault(); do_put_user_call(put_user,x,ptr); })

 
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


#define __try_cmpxchg_user_asm(itype, ltype, _ptr, _pold, _new, label)	({ \
	int __err = 0;							\
	bool success;							\
	__typeof__(_ptr) _old = (__typeof__(_ptr))(_pold);		\
	__typeof__(*(_ptr)) __old = *_old;				\
	__typeof__(*(_ptr)) __new = (_new);				\
	asm volatile("\n"						\
		     "1: " LOCK_PREFIX "cmpxchg"itype" %[new], %[ptr]\n"\
		     CC_SET(z)						\
		     "2:\n"						\
		     _ASM_EXTABLE_TYPE_REG(1b, 2b, EX_TYPE_EFAULT_REG,	\
					   %[errout])			\
		     : CC_OUT(z) (success),				\
		       [errout] "+r" (__err),				\
		       [ptr] "+m" (*_ptr),				\
		       [old] "+a" (__old)				\
		     : [new] ltype (__new)				\
		     : "memory");					\
	if (unlikely(__err))						\
		goto label;						\
	if (unlikely(!success))						\
		*_old = __old;						\
	likely(success);					})

 
#define __try_cmpxchg64_user_asm(_ptr, _pold, _new, label)	({	\
	int __result;							\
	__typeof__(_ptr) _old = (__typeof__(_ptr))(_pold);		\
	__typeof__(*(_ptr)) __old = *_old;				\
	__typeof__(*(_ptr)) __new = (_new);				\
	asm volatile("\n"						\
		     "1: " LOCK_PREFIX "cmpxchg8b %[ptr]\n"		\
		     "mov $0, %%ecx\n\t"				\
		     "setz %%cl\n"					\
		     "2:\n"						\
		     _ASM_EXTABLE_TYPE_REG(1b, 2b, EX_TYPE_EFAULT_REG, %%ecx) \
		     : [result]"=c" (__result),				\
		       "+A" (__old),					\
		       [ptr] "+m" (*_ptr)				\
		     : "b" ((u32)__new),				\
		       "c" ((u32)((u64)__new >> 32))			\
		     : "memory", "cc");					\
	if (unlikely(__result < 0))					\
		goto label;						\
	if (unlikely(!__result))					\
		*_old = __old;						\
	likely(__result);					})

 
struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct __user *)(x))

 
#define __put_user_goto(x, addr, itype, ltype, label)			\
	asm_volatile_goto("\n"						\
		"1:	mov"itype" %0,%1\n"				\
		_ASM_EXTABLE_UA(1b, %l2)				\
		: : ltype(x), "m" (__m(addr))				\
		: : label)

extern unsigned long
copy_from_user_nmi(void *to, const void __user *from, unsigned long n);
extern __must_check long
strncpy_from_user(char *dst, const char __user *src, long count);

extern __must_check long strnlen_user(const char __user *str, long n);

unsigned long __must_check clear_user(void __user *mem, unsigned long len);
unsigned long __must_check __clear_user(void __user *mem, unsigned long len);


 

#define ARCH_HAS_NOCACHE_UACCESS 1

# include <asm/uaccess_32.h>

 
static __must_check __always_inline bool user_access_begin(const void __user *ptr, size_t len)
{
	if (unlikely(!access_ok(ptr,len)))
		return 0;
	__uaccess_begin_nospec();
	return 1;
}
#define user_access_begin(a,b)	user_access_begin(a,b)
#define user_access_end()	__uaccess_end()

#define user_access_save()	smap_save()
#define user_access_restore(x)	smap_restore(x)

#define unsafe_put_user(x, ptr, label)	\
	__put_user_size((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)), label)

#define unsafe_get_user(x, ptr, err_label)					\
do {										\
	__inttype(*(ptr)) __gu_val;						\
	__get_user_size(__gu_val, (ptr), sizeof(*(ptr)), err_label);		\
	(x) = (__force __typeof__(*(ptr)))__gu_val;				\
} while (0)

extern void __try_cmpxchg_user_wrong_size(void);


 
#define unsafe_try_cmpxchg_user(_ptr, _oldp, _nval, _label) ({			\
	bool __ret;								\
	__chk_user_ptr(_ptr);							\
	switch (sizeof(*(_ptr))) {						\
	case 1:	__ret = __try_cmpxchg_user_asm("b", "q",			\
					       (__force u8 *)(_ptr), (_oldp),	\
					       (_nval), _label);		\
		break;								\
	case 2:	__ret = __try_cmpxchg_user_asm("w", "r",			\
					       (__force u16 *)(_ptr), (_oldp),	\
					       (_nval), _label);		\
		break;								\
	case 4:	__ret = __try_cmpxchg_user_asm("l", "r",			\
					       (__force u32 *)(_ptr), (_oldp),	\
					       (_nval), _label);		\
		break;								\
	case 8:	__ret = __try_cmpxchg64_user_asm((__force u64 *)(_ptr), (_oldp),\
						 (_nval), _label);		\
		break;								\
	default: __try_cmpxchg_user_wrong_size();				\
	}									\
	__ret;						})

 
#define __try_cmpxchg_user(_ptr, _oldp, _nval, _label)	({		\
	int __ret = -EFAULT;						\
	__uaccess_begin_nospec();					\
	__ret = !unsafe_try_cmpxchg_user(_ptr, _oldp, _nval, _label);	\
_label:									\
	__uaccess_end();						\
	__ret;								\
							})

 
#define unsafe_copy_loop(dst, src, len, type, label)				\
	while (len >= sizeof(type)) {						\
		unsafe_put_user(*(type *)(src),(type __user *)(dst),label);	\
		dst += sizeof(type);						\
		src += sizeof(type);						\
		len -= sizeof(type);						\
	}

#define unsafe_copy_to_user(_dst,_src,_len,label)			\
do {									\
	char __user *__ucu_dst = (_dst);				\
	const char *__ucu_src = (_src);					\
	size_t __ucu_len = (_len);					\
	unsafe_copy_loop(__ucu_dst, __ucu_src, __ucu_len, u64, label);	\
	unsafe_copy_loop(__ucu_dst, __ucu_src, __ucu_len, u32, label);	\
	unsafe_copy_loop(__ucu_dst, __ucu_src, __ucu_len, u16, label);	\
	unsafe_copy_loop(__ucu_dst, __ucu_src, __ucu_len, u8, label);	\
} while (0)

#define __get_kernel_nofault(dst, src, type, err_label)			\
	__get_user_size(*((type *)(dst)), (__force type __user *)(src),	\
			sizeof(type), err_label)

#define __put_kernel_nofault(dst, src, type, err_label)			\
	__put_user_size(*((type *)(src)), (__force type __user *)(dst),	\
			sizeof(type), err_label)

#endif  

