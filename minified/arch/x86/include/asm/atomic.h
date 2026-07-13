 
#ifndef _ASM_X86_ATOMIC_H
#define _ASM_X86_ATOMIC_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/alternative.h>
#include <asm/cmpxchg.h>
#include <asm/rmwcc.h>
#include <asm/barrier.h>

 

 
static __always_inline int arch_atomic_read(const atomic_t *v) {
	 
	return __READ_ONCE((v)->counter); }

 
static __always_inline void arch_atomic_set(atomic_t *v, int i) {
	__WRITE_ONCE(v->counter, i); }

 
static __always_inline void arch_atomic_add(int i, atomic_t *v) {
	asm volatile(LOCK_PREFIX "addl %1,%0" : "+m" (v->counter) : "ir" (i) : "memory"); }

 

static __always_inline bool arch_atomic_sub_and_test(int i, atomic_t *v) {
	return GEN_BINARY_RMWcc(LOCK_PREFIX "subl", v->counter, e, "er", i); }

 
static __always_inline void arch_atomic_inc(atomic_t *v) {
	asm volatile(LOCK_PREFIX "incl %0" : "+m" (v->counter) :: "memory"); }

 
static __always_inline void arch_atomic_dec(atomic_t *v) {
	asm volatile(LOCK_PREFIX "decl %0" : "+m" (v->counter) :: "memory"); }

 
static __always_inline bool arch_atomic_dec_and_test(atomic_t *v) {
	return GEN_UNARY_RMWcc(LOCK_PREFIX "decl", v->counter, e); }

 
static __always_inline bool arch_atomic_inc_and_test(atomic_t *v) {
	return GEN_UNARY_RMWcc(LOCK_PREFIX "incl", v->counter, e); }

 
static __always_inline bool arch_atomic_add_negative(int i, atomic_t *v) {
	return GEN_BINARY_RMWcc(LOCK_PREFIX "addl", v->counter, s, "er", i); }

 
static __always_inline int arch_atomic_add_return(int i, atomic_t *v) {
	return i + xadd(&v->counter, i); }

 
static __always_inline int arch_atomic_sub_return(int i, atomic_t *v) {
	return arch_atomic_add_return(-i, v); }

static __always_inline int arch_atomic_fetch_add(int i, atomic_t *v) {
	return xadd(&v->counter, i); }

static __always_inline int arch_atomic_fetch_sub(int i, atomic_t *v) {
	return xadd(&v->counter, -i); }

static __always_inline int arch_atomic_cmpxchg(atomic_t *v, int old, int new) {
	return arch_cmpxchg(&v->counter, old, new); }

static __always_inline bool arch_atomic_try_cmpxchg(atomic_t *v, int *old, int new) {
	return arch_try_cmpxchg(&v->counter, old, new); }
#define arch_atomic_try_cmpxchg arch_atomic_try_cmpxchg

static __always_inline int arch_atomic_xchg(atomic_t *v, int new) {
	return arch_xchg(&v->counter, new); }


static __always_inline int arch_atomic_fetch_and(int i, atomic_t *v) {
	int val = arch_atomic_read(v);

	do { } while (!arch_atomic_try_cmpxchg(v, &val, val & i));

	return val; }


static __always_inline int arch_atomic_fetch_or(int i, atomic_t *v) {
	int val = arch_atomic_read(v);

	do { } while (!arch_atomic_try_cmpxchg(v, &val, val | i));

	return val; }


static __always_inline int arch_atomic_fetch_xor(int i, atomic_t *v) {
	int val = arch_atomic_read(v);

	do { } while (!arch_atomic_try_cmpxchg(v, &val, val ^ i));

	return val; }

# include <asm/atomic64_32.h>

#endif  
