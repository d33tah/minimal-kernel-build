
#ifndef _ASM_X86_ATOMIC64_32_H
#define _ASM_X86_ATOMIC64_32_H

#include <linux/compiler.h>
#include <linux/types.h>

typedef struct {
	s64 __aligned(8) counter;
} atomic64_t;

#define ATOMIC64_INIT(val) { (val) }

#define __ATOMIC64_DECL(sym) void atomic64_##sym(atomic64_t *, ...)
#define ATOMIC64_DECL_ONE __ATOMIC64_DECL

#define atomic64_call(f, out, in...) \
	asm volatile("call %P[func]" \
		     : out           \
		     : [func] "i"(atomic64_##f##_cx8), ##in)

#define ATOMIC64_DECL(sym) ATOMIC64_DECL_ONE(sym##_cx8)

ATOMIC64_DECL(read);
ATOMIC64_DECL(set);
ATOMIC64_DECL(xchg);
ATOMIC64_DECL(add_return);
ATOMIC64_DECL(sub_return);
ATOMIC64_DECL(inc_return);
ATOMIC64_DECL(dec_return);
ATOMIC64_DECL(dec_if_positive);
ATOMIC64_DECL(inc_not_zero);
ATOMIC64_DECL(add_unless);

#undef ATOMIC64_DECL
#undef ATOMIC64_DECL_ONE
#undef __ATOMIC64_DECL

static inline void arch_atomic64_set(atomic64_t *v, s64 i)
{
	unsigned high = (unsigned)(i >> 32);
	unsigned low = (unsigned)i;
	atomic64_call(set, , "S"(v), "b"(low), "c"(high) : "eax", "edx",
		      "memory");
}

static inline s64 arch_atomic64_read(const atomic64_t *v)
{
	s64 r;
	atomic64_call(read, "=&A"(r), "c"(v) : "memory");
	return r;
}

static inline s64 arch_atomic64_inc_return(atomic64_t *v)
{
	s64 a;
	atomic64_call(inc_return, "=&A"(a), "S"(v) : "memory", "ecx");
	return a;
}

#undef alternative_atomic64
#undef __alternative_atomic64

#endif
