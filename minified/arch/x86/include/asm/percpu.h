 
#ifndef _ASM_X86_PERCPU_H
#define _ASM_X86_PERCPU_H

#ifdef __ASSEMBLY__

#define PER_CPU_VAR(var)	var

#else  

#include <linux/kernel.h>
#include <linux/stringify.h>

#define __percpu_prefix		""

#define __percpu_arg(x)		__percpu_prefix "%" #x

#define __pcpu_type_1 u8
#define __pcpu_type_2 u16
#define __pcpu_type_4 u32
#define __pcpu_type_8 u64

#define __pcpu_cast_1(val) ((u8)(((unsigned long) val) & 0xff))
#define __pcpu_cast_2(val) ((u16)(((unsigned long) val) & 0xffff))
#define __pcpu_cast_4(val) ((u32)(((unsigned long) val) & 0xffffffff))
#define __pcpu_cast_8(val) ((u64)(val))

#define __pcpu_op1_1(op, dst) op "b " dst
#define __pcpu_op1_2(op, dst) op "w " dst
#define __pcpu_op1_4(op, dst) op "l " dst
#define __pcpu_op1_8(op, dst) op "q " dst

#define __pcpu_op2_1(op, src, dst) op "b " src ", " dst
#define __pcpu_op2_2(op, src, dst) op "w " src ", " dst
#define __pcpu_op2_4(op, src, dst) op "l " src ", " dst
#define __pcpu_op2_8(op, src, dst) op "q " src ", " dst

#define __pcpu_reg_1(mod, x) mod "q" (x)
#define __pcpu_reg_2(mod, x) mod "r" (x)
#define __pcpu_reg_4(mod, x) mod "r" (x)
#define __pcpu_reg_8(mod, x) mod "r" (x)

#define __pcpu_reg_imm_1(x) "qi" (x)
#define __pcpu_reg_imm_2(x) "ri" (x)
#define __pcpu_reg_imm_4(x) "ri" (x)
#define __pcpu_reg_imm_8(x) "re" (x)

#define percpu_to_op(size, qual, op, _var, _val)			\
do {									\
	__pcpu_type_##size pto_val__ = __pcpu_cast_##size(_val);	\
	if (0) {		                                        \
		typeof(_var) pto_tmp__;					\
		pto_tmp__ = (_val);					\
		(void)pto_tmp__;					\
	}								\
	asm qual(__pcpu_op2_##size(op, "%[val]", __percpu_arg([var]))	\
	    : [var] "+m" (_var)						\
	    : [val] __pcpu_reg_imm_##size(pto_val__));			\
} while (0)

#define percpu_unary_op(size, qual, op, _var)				\
({									\
	asm qual (__pcpu_op1_##size(op, __percpu_arg([var]))		\
	    : [var] "+m" (_var));					\
})

#define percpu_add_op(size, qual, var, val)				\
do {									\
	const int pao_ID__ = (__builtin_constant_p(val) &&		\
			      ((val) == 1 || (val) == -1)) ?		\
				(int)(val) : 0;				\
	if (0) {							\
		typeof(var) pao_tmp__;					\
		pao_tmp__ = (val);					\
		(void)pao_tmp__;					\
	}								\
	if (pao_ID__ == 1)						\
		percpu_unary_op(size, qual, "inc", var);		\
	else if (pao_ID__ == -1)					\
		percpu_unary_op(size, qual, "dec", var);		\
	else								\
		percpu_to_op(size, qual, "add", var, val);		\
} while (0)

#define percpu_from_op(size, qual, op, _var)				\
({									\
	__pcpu_type_##size pfo_val__;					\
	asm qual (__pcpu_op2_##size(op, __percpu_arg([var]), "%[val]")	\
	    : [val] __pcpu_reg_##size("=", pfo_val__)			\
	    : [var] "m" (_var));					\
	(typeof(_var))(unsigned long) pfo_val__;			\
})

#define percpu_stable_op(size, op, _var)				\
({									\
	__pcpu_type_##size pfo_val__;					\
	asm(__pcpu_op2_##size(op, __percpu_arg(P[var]), "%[val]")	\
	    : [val] __pcpu_reg_##size("=", pfo_val__)			\
	    : [var] "p" (&(_var)));					\
	(typeof(_var))(unsigned long) pfo_val__;			\
})

#define percpu_cmpxchg_op(size, qual, _var, _oval, _nval)		\
({									\
	__pcpu_type_##size pco_old__ = __pcpu_cast_##size(_oval);	\
	__pcpu_type_##size pco_new__ = __pcpu_cast_##size(_nval);	\
	asm qual (__pcpu_op2_##size("cmpxchg", "%[nval]",		\
				    __percpu_arg([var]))		\
		  : [oval] "+a" (pco_old__),				\
		    [var] "+m" (_var)					\
		  : [nval] __pcpu_reg_##size(, pco_new__)		\
		  : "memory");						\
	(typeof(_var))(unsigned long) pco_old__;			\
})

