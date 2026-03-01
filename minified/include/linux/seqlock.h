#ifndef __LINUX_SEQLOCK_H
#define __LINUX_SEQLOCK_H

#include <linux/compiler.h>
#include <linux/preempt.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#include <asm/processor.h>

typedef struct seqcount {
	unsigned sequence;
} seqcount_t;

#define seqcount_init(s)           \
	do {                       \
		(s)->sequence = 0; \
	} while (0)

#define SEQCNT_ZERO(name) { .sequence = 0 }

#define __SEQ_LOCK(expr)

#define seqcount_LOCKNAME_init(s, _lock, lockname)    \
	do {                                          \
		seqcount_##lockname##_t *____s = (s); \
		seqcount_init(&____s->seqcount);      \
		__SEQ_LOCK(____s->lock = (_lock));    \
	} while (0)

#define seqcount_spinlock_init(s, lock) \
	seqcount_LOCKNAME_init(s, lock, spinlock)

#define SEQCOUNT_LOCKNAME(lockname, locktype, preemptible, lockmember,   \
			  lockbase, lock_acquire)                        \
	typedef struct seqcount_##lockname {                             \
		seqcount_t seqcount;                                     \
		__SEQ_LOCK(locktype *lock);                              \
	} seqcount_##lockname##_t;                                       \
                                                                         \
	static __always_inline seqcount_t *__seqprop_##lockname##_ptr(   \
		seqcount_##lockname##_t *s)                              \
	{                                                                \
		return &s->seqcount;                                     \
	}                                                                \
                                                                         \
	static __always_inline unsigned __seqprop_##lockname##_sequence( \
		const seqcount_##lockname##_t *s)                        \
	{                                                                \
		return READ_ONCE(s->seqcount.sequence);                  \
	}                                                                \
                                                                         \
	static __always_inline bool __seqprop_##lockname##_preemptible(  \
		const seqcount_##lockname##_t *s)                        \
	{                                                                \
		return preemptible;                                      \
	}

static inline seqcount_t *__seqprop_ptr(seqcount_t *s)
{
	return s;
}

static inline bool __seqprop_preemptible(const seqcount_t *s)
{
	return false;
}

#define __SEQ_RT false

SEQCOUNT_LOCKNAME(raw_spinlock, raw_spinlock_t, false, s->lock, raw_spin,
		  raw_spin_lock(s->lock))
SEQCOUNT_LOCKNAME(spinlock, spinlock_t, __SEQ_RT, s->lock, spin,
		  spin_lock(s->lock))

#define SEQCOUNT_LOCKNAME_ZERO(seq_name, assoc_lock)  \
	{ .seqcount = SEQCNT_ZERO(seq_name.seqcount), \
	  __SEQ_LOCK(.lock = (assoc_lock)) }

#define SEQCNT_RAW_SPINLOCK_ZERO(name, lock) SEQCOUNT_LOCKNAME_ZERO(name, lock)
#define SEQCNT_SPINLOCK_ZERO(name, lock) SEQCOUNT_LOCKNAME_ZERO(name, lock)

#define __seqprop_case(s, lockname, prop) \
	seqcount_##lockname##_t : __seqprop_##lockname##_##prop((void *)(s))

#define __seqprop(s, prop)                                 \
	_Generic(*(s),                                     \
		seqcount_t: __seqprop_##prop((void *)(s)), \
		__seqprop_case((s), raw_spinlock, prop),   \
		__seqprop_case((s), spinlock, prop))

#define seqprop_ptr(s) __seqprop(s, ptr)
#define seqprop_preemptible(s) __seqprop(s, preemptible)

#define raw_write_seqcount_begin(s)                          \
	do {                                                 \
		if (seqprop_preemptible(s))                  \
			preempt_disable();                   \
                                                             \
		do_raw_write_seqcount_begin(seqprop_ptr(s)); \
	} while (0)

static inline void do_raw_write_seqcount_begin(seqcount_t *s)
{
	s->sequence++;
	smp_wmb();
}

#define raw_write_seqcount_end(s)                          \
	do {                                               \
		do_raw_write_seqcount_end(seqprop_ptr(s)); \
                                                           \
		if (seqprop_preemptible(s))                \
			preempt_enable();                  \
	} while (0)

static inline void do_raw_write_seqcount_end(seqcount_t *s)
{
	smp_wmb();
	s->sequence++;
}

#define write_seqcount_begin(s)                              \
	do {                                                 \
		if (seqprop_preemptible(s))                  \
			preempt_disable();                   \
                                                             \
		do_raw_write_seqcount_begin(seqprop_ptr(s)); \
	} while (0)

#define write_seqcount_end(s)                              \
	do {                                               \
		do_raw_write_seqcount_end(seqprop_ptr(s)); \
                                                           \
		if (seqprop_preemptible(s))                \
			preempt_enable();                  \
	} while (0)

#define write_seqcount_invalidate(s) \
	do_write_seqcount_invalidate(seqprop_ptr(s))

static inline void do_write_seqcount_invalidate(seqcount_t *s)
{
	smp_wmb();
	s->sequence += 2;
}

typedef struct {
	seqcount_t seqcount;
} seqcount_latch_t;

#define seqcount_latch_init(s) seqcount_init(&(s)->seqcount)

static inline void raw_write_seqcount_latch(seqcount_latch_t *s)
{
	smp_wmb();
	s->seqcount.sequence++;
	smp_wmb();
}

#endif
