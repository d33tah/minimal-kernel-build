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

typedef struct seqcount_raw_spinlock {
	seqcount_t seqcount;
} seqcount_raw_spinlock_t;

typedef struct seqcount_spinlock {
	seqcount_t seqcount;
} seqcount_spinlock_t;

#define seqcount_spinlock_init(s, lock) seqcount_init(&(s)->seqcount)

#define SEQCOUNT_LOCKNAME_ZERO(seq_name, assoc_lock) \
	{ .seqcount = SEQCNT_ZERO(seq_name.seqcount) }

#define SEQCNT_RAW_SPINLOCK_ZERO(name, lock) SEQCOUNT_LOCKNAME_ZERO(name, lock)
#define SEQCNT_SPINLOCK_ZERO(name, lock) SEQCOUNT_LOCKNAME_ZERO(name, lock)

/* All seqcount variants resolve to the base seqcount_t pointer */
#define seqprop_ptr(s) \
	_Generic(*(s), \
		seqcount_t: (seqcount_t *)(s), \
		seqcount_raw_spinlock_t: &((seqcount_raw_spinlock_t *)(s))->seqcount, \
		seqcount_spinlock_t: &((seqcount_spinlock_t *)(s))->seqcount)

static inline void do_raw_write_seqcount_begin(seqcount_t *s)
{
	s->sequence++;
	smp_wmb();
}

static inline void do_raw_write_seqcount_end(seqcount_t *s)
{
	smp_wmb();
	s->sequence++;
}

#define raw_write_seqcount_begin(s) do_raw_write_seqcount_begin(seqprop_ptr(s))
#define raw_write_seqcount_end(s) do_raw_write_seqcount_end(seqprop_ptr(s))
#define write_seqcount_begin(s) do_raw_write_seqcount_begin(seqprop_ptr(s))
#define write_seqcount_end(s) do_raw_write_seqcount_end(seqprop_ptr(s))

#define write_seqcount_invalidate(s) \
	do_write_seqcount_invalidate(seqprop_ptr(s))

static inline void do_write_seqcount_invalidate(seqcount_t *s)
{
	smp_wmb();
	s->sequence += 2;
}

#endif
