#ifndef _ASM_X86_ATOMIC64_32_H
#define _ASM_X86_ATOMIC64_32_H

#include <linux/compiler.h>
#include <linux/types.h>
 

 

typedef struct {
	s64 __aligned(8) counter;
} atomic64_t;

#define ATOMIC64_INIT(val)	{ (val) }

#define __ATOMIC64_DECL(sym) void atomic64_##sym(atomic64_t *, ...)
#ifndef ATOMIC64_EXPORT
#define ATOMIC64_DECL_ONE __ATOMIC64_DECL
#else
#define ATOMIC64_DECL_ONE(sym) __ATOMIC64_DECL(sym); \
	ATOMIC64_EXPORT(atomic64_##sym)
#endif

/* CONFIG_X86_CMPXCHG64 not set - using _386 versions */
#define __alternative_atomic64(f, g, out, in...) \
	asm volatile("call %P[func]" \
		     : out : [func] "i" (atomic64_##f##_386), ## in)
#define ATOMIC64_DECL(sym) ATOMIC64_DECL_ONE(sym##_386)

#define alternative_atomic64(f, out, in...) \
	__alternative_atomic64(f, f, ASM_OUTPUT2(out), ## in)

/* Only declare assembly functions that are actually used */
ATOMIC64_DECL(read);
ATOMIC64_DECL(set);
ATOMIC64_DECL(add_return);
ATOMIC64_DECL(inc_return);

#undef ATOMIC64_DECL
#undef ATOMIC64_DECL_ONE
#undef __ATOMIC64_DECL
#undef ATOMIC64_EXPORT

 
static inline void arch_atomic64_set(atomic64_t *v, s64 i)
{
	unsigned high = (unsigned)(i >> 32);
	unsigned low = (unsigned)i;
	alternative_atomic64(set,  ,
			     "S" (v), "b" (low), "c" (high)
			     : "eax", "edx", "memory");
}

 
static inline s64 arch_atomic64_read(const atomic64_t *v)
{
	s64 r;
	alternative_atomic64(read, "=&A" (r), "c" (v) : "memory");
	return r;
}

 
static inline s64 arch_atomic64_add_return(s64 i, atomic64_t *v)
{
	alternative_atomic64(add_return,
			     ASM_OUTPUT2("+A" (i), "+c" (v)),
			     ASM_NO_INPUT_CLOBBER("memory"));
	return i;
}
#define arch_atomic64_add_return arch_atomic64_add_return

static inline s64 arch_atomic64_inc_return(atomic64_t *v)
{
	s64 a;
	alternative_atomic64(inc_return, "=&A" (a),
			     "S" (v) : "memory", "ecx");
	return a;
}
#define arch_atomic64_inc_return arch_atomic64_inc_return

#undef alternative_atomic64
#undef __alternative_atomic64

/* Removed unused arch_atomic64_* functions: cmpxchg, xchg, sub_return,
 * dec_return, add, sub, inc, dec, add_unless, inc_not_zero, dec_if_positive,
 * and, fetch_and, or, fetch_or, xor, fetch_xor, fetch_add (~170 LOC) */

#endif
