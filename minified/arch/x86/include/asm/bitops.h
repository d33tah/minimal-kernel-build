 
#ifndef _ASM_X86_BITOPS_H
#define _ASM_X86_BITOPS_H

 

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <linux/compiler.h>
#include <asm/alternative.h>
#include <asm/rmwcc.h>
#include <asm/barrier.h>

/* BITS_PER_LONG == 32 (i386) */
#define _BITOPS_LONG_SHIFT 5

#define BIT_64(n)			(U64_C(1) << (n))

 

#define RLONG_ADDR(x)			 "m" (*(volatile long *) (x))
#define WBYTE_ADDR(x)			"+m" (*(volatile char *) (x))

#define ADDR				RLONG_ADDR(addr)

 
#define CONST_MASK_ADDR(nr, addr)	WBYTE_ADDR((void *)(addr) + ((nr)>>3))
#define CONST_MASK(nr)			(1 << ((nr) & 7))

static __always_inline void
arch_set_bit(long nr, volatile unsigned long *addr)
{
	if (__builtin_constant_p(nr)) {
		asm volatile(LOCK_PREFIX "orb %b1,%0"
			: CONST_MASK_ADDR(nr, addr)
			: "iq" (CONST_MASK(nr))
			: "memory");
	} else {
		asm volatile(LOCK_PREFIX __ASM_SIZE(bts) " %1,%0"
			: : RLONG_ADDR(addr), "Ir" (nr) : "memory");
	}
}

static __always_inline void
arch___set_bit(long nr, volatile unsigned long *addr)
{
	asm volatile(__ASM_SIZE(bts) " %1,%0" : : ADDR, "Ir" (nr) : "memory");
}

static __always_inline void
arch_clear_bit(long nr, volatile unsigned long *addr)
{
	if (__builtin_constant_p(nr)) {
		asm volatile(LOCK_PREFIX "andb %b1,%0"
			: CONST_MASK_ADDR(nr, addr)
			: "iq" (~CONST_MASK(nr)));
	} else {
		asm volatile(LOCK_PREFIX __ASM_SIZE(btr) " %1,%0"
			: : RLONG_ADDR(addr), "Ir" (nr) : "memory");
	}
}

static __always_inline void
arch_clear_bit_unlock(long nr, volatile unsigned long *addr)
{
	barrier();
	arch_clear_bit(nr, addr);
}

static __always_inline void
arch___clear_bit(long nr, volatile unsigned long *addr)
{
	asm volatile(__ASM_SIZE(btr) " %1,%0" : : ADDR, "Ir" (nr) : "memory");
}

static __always_inline bool
arch_clear_bit_unlock_is_negative_byte(long nr, volatile unsigned long *addr)
{
	bool negative;
	asm volatile(LOCK_PREFIX "andb %2,%1"
		CC_SET(s)
		: CC_OUT(s) (negative), WBYTE_ADDR(addr)
		: "ir" ((char) ~(1 << nr)) : "memory");
	return negative;
}
#define arch_clear_bit_unlock_is_negative_byte                                 \
	arch_clear_bit_unlock_is_negative_byte


static __always_inline bool
arch_test_and_set_bit(long nr, volatile unsigned long *addr)
{
	return GEN_BINARY_RMWcc(LOCK_PREFIX __ASM_SIZE(bts), *addr, c, "Ir", nr);
}

static __always_inline bool
arch_test_and_set_bit_lock(long nr, volatile unsigned long *addr)
{
	return arch_test_and_set_bit(nr, addr);
}


static __always_inline bool
arch___test_and_set_bit(long nr, volatile unsigned long *addr)
{
	bool oldbit;

	asm(__ASM_SIZE(bts) " %2,%1"
	    CC_SET(c)
	    : CC_OUT(c) (oldbit)
	    : ADDR, "Ir" (nr) : "memory");
	return oldbit;
}

static __always_inline bool
arch_test_and_clear_bit(long nr, volatile unsigned long *addr)
{
	return GEN_BINARY_RMWcc(LOCK_PREFIX __ASM_SIZE(btr), *addr, c, "Ir", nr);
}

 
static __always_inline bool
arch___test_and_clear_bit(long nr, volatile unsigned long *addr)
{
	bool oldbit;

	asm volatile(__ASM_SIZE(btr) " %2,%1"
		     CC_SET(c)
		     : CC_OUT(c) (oldbit)
		     : ADDR, "Ir" (nr) : "memory");
	return oldbit;
}


