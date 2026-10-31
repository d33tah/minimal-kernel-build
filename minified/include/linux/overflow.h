#ifndef __LINUX_OVERFLOW_H
#define __LINUX_OVERFLOW_H

static inline bool __must_check __must_check_overflow(bool overflow)
{
	return unlikely(overflow);
}

#define check_mul_overflow(a, b, d)                    \
	__must_check_overflow(({                       \
		typeof(a) __a = (a);                   \
		typeof(b) __b = (b);                   \
		typeof(d) __d = (d);                   \
		(void)(&__a == &__b);                  \
		(void)(&__a == __d);                   \
		__builtin_mul_overflow(__a, __b, __d); \
	}))

#endif
