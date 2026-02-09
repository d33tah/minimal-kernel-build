#ifndef __LINUX_COMPILER_TYPES_H
#define __LINUX_COMPILER_TYPES_H

#define __must_hold(x)
#define __acquires(x)
#define __releases(x)
#define __acquire(x)	(void)0
#define __release(x)	(void)0
#define __cond_lock(x,c) (c)

/* --- Inlined compiler-gcc.h --- */
#ifndef GCC_VERSION
#define GCC_VERSION (__GNUC__ * 10000		\
		     + __GNUC_MINOR__ * 100	\
		     + __GNUC_PATCHLEVEL__)
#endif

#if GCC_VERSION >= 70000 && !defined(__CHECKER__)
# define __fallthrough __attribute__ ((fallthrough))
#endif

#if __has_attribute(__error__)
# define __compiletime_error(message) __attribute__((error(message)))
#endif

 
#define __must_be_array(a)	BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))

#ifndef __pure
#define  __pure		__attribute__((pure))
#endif
#define  noinline	__attribute__((noinline))
#ifndef __packed
#define __packed	__attribute__((packed))
#endif
#ifndef __noreturn
#define __noreturn	__attribute__((noreturn))
#endif
#ifndef __aligned
#define __aligned(x)	__attribute__((aligned(x)))
#endif
#define __printf(a, b)	__attribute__((format(printf, a, b)))
#define __scanf(a, b)	__attribute__((format(scanf, a, b)))

#endif
