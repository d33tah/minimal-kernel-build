/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_COMPILER_TYPES_H
#define __LINUX_COMPILER_TYPES_H

#define BTF_TYPE_TAG(value) /* nothing */

/* address spaces */
#define __kernel
#define __user BTF_TYPE_TAG(user)
#define __percpu BTF_TYPE_TAG(percpu)
#define __rcu
#define __chk_user_ptr(x) (void)0
#define __releases(x)
#define __cond_lock(x, c) (c)
/* other */
#define __force

/* Indirect macros required for expanded argument pasting, eg. __LINE__. */
#define ___PASTE(a, b) a##b
#define __PASTE(a, b) ___PASTE(a, b)

/* Attributes */
#include <linux/compiler_attributes.h>

/* Compiler specific macros. */
#ifdef __clang__
#define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)
#define __no_sanitize_address
#define __no_sanitize_coverage

#define __nocfi __attribute__((__no_sanitize__("cfi")))

#else
#error "Unknown compiler"
#endif

#define notrace __attribute__((__no_instrument_function__))

/*
 * Prefer gnu_inline, so that extern inline functions do not emit an
 * externally visible function. This makes extern inline behave as per gnu89
 * semantics rather than c99. This prevents multiple symbol definition errors
 * of extern inline functions at link time.
 * A lot of inline functions can cause havoc with function tracing.
 */
#define inline inline __gnu_inline __inline_maybe_unused notrace

#define __inline_maybe_unused __maybe_unused

#define __no_kasan_or_inline __always_inline

#define __no_kcsan

/* Section for code which can't be instrumented at all */
#define noinstr                                                      \
	noinline notrace __attribute((__section__(".noinstr.text"))) \
	__no_kcsan __no_sanitize_address __no_profile __no_sanitize_coverage

#define __latent_entropy

#define __randomize_layout __designated_init
#define randomized_struct_fields_start
#define randomized_struct_fields_end

#define __alloc_size(x, ...) __alloc_size__(x, ##__VA_ARGS__) __malloc

#define asm_volatile_goto(x...) asm goto(x)

/* Are two types/vars the same type (ignoring qualifiers)? */
#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))

/*
 * __unqual_scalar_typeof(x) - Declare an unqualified scalar type, leaving
 *			       non-scalar types unchanged.
 */
/*
 * Prefer C11 _Generic for better compile-times and simpler code. Note: 'char'
 * is not type-compatible with 'signed char', and we define a separate case.
 */
#define __scalar_type_to_expr_cases(type) \
	unsigned type : (unsigned type)0, signed type : (signed type)0

#define __unqual_scalar_typeof(x)                              \
	typeof(_Generic((x),                                   \
		       char: (char)0,                          \
		       __scalar_type_to_expr_cases(char),      \
		       __scalar_type_to_expr_cases(short),     \
		       __scalar_type_to_expr_cases(int),       \
		       __scalar_type_to_expr_cases(long),      \
		       __scalar_type_to_expr_cases(long long), \
		       default: (x)))

/* Is this type a native word size -- useful for atomic operations */
#define __native_word(t)                                            \
	(sizeof(t) == sizeof(char) || sizeof(t) == sizeof(short) || \
	 sizeof(t) == sizeof(int) || sizeof(t) == sizeof(long))

#define __compiletime_assert(condition, msg, prefix, suffix) \
	do {                                                 \
		__noreturn extern void prefix##suffix(void)  \
			__compiletime_error(msg);            \
		if (!(condition))                            \
			prefix##suffix();                    \
	} while (0)

#define _compiletime_assert(condition, msg, prefix, suffix) \
	__compiletime_assert(condition, msg, prefix, suffix)

/**
 * compiletime_assert - break build and emit msg if condition is false
 * @condition: a compile-time constant condition to check
 * @msg:       a message to emit if condition is false
 *
 * In tradition of POSIX assert, this macro will break the build if the
 * supplied condition is *false*, emitting the supplied error message if the
 * compiler has support to do so.
 */
#define compiletime_assert(condition, msg) \
	_compiletime_assert(condition, msg, __compiletime_assert_, __COUNTER__)

#define compiletime_assert_atomic_type(t) \
	compiletime_assert(               \
		__native_word(t),         \
		"Need native word sized stores/loads for atomicity.")

#endif
