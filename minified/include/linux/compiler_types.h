#ifndef __LINUX_COMPILER_TYPES_H
#define __LINUX_COMPILER_TYPES_H

#ifndef __ASSEMBLY__

# define BTF_TYPE_TAG(value)
# define __kernel
# define __user	BTF_TYPE_TAG(user)
# define __iomem
# define __percpu	BTF_TYPE_TAG(percpu)
# define __rcu
# define __chk_user_ptr(x)	(void)0
# define __chk_io_ptr(x)	(void)0
# define __must_hold(x)
# define __acquires(x)
# define __cond_acquires(x)
# define __releases(x)
# define __acquire(x)	(void)0
# define __release(x)	(void)0
# define __cond_lock(x,c) (c)
# define __force
# define __nocast
# define __safe
# define __private
# define ACCESS_PRIVATE(p, member) ((p)->member)
# define __builtin_warning(x, y...) (1)

#define ___PASTE(a,b) a##b
#define __PASTE(a,b) ___PASTE(a,b)

#ifdef __KERNEL__

#include <linux/compiler_attributes.h>


/* Clang-specific definitions - inlined from compiler-clang.h */
#define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)
#define KASAN_ABI_VERSION 5
#define __no_sanitize_address
#define __no_sanitize_thread
#define __HAVE_BUILTIN_BSWAP32__
#define __HAVE_BUILTIN_BSWAP64__
#define __HAVE_BUILTIN_BSWAP16__
#define __no_sanitize_coverage
#define __nocfi		__attribute__((__no_sanitize__("cfi")))

#define __diag_clang(version, severity, s) \
	__diag_clang_ ## version(__diag_clang_ ## severity s)
#define __diag_clang_ignore	ignored
#define __diag_clang_warn	warning
#define __diag_clang_error	error
#define __diag_str1(s)		#s
#define __diag_str(s)		__diag_str1(s)
#define __diag(s)		_Pragma(__diag_str(clang diagnostic s))
#define __diag_clang_11(s)	__diag(s)
#define __diag_ignore_all(option, comment) \
	__diag_clang(11, ignore, option)

#define notrace			__attribute__((__no_instrument_function__))

#define __naked			__attribute__((__naked__)) notrace

#define inline inline __gnu_inline __inline_maybe_unused notrace

#define __inline__ inline

#define __inline_maybe_unused __maybe_unused
#define noinline_for_stack noinline
#define __no_kasan_or_inline __always_inline
#define __no_kcsan
#define __no_sanitize_or_inline __always_inline

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

#define __diag_GCC(version, severity, string)

#define __diag_push()	__diag(push)
#define __diag_pop()	__diag(pop)

#define __diag_ignore(compiler, version, option, comment) \
	__diag_ ## compiler(version, ignore, option)
#define __diag_warn(compiler, version, option, comment) \
	__diag_ ## compiler(version, warn, option)
#define __diag_error(compiler, version, option, comment) \
	__diag_ ## compiler(version, error, option)

#endif  
