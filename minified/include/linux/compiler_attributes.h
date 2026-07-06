#ifndef __LINUX_COMPILER_ATTRIBUTES_H
#define __LINUX_COMPILER_ATTRIBUTES_H


#define __alias(symbol)                 __attribute__((__alias__(#symbol)))

#define __aligned(x)                    __attribute__((__aligned__(x)))

#define __alloc_size__(x, ...)		__attribute__((__alloc_size__(x, ## __VA_ARGS__)))

#define __always_inline                 inline __attribute__((__always_inline__))

# define __assume_aligned(a, ...)       __attribute__((__assume_aligned__(a, ## __VA_ARGS__)))

#define __cold                          __attribute__((__cold__))

#define __attribute_const__             __attribute__((__const__))

# define __copy(symbol)

# define __diagnose_as(builtin...)	__attribute__((__diagnose_as_builtin__(builtin)))

# define __designated_init

# define __compiletime_error(msg)       __attribute__((__error__(msg)))

# define __visible

#define __printf(a, b)                  __attribute__((__format__(printf, a, b)))

#define __gnu_inline                    __attribute__((__gnu_inline__))

#define __malloc                        __attribute__((__malloc__))

# define fallthrough                    __attribute__((__fallthrough__))

#define   noinline                      __attribute__((__noinline__))

# define __no_profile                  __attribute__((__no_profile_instrument_function__))

#define __noreturn                      __attribute__((__noreturn__))

#define __packed                        __attribute__((__packed__))

# define __pass_object_size(type)	__attribute__((__pass_object_size__(type)))

#define __pure                          __attribute__((__pure__))

#define __section(section)              __attribute__((__section__(section)))

#define __maybe_unused                  __attribute__((__unused__))

#define __used                          __attribute__((__used__))

#define __must_check                    __attribute__((__warn_unused_result__))

# define __compiletime_warning(msg)     __attribute__((__warning__(msg)))

#define __weak                          __attribute__((__weak__))

#endif  
