#ifndef __LINUX_COMPILER_TYPES_H
#define __LINUX_COMPILER_TYPES_H

#ifndef __ASSEMBLY__

# define BTF_TYPE_TAG(value)
# define __kernel
# define __user	BTF_TYPE_TAG(user)
# define __percpu	BTF_TYPE_TAG(percpu)
# define __rcu
# define __chk_user_ptr(x)	(void)0
# define __acquires(x)
# define __releases(x)
# define __acquire(x)	(void)0
# define __release(x)	(void)0
# define __cond_lock(x,c) (c)
# define __force
# define __private
# define ACCESS_PRIVATE(p, member) ((p)->member)

#define ___PASTE(a,b) a##b
#define __PASTE(a,b) ___PASTE(a,b)

#ifdef __KERNEL__

#include <linux/compiler_attributes.h>

#define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)
#define __no_sanitize_address
#define __no_sanitize_coverage
#define __nocfi		__attribute__((__no_sanitize__("cfi")))

#define notrace			__attribute__((__no_instrument_function__))

#define inline inline __gnu_inline __inline_maybe_unused notrace

#define __inline_maybe_unused __maybe_unused
#define noinline_for_stack noinline
#define __no_kasan_or_inline __always_inline
#define __no_kcsan

#define noinstr								\
	noinline notrace __attribute((__section__(".noinstr.text")))	\
	__no_kcsan __no_sanitize_address __no_profile __no_sanitize_coverage

#endif  

#endif  

# define __latent_entropy
# define __randomize_layout __designated_init
# define randomized_struct_fields_start
# define randomized_struct_fields_end

# define __alloc_size(x, ...)	__alloc_size__(x, ## __VA_ARGS__) __malloc

#define asm_volatile_goto(x...) asm goto(x)

#define asm_inline asm __inline

#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))

#define __scalar_type_to_expr_cases(type)				\
		unsigned type:	(unsigned type)0,			\
		signed type:	(signed type)0

#define __unqual_scalar_typeof(x) typeof(				\
		_Generic((x),						\
			 char:	(char)0,				\
			 __scalar_type_to_expr_cases(char),		\
			 __scalar_type_to_expr_cases(short),		\
			 __scalar_type_to_expr_cases(int),		\
			 __scalar_type_to_expr_cases(long),		\
			 __scalar_type_to_expr_cases(long long),	\
			 default: (x)))

#define __native_word(t) \
	(sizeof(t) == sizeof(char) || sizeof(t) == sizeof(short) || \
	 sizeof(t) == sizeof(int) || sizeof(t) == sizeof(long))

#define __compiletime_assert(condition, msg, prefix, suffix)		\
	do {								\
		__noreturn extern void prefix ## suffix(void)		\
			__compiletime_error(msg);			\
		if (!(condition))					\
			prefix ## suffix();				\
	} while (0)

#define _compiletime_assert(condition, msg, prefix, suffix) \
	__compiletime_assert(condition, msg, prefix, suffix)

#define compiletime_assert(condition, msg) \
	_compiletime_assert(condition, msg, __compiletime_assert_, __COUNTER__)

#define compiletime_assert_atomic_type(t)				\
	compiletime_assert(__native_word(t),				\
		"Need native word sized stores/loads for atomicity.")

#endif  