static __always_inline bool constant_test_bit(long nr, const volatile unsigned long *addr)
{
	return ((1UL << (nr & (BITS_PER_LONG-1))) &
		(addr[nr >> _BITOPS_LONG_SHIFT])) != 0;
}

static __always_inline bool variable_test_bit(long nr, volatile const unsigned long *addr)
{
	bool oldbit;

	asm volatile(__ASM_SIZE(bt) " %2,%1"
		     CC_SET(c)
		     : CC_OUT(c) (oldbit)
		     : "m" (*(unsigned long *)addr), "Ir" (nr) : "memory");

	return oldbit;
}

#define arch_test_bit(nr, addr)			\
	(__builtin_constant_p((nr))		\
	 ? constant_test_bit((nr), (addr))	\
	 : variable_test_bit((nr), (addr)))

 
static __always_inline unsigned long __ffs(unsigned long word)
{
	asm("rep; bsf %1,%0"
		: "=r" (word)
		: "rm" (word));
	return word;
}

 
static __always_inline unsigned long ffz(unsigned long word)
{
	asm("rep; bsf %1,%0"
		: "=r" (word)
		: "r" (~word));
	return word;
}

 
static __always_inline unsigned long __fls(unsigned long word)
{
	asm("bsr %1,%0"
	    : "=r" (word)
	    : "rm" (word));
	return word;
}

#undef ADDR

#ifdef __KERNEL__
 
static __always_inline int ffs(int x)
{
	int r;

	asm("bsfl %1,%0\n\t"
	    "cmovzl %2,%0"
	    : "=&r" (r) : "rm" (x), "r" (-1));
	return r + 1;
}

 
static __always_inline int fls(unsigned int x)
{
	int r;

	asm("bsrl %1,%0\n\t"
	    "cmovzl %2,%0"
	    : "=&r" (r) : "rm" (x), "rm" (-1));
	return r + 1;
}

/* Inlined from asm-generic/bitops/fls64.h - BITS_PER_LONG == 32 */
static __always_inline int fls64(__u64 x)
{
	__u32 h = x >> 32;
	if (h)
		return fls(h) + 32;
	return fls(x);
}


/* --- 2025-12-07 20:24 --- Inlined arch_hweight.h */
#include <asm/cpufeatures.h>
#define REG_IN "a"
#define REG_OUT "a"
static __always_inline unsigned int __arch_hweight32(unsigned int w)
{
	unsigned int res;
	asm (ALTERNATIVE("call __sw_hweight32", "popcntl %1, %0", X86_FEATURE_POPCNT)
			 : "="REG_OUT (res)
			 : REG_IN (w));
	return res;
}
static inline unsigned int __arch_hweight16(unsigned int w)
{
	return __arch_hweight32(w & 0xffff);
}
static inline unsigned int __arch_hweight8(unsigned int w)
{
	return __arch_hweight32(w & 0xff);
}
static inline unsigned long __arch_hweight64(__u64 w)
{
	return  __arch_hweight32((u32)w) +
		__arch_hweight32((u32)(w >> 32));
}
/* --- end inlined arch_hweight.h --- */

/* Inlined from asm-generic/bitops/const_hweight.h */
#define __const_hweight8(w)		\
	((unsigned int)			\
	 ((!!((w) & (1ULL << 0))) +	\
	  (!!((w) & (1ULL << 1))) +	\
	  (!!((w) & (1ULL << 2))) +	\
	  (!!((w) & (1ULL << 3))) +	\
	  (!!((w) & (1ULL << 4))) +	\
	  (!!((w) & (1ULL << 5))) +	\
	  (!!((w) & (1ULL << 6))) +	\
	  (!!((w) & (1ULL << 7)))))

#define __const_hweight16(w) (__const_hweight8(w)  + __const_hweight8((w)  >> 8 ))
#define __const_hweight32(w) (__const_hweight16(w) + __const_hweight16((w) >> 16))
#define __const_hweight64(w) (__const_hweight32(w) + __const_hweight32((w) >> 32))