#define this_cpu_read_stable_1(pcp)	percpu_stable_op(1, "mov", pcp)
#define this_cpu_read_stable_2(pcp)	percpu_stable_op(2, "mov", pcp)
#define this_cpu_read_stable_4(pcp)	percpu_stable_op(4, "mov", pcp)
#define this_cpu_read_stable_8(pcp)	percpu_stable_op(8, "mov", pcp)
#define this_cpu_read_stable(pcp)	__pcpu_size_call_return(this_cpu_read_stable_, pcp)

#define raw_cpu_read_1(pcp)		percpu_from_op(1, , "mov", pcp)
#define raw_cpu_read_2(pcp)		percpu_from_op(2, , "mov", pcp)
#define raw_cpu_read_4(pcp)		percpu_from_op(4, , "mov", pcp)

#define raw_cpu_write_1(pcp, val)	percpu_to_op(1, , "mov", (pcp), val)
#define raw_cpu_write_2(pcp, val)	percpu_to_op(2, , "mov", (pcp), val)
#define raw_cpu_write_4(pcp, val)	percpu_to_op(4, , "mov", (pcp), val)
#define raw_cpu_add_4(pcp, val)		percpu_add_op(4, , (pcp), val)
#define raw_cpu_and_4(pcp, val)		percpu_to_op(4, , "and", (pcp), val)
#define raw_cpu_or_1(pcp, val)		percpu_to_op(1, , "or", (pcp), val)
#define raw_cpu_or_2(pcp, val)		percpu_to_op(2, , "or", (pcp), val)
#define raw_cpu_or_4(pcp, val)		percpu_to_op(4, , "or", (pcp), val)

#define this_cpu_read_1(pcp)		percpu_from_op(1, volatile, "mov", pcp)
#define this_cpu_read_2(pcp)		percpu_from_op(2, volatile, "mov", pcp)
#define this_cpu_read_4(pcp)		percpu_from_op(4, volatile, "mov", pcp)
#define this_cpu_write_1(pcp, val)	percpu_to_op(1, volatile, "mov", (pcp), val)
#define this_cpu_write_2(pcp, val)	percpu_to_op(2, volatile, "mov", (pcp), val)
#define this_cpu_write_4(pcp, val)	percpu_to_op(4, volatile, "mov", (pcp), val)
#define raw_cpu_cmpxchg_4(pcp, oval, nval)	percpu_cmpxchg_op(4, , pcp, oval, nval)

#include <linux/compiler.h>
#include <linux/threads.h>
#include <linux/percpu-defs.h>

#ifndef PER_CPU_BASE_SECTION
#define PER_CPU_BASE_SECTION ".data"
#endif

#ifndef PER_CPU_ATTRIBUTES
#define PER_CPU_ATTRIBUTES
#endif

#define raw_cpu_generic_read(pcp)					\
({									\
	*raw_cpu_ptr(&(pcp));						\
})

#define raw_cpu_generic_to_op(pcp, val, op)				\
do {									\
	*raw_cpu_ptr(&(pcp)) op val;					\
} while (0)

#define __this_cpu_generic_read_nopreempt(pcp)				\
({									\
	typeof(pcp) ___ret;						\
	preempt_disable_notrace();					\
	___ret = READ_ONCE(*raw_cpu_ptr(&(pcp)));			\
	preempt_enable_notrace();					\
	___ret;								\
})

#define __this_cpu_generic_read_noirq(pcp)				\
({									\
	typeof(pcp) ___ret;						\
	unsigned long ___flags;						\
	raw_local_irq_save(___flags);					\
	___ret = raw_cpu_generic_read(pcp);				\
	raw_local_irq_restore(___flags);				\
	___ret;								\
})

#define this_cpu_generic_read(pcp)					\
({									\
	typeof(pcp) __ret;						\
	if (__native_word(pcp))						\
		__ret = __this_cpu_generic_read_nopreempt(pcp);		\
	else								\
		__ret = __this_cpu_generic_read_noirq(pcp);		\
	__ret;								\
})

#define this_cpu_generic_to_op(pcp, val, op)				\
do {									\
	unsigned long __flags;						\
	raw_local_irq_save(__flags);					\
	raw_cpu_generic_to_op(pcp, val, op);				\
	raw_local_irq_restore(__flags);					\
} while (0)

#ifndef raw_cpu_read_8
#define raw_cpu_read_8(pcp)		raw_cpu_generic_read(pcp)
#endif
#ifndef raw_cpu_write_8
#define raw_cpu_write_8(pcp, val)	raw_cpu_generic_to_op(pcp, val, =)
#endif
#ifndef raw_cpu_or_8
#define raw_cpu_or_8(pcp, val)		raw_cpu_generic_to_op(pcp, val, |=)
#endif
#ifndef this_cpu_read_8
#define this_cpu_read_8(pcp)		this_cpu_generic_read(pcp)
#endif
#ifndef this_cpu_write_8
#define this_cpu_write_8(pcp, val)	this_cpu_generic_to_op(pcp, val, =)
#endif
DECLARE_PER_CPU_READ_MOSTLY(unsigned long, this_cpu_off);

#endif  

#endif  
