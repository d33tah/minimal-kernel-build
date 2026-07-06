#ifndef __LINUX_COMPILER_TYPES_H
#define __LINUX_COMPILER_TYPES_H

#ifndef __ASSEMBLY__

# define BTF_TYPE_TAG(value)

#ifdef __CHECKER__
# define __kernel	__attribute__((address_space(0)))
# define __user		__attribute__((noderef, address_space(__user)))
# define __iomem	__attribute__((noderef, address_space(__iomem)))
# define __percpu	__attribute__((noderef, address_space(__percpu)))
# define __rcu		__attribute__((noderef, address_space(__rcu)))
static inline void __chk_user_ptr(const volatile void __user *ptr) { }
static inline void __chk_io_ptr(const volatile void __iomem *ptr) { }
# define __must_hold(x)	__attribute__((context(x,1,1)))
# define __acquires(x)	__attribute__((context(x,0,1)))
# define __cond_acquires(x) __attribute__((context(x,0,-1)))
# define __releases(x)	__attribute__((context(x,1,0)))
# define __acquire(x)	__context__(x,1)
# define __release(x)	__context__(x,-1)
# define __cond_lock(x,c)	((c) ? ({ __acquire(x); 1; }) : 0)
# define __force	__attribute__((force))
# define __nocast	__attribute__((nocast))
# define __safe		__attribute__((safe))
# define __private	__attribute__((noderef))
# define ACCESS_PRIVATE(p, member) (*((typeof((p)->member) __force *) &(p)->member))
#else  
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
#endif  

#define ___PASTE(a,b) a##b
#define __PASTE(a,b) ___PASTE(a,b)

#ifdef __KERNEL__

#include <linux/compiler_attributes.h>


#ifndef __has_builtin
#define __has_builtin(x) (0)
#endif

#ifdef __clang__
/* --- 2025-12-08 00:18 --- Inlined from compiler-clang.h */
#define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)
#define __no_sanitize_address
/* __no_sanitize_thread dropped: KCSAN unset, zero expanders tree-wide (tick #404) */
#define __no_sanitize_coverage

#define __nocfi		__attribute__((__no_sanitize__("cfi")))

#define __diag_clang(version, severity, s) \
	__diag_clang_ ## version(__diag_clang_ ## severity s)


#define __diag_str1(s)		#s
#define __diag_str(s)		__diag_str1(s)
#define __diag(s)		_Pragma(__diag_str(clang diagnostic s))

#define __diag_clang_11(s)	__diag(s)

#define __diag_ignore_all(option, comment) \
	__diag_clang(11, ignore, option)
/* end compiler-clang.h */
#else
#error "Unknown compiler"
#endif


/*
 * notrace selector: the CC_USING_HOTPATCH and CC_USING_PATCHABLE_FUNCTION_ENTRY
 * arms are statically dead in this build. Neither token is ever -D'd here:
 * CC_USING_HOTPATCH is only defined by arch/s390's Makefile (absent), and
 * CC_USING_PATCHABLE_FUNCTION_ENTRY by a CONFIG-gated top-Makefile rule that is
 * likewise absent. The ftrace block in the top Makefile only ever -D's
 * CC_USING_NOP_MCOUNT / CC_USING_FENTRY. So only the __no_instrument_function__
 * arm was ever live; keep it unconditionally.
 */
#define notrace			__attribute__((__no_instrument_function__))

#define inline inline __gnu_inline __inline_maybe_unused notrace

#ifdef KBUILD_EXTRA_WARN1
#define __inline_maybe_unused
#else
#define __inline_maybe_unused __maybe_unused
#endif

#define noinline_for_stack noinline

/*
 * __SANITIZE_ADDRESS__ is #defined at exactly one site (the
 * __has_feature(address_sanitizer) bridge above). This is a clang-only build
 * (CONFIG_CC_IS_CLANG=y; clang does NOT predefine __SANITIZE_ADDRESS__) with no
 * KASAN: CONFIG_KASAN is unset, scripts/Makefile.kasan is absent, and nothing
 * ever passes -fsanitize=address/hwaddress. So that bridge never fires, the
 * token is never defined, and the KASAN then-arm is statically dead. Keep only
 * the live plain-inline arm.
 */
# define __no_kasan_or_inline __always_inline

/*
 * Likewise __SANITIZE_THREAD__: defined only at the __has_feature(thread_sanitizer)
 * bridge above; this build never passes -fsanitize=thread (CONFIG_KCSAN unset,
 * scripts/Makefile.kcsan absent), so the token is never defined and the KCSAN
 * then-arm is statically dead. Keep only the live empty arm.
 */
# define __no_kcsan

#ifndef __no_sanitize_or_inline
#define __no_sanitize_or_inline __always_inline
#endif

#define noinstr								\
	noinline notrace __attribute((__section__(".noinstr.text")))	\
	__no_kcsan __no_sanitize_address __no_profile __no_sanitize_coverage

#endif  

#endif  

#ifndef __latent_entropy
# define __latent_entropy
#endif

/* RANDSTRUCT gcc-plugin is unconfigured in this build (CONFIG_RANDSTRUCT unset;
 * scripts/Makefile.randstruct absent, so RANDSTRUCT is never -D'd) => the
 * defined(RANDSTRUCT) arm is statically dead; only the plain arm is live. */
# define __randomize_layout __designated_init
# define randomized_struct_fields_start
# define randomized_struct_fields_end

#ifndef __nocfi
# define __nocfi
#endif

#ifdef __alloc_size__
# define __alloc_size(x, ...)	__alloc_size__(x, ## __VA_ARGS__) __malloc
#else
# define __alloc_size(x, ...)	__malloc
#endif

#ifndef asm_volatile_goto
#define asm_volatile_goto(x...) asm goto(x)
#endif

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

#ifdef __OPTIMIZE__
# define __compiletime_assert(condition, msg, prefix, suffix)		\
	do {								\
		 							\
		__noreturn extern void prefix ## suffix(void)		\
			__compiletime_error(msg);			\
		if (!(condition))					\
			prefix ## suffix();				\
	} while (0)
#else
# define __compiletime_assert(condition, msg, prefix, suffix) do { } while (0)
#endif

#define _compiletime_assert(condition, msg, prefix, suffix) \
	__compiletime_assert(condition, msg, prefix, suffix)

#define compiletime_assert(condition, msg) \
	_compiletime_assert(condition, msg, __compiletime_assert_, __COUNTER__)

#define compiletime_assert_atomic_type(t)				\
	compiletime_assert(__native_word(t),				\
		"Need native word sized stores/loads for atomicity.")

#ifndef __diag
#define __diag(string)
#endif

#ifndef __diag_GCC
#define __diag_GCC(version, severity, string)
#endif


#ifndef __diag_ignore_all
#define __diag_ignore_all(option, comment)
#endif

#endif  
