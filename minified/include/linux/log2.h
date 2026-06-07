
#ifndef _LINUX_LOG2_H
#define _LINUX_LOG2_H

#include <linux/types.h>
#include <linux/bitops.h>

#ifndef CONFIG_ARCH_HAS_ILOG2_U32
static __always_inline __attribute__((const))
int __ilog2_u32(u32 n)
{
	return fls(n) - 1;
}
#endif

#ifndef CONFIG_ARCH_HAS_ILOG2_U64
static __always_inline __attribute__((const))
int __ilog2_u64(u64 n)
{
	return fls64(n) - 1;
}
#endif

static inline __attribute__((const))
bool is_power_of_2(unsigned long n)
{
	return (n != 0 && ((n & (n - 1)) == 0));
}

static inline __attribute__((const))
unsigned long __roundup_pow_of_two(unsigned long n)
{
	return 1UL << fls_long(n - 1);
}

static inline __attribute__((const))
unsigned long __rounddown_pow_of_two(unsigned long n)
{
	return 1UL << (fls_long(n) - 1);
}

#define ilog2(n) \
( \
	__builtin_constant_p(n) ?	\
	((n) < 2 ? 0 :			\
	 63 - __builtin_clzll(n)) :	\
	(sizeof(n) <= 4) ?		\
	__ilog2_u32(n) :		\
	__ilog2_u64(n)			\
 )

#define roundup_pow_of_two(n)			\
(						\
	__builtin_constant_p(n) ? (		\
		((n) == 1) ? 1 :		\
		(1UL << (ilog2((n) - 1) + 1))	\
				   ) :		\
	__roundup_pow_of_two(n)			\
 )

#define rounddown_pow_of_two(n)			\
(						\
	__builtin_constant_p(n) ? (		\
		(1UL << ilog2(n))) :		\
	__rounddown_pow_of_two(n)		\
 )

static inline __attribute_const__
int __order_base_2(unsigned long n)
{
	return n > 1 ? ilog2(n - 1) + 1 : 0;
}

#define order_base_2(n)				\
(						\
	__builtin_constant_p(n) ? (		\
		((n) == 0 || (n) == 1) ? 0 :	\
		ilog2((n) - 1) + 1) :		\
	__order_base_2(n)			\
)

#endif