#define hweight8(w)  (__builtin_constant_p(w) ? __const_hweight8(w)  : __arch_hweight8(w))
#define hweight16(w) (__builtin_constant_p(w) ? __const_hweight16(w) : __arch_hweight16(w))
#define hweight32(w) (__builtin_constant_p(w) ? __const_hweight32(w) : __arch_hweight32(w))
#define hweight64(w) (__builtin_constant_p(w) ? __const_hweight64(w) : __arch_hweight64(w))

#define HWEIGHT8(w)  (BUILD_BUG_ON_ZERO(!__builtin_constant_p(w)) + __const_hweight8(w))
#define HWEIGHT16(w) (BUILD_BUG_ON_ZERO(!__builtin_constant_p(w)) + __const_hweight16(w))
#define HWEIGHT32(w) (BUILD_BUG_ON_ZERO(!__builtin_constant_p(w)) + __const_hweight32(w))
#define HWEIGHT64(w) (BUILD_BUG_ON_ZERO(!__builtin_constant_p(w)) + __const_hweight64(w))

#define HWEIGHT(w)   HWEIGHT64((u64)w)

/* Inlined from asm-generic/bitops/instrumented-atomic.h */
#include <linux/instrumented.h>

static __always_inline void set_bit(long nr, volatile unsigned long *addr)
{
	instrument_atomic_write(addr + BIT_WORD(nr), sizeof(long));
	arch_set_bit(nr, addr);
}

static __always_inline void clear_bit(long nr, volatile unsigned long *addr)
{
	instrument_atomic_write(addr + BIT_WORD(nr), sizeof(long));
	arch_clear_bit(nr, addr);
}


static __always_inline bool test_and_set_bit(long nr, volatile unsigned long *addr)
{
	instrument_atomic_read_write(addr + BIT_WORD(nr), sizeof(long));
	return arch_test_and_set_bit(nr, addr);
}

static __always_inline bool test_and_clear_bit(long nr, volatile unsigned long *addr)
{
	instrument_atomic_read_write(addr + BIT_WORD(nr), sizeof(long));
	return arch_test_and_clear_bit(nr, addr);
}


/* Inlined from asm-generic/bitops/instrumented-non-atomic.h */
static __always_inline void __set_bit(long nr, volatile unsigned long *addr)
{
	instrument_write(addr + BIT_WORD(nr), sizeof(long));
	arch___set_bit(nr, addr);
}

static __always_inline void __clear_bit(long nr, volatile unsigned long *addr)
{
	instrument_write(addr + BIT_WORD(nr), sizeof(long));
	arch___clear_bit(nr, addr);
}


static __always_inline void __instrument_read_write_bitop(long nr, volatile unsigned long *addr)
{
	instrument_read_write(addr + BIT_WORD(nr), sizeof(long));
}

static __always_inline bool __test_and_set_bit(long nr, volatile unsigned long *addr)
{
	__instrument_read_write_bitop(nr, addr);
	return arch___test_and_set_bit(nr, addr);
}

static __always_inline bool __test_and_clear_bit(long nr, volatile unsigned long *addr)
{
	__instrument_read_write_bitop(nr, addr);
	return arch___test_and_clear_bit(nr, addr);
}


static __always_inline bool test_bit(long nr, const volatile unsigned long *addr)
{
	instrument_atomic_read(addr + BIT_WORD(nr), sizeof(long));
	return arch_test_bit(nr, addr);
}

/* Inlined from asm-generic/bitops/instrumented-lock.h */
static inline void clear_bit_unlock(long nr, volatile unsigned long *addr)
{
	instrument_atomic_write(addr + BIT_WORD(nr), sizeof(long));
	arch_clear_bit_unlock(nr, addr);
}

static inline bool test_and_set_bit_lock(long nr, volatile unsigned long *addr)
{
	instrument_atomic_read_write(addr + BIT_WORD(nr), sizeof(long));
	return arch_test_and_set_bit_lock(nr, addr);
}

#if defined(arch_clear_bit_unlock_is_negative_byte)
static inline bool
clear_bit_unlock_is_negative_byte(long nr, volatile unsigned long *addr)
{
	instrument_atomic_write(addr + BIT_WORD(nr), sizeof(long));
	return arch_clear_bit_unlock_is_negative_byte(nr, addr);
}
#define clear_bit_unlock_is_negative_byte clear_bit_unlock_is_negative_byte
#endif


#endif  
#endif  
