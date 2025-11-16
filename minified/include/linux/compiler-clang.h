 
#ifndef __LINUX_COMPILER_TYPES_H
#error "Please don't include <linux/compiler-clang.h> directly, include <linux/compiler.h> instead."
#endif

 

 
#define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)

 
#define KASAN_ABI_VERSION 5

 

#if __has_feature(address_sanitizer) || __has_feature(hwaddress_sanitizer)
 
#define __SANITIZE_ADDRESS__
#define __no_sanitize_address \
		__attribute__((no_sanitize("address", "hwaddress")))
#else
#define __no_sanitize_address
#endif

#if __has_feature(thread_sanitizer)
 
#define __SANITIZE_THREAD__
#define __no_sanitize_thread \
		__attribute__((no_sanitize("thread")))
#else
#define __no_sanitize_thread
#endif

#if defined(CONFIG_ARCH_USE_BUILTIN_BSWAP)
#define __HAVE_BUILTIN_BSWAP32__
#define __HAVE_BUILTIN_BSWAP64__
#define __HAVE_BUILTIN_BSWAP16__
#endif  

#if __has_feature(undefined_behavior_sanitizer)
 
#define __no_sanitize_undefined \
		__attribute__((no_sanitize("undefined")))
#else
#define __no_sanitize_undefined
#endif

 
#if __has_feature(coverage_sanitizer)
#define __no_sanitize_coverage __attribute__((no_sanitize("coverage")))
#else
#define __no_sanitize_coverage
#endif

#if __has_feature(shadow_call_stack)
# define __noscs	__attribute__((__no_sanitize__("shadow-call-stack")))
#endif

#define __nocfi		__attribute__((__no_sanitize__("cfi")))
#define __cficanonical	__attribute__((__cfi_canonical_jump_table__))

#if defined(CONFIG_CFI_CLANG)
 
#define function_nocfi(x)	__builtin_function_start(x)
#endif

 
#define __diag_clang(version, severity, s) \
	__diag_clang_ ## version(__diag_clang_ ## severity s)

 
#define __diag_clang_ignore	ignored
#define __diag_clang_warn	warning
#define __diag_clang_error	error

#define __diag_str1(s)		#s
#define __diag_str(s)		__diag_str1(s)
#define __diag(s)		_Pragma(__diag_str(clang diagnostic s))

#if CONFIG_CLANG_VERSION >= 110000
#define __diag_clang_11(s)	__diag(s)
#else
#define __diag_clang_11(s)
#endif

#define __diag_ignore_all(option, comment) \
	__diag_clang(11, ignore, option)
