#ifndef __LINUX_COMPILER_ATTRIBUTES_H
#define __LINUX_COMPILER_ATTRIBUTES_H


#define __alias(symbol)                 __attribute__((__alias__(#symbol)))

#define __aligned(x)                    __attribute__((__aligned__(x)))
/* __aligned_largest removed - unused */

#define __alloc_size__(x, ...)		__attribute__((__alloc_size__(x, ## __VA_ARGS__)))

#define __always_inline                 inline __attribute__((__always_inline__))

#if __has_attribute(__assume_aligned__)
# define __assume_aligned(a, ...)       __attribute__((__assume_aligned__(a, ## __VA_ARGS__)))
#else
# define __assume_aligned(a, ...)
#endif

#define __cold                          __attribute__((__cold__))

#define __attribute_const__             __attribute__((__const__))

/* __deprecated removed - never used */

#if __has_attribute(__designated_init__)
# define __designated_init              __attribute__((__designated_init__))
#else
# define __designated_init
#endif

#if __has_attribute(__error__)
# define __compiletime_error(msg)       __attribute__((__error__(msg)))
#else
# define __compiletime_error(msg)
#endif

#if __has_attribute(__externally_visible__)
# define __visible                      __attribute__((__externally_visible__))
#else
# define __visible
#endif

#define __printf(a, b)                  __attribute__((__format__(printf, a, b)))
#define __scanf(a, b)                   __attribute__((__format__(scanf, a, b)))

#define __gnu_inline                    __attribute__((__gnu_inline__))

#define __malloc                        __attribute__((__malloc__))

#if __has_attribute(__fallthrough__)
# define fallthrough                    __attribute__((__fallthrough__))
#else
# define fallthrough                    do {} while (0)
#endif

#define   noinline                      __attribute__((__noinline__))

#if __has_attribute(__no_profile_instrument_function__)
# define __no_profile                  __attribute__((__no_profile_instrument_function__))
#else
# define __no_profile
#endif

#define __noreturn                      __attribute__((__noreturn__))

#define __packed                        __attribute__((__packed__))

#define __pure                          __attribute__((__pure__))

#define __section(section)              __attribute__((__section__(section)))

/* __always_unused removed - never used */
#define __maybe_unused                  __attribute__((__unused__))

#define __used                          __attribute__((__used__))

#define __must_check                    __attribute__((__warn_unused_result__))

#define __weak                          __attribute__((__weak__))

#endif  
