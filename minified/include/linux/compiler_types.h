#ifndef __LINUX_COMPILER_TYPES_H
#define __LINUX_COMPILER_TYPES_H

# define BTF_TYPE_TAG(value)

# define __kernel
# define __user	BTF_TYPE_TAG(user)
# define __percpu	BTF_TYPE_TAG(percpu)
# define __rcu
# define __chk_user_ptr(x)	(void)0
# define __releases(x)
# define __cond_lock(x,c) (c)
# define __force

#define ___PASTE(a,b) a##b
#define __PASTE(a,b) ___PASTE(a,b)

#include <linux/compiler_attributes.h>

#ifdef __clang__
/* --- 2025-12-08 00:18 --- Inlined from compiler-clang.h */
#define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)
#define __no_sanitize_address
/* __no_sanitize_thread dropped: KCSAN unset, zero expanders tree-wide (tick #404) */
#define __no_sanitize_coverage

#define __nocfi		__attribute__((__no_sanitize__("cfi")))

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

/* KBUILD_EXTRA_WARN1 is defined only under `make W=1` (scripts/Makefile.extrawarn,
 * -D-gated by KBUILD_EXTRA_WARN); the plain gate build passes no W=, so the then-arm
 * is statically dead. Keep only the live __maybe_unused arm. */
#define __inline_maybe_unused __maybe_unused

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

#define noinstr									noinline notrace __attribute((__section__(".noinstr.text")))		__no_kcsan __no_sanitize_address __no_profile __no_sanitize_coverage

# define __latent_entropy

/* RANDSTRUCT gcc-plugin is unconfigured in this build (CONFIG_RANDSTRUCT unset;
 * scripts/Makefile.randstruct absent, so RANDSTRUCT is never -D'd) => the
 * defined(RANDSTRUCT) arm is statically dead; only the plain arm is live. */
# define __randomize_layout __designated_init
# define randomized_struct_fields_start
# define randomized_struct_fields_end

# define __alloc_size(x, ...)	__alloc_size__(x, ## __VA_ARGS__) __malloc

#define asm_volatile_goto(x...) asm goto(x)

#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))

#define __scalar_type_to_expr_cases(type)						unsigned type:	(unsigned type)0,					signed type:	(signed type)0

#define __unqual_scalar_typeof(x) typeof(						_Generic((x),									 char:	(char)0,							 __scalar_type_to_expr_cases(char),					 __scalar_type_to_expr_cases(short),					 __scalar_type_to_expr_cases(int),					 __scalar_type_to_expr_cases(long),					 __scalar_type_to_expr_cases(long long),				 default: (x)))

#define __native_word(t) 	(sizeof(t) == sizeof(char) || sizeof(t) == sizeof(short) || 	 sizeof(t) == sizeof(int) || sizeof(t) == sizeof(long))

# define __compiletime_assert(condition, msg, prefix, suffix)		\
	do {								\
		 							\
		__noreturn extern void prefix ## suffix(void)		\
			__compiletime_error(msg);			\
		if (!(condition))					\
			prefix ## suffix();				\
	} while (0)

#define _compiletime_assert(condition, msg, prefix, suffix) 	__compiletime_assert(condition, msg, prefix, suffix)

#define compiletime_assert(condition, msg) 	_compiletime_assert(condition, msg, __compiletime_assert_, __COUNTER__)

#define compiletime_assert_atomic_type(t)					compiletime_assert(__native_word(t),						"Need native word sized stores/loads for atomicity.")

#endif  
