#ifndef __LINUX_COMPILER_H
#define __LINUX_COMPILER_H

#include <linux/compiler_types.h>

#ifndef __ASSEMBLY__

#ifdef __KERNEL__

/* CONFIG_TRACE_BRANCH_PROFILING not enabled - simple likely/unlikely */
#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#define likely_notrace(x)	likely(x)
#define unlikely_notrace(x)	unlikely(x)

#ifndef barrier
# define barrier() __asm__ __volatile__("": : :"memory")
#endif

#ifndef barrier_data
# define barrier_data(ptr) __asm__ __volatile__("": :"r"(ptr) :"memory")
#endif

#ifndef barrier_before_unreachable
# define barrier_before_unreachable() do { } while (0)
#endif

#define annotate_unreachable()
#define __annotate_jump_table  

#ifndef unreachable
# define unreachable() do {		\
	annotate_unreachable();		\
	__builtin_unreachable();	\
} while (0)
#endif

#ifndef KENTRY
# define KENTRY(sym)						\
	extern typeof(sym) sym;					\
	static const unsigned long __kentry_##sym		\
	__used							\
	__attribute__((__section__("___kentry+" #sym)))		\
	= (unsigned long)&sym;
#endif

#ifndef RELOC_HIDE
# define RELOC_HIDE(ptr, off)					\
  ({ unsigned long __ptr;					\
     __ptr = (unsigned long) (ptr);				\
    (typeof(ptr)) (__ptr + (off)); })
#endif

#define absolute_pointer(val)	RELOC_HIDE((void *)(val), 0)

#ifndef OPTIMIZER_HIDE_VAR
#define OPTIMIZER_HIDE_VAR(var)						\
	__asm__ ("" : "=r" (var) : "0" (var))
#endif

#ifndef __UNIQUE_ID
# define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __LINE__)
#endif

#define data_race(expr)							\
({									\
	__unqual_scalar_typeof(({ expr; })) __v = ({			\
						\
		expr;							\
	});								\
						\
	__v;								\
})

#define ASSERT_EXCLUSIVE_BITS(var, mask) do { } while (0)
#define ASSERT_EXCLUSIVE_WRITER(var) do { } while (0)

#endif  

#define __ADDRESSABLE(sym) \
	static void * __section(".discard.addressable") __used \
		__UNIQUE_ID(__PASTE(__addressable_,sym)) = (void *)&sym;

static inline void *offset_to_ptr(const int *off)
{
	return (void *)((unsigned long)off + *off);
}

#endif  

#define __must_be_array(a)	BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))

#define prevent_tail_call_optimization()	mb()

#include <asm/rwonce.h>

#endif  
