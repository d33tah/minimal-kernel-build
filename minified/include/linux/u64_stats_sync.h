 
#ifndef _LINUX_U64_STATS_SYNC_H
#define _LINUX_U64_STATS_SYNC_H

 
#include <linux/seqlock.h>

struct u64_stats_sync {
};

/* BITS_PER_LONG == 32 (building for i386) */
typedef struct {
	u64		v;
} u64_stats_t;

static inline u64 u64_stats_read(const u64_stats_t *p)
{
	return p->v;
}

static inline void u64_stats_set(u64_stats_t *p, u64 val)
{
	p->v = val;
}

static inline void u64_stats_add(u64_stats_t *p, unsigned long val)
{
	p->v += val;
}

static inline void u64_stats_inc(u64_stats_t *p)
{
	p->v++;
}

static inline void u64_stats_init(struct u64_stats_sync *syncp)
{
}

static inline void u64_stats_update_begin(struct u64_stats_sync *syncp)
{
}

static inline void u64_stats_update_end(struct u64_stats_sync *syncp)
{
}

static inline unsigned long
u64_stats_update_begin_irqsave(struct u64_stats_sync *syncp)
{
	unsigned long flags = 0;

	return flags;
}

static inline void
u64_stats_update_end_irqrestore(struct u64_stats_sync *syncp,
				unsigned long flags)
{
}

static inline unsigned int __u64_stats_fetch_begin(const struct u64_stats_sync *syncp)
{
	return 0;
}

static inline unsigned int u64_stats_fetch_begin(const struct u64_stats_sync *syncp)
{
#if BITS_PER_LONG == 32 && (!defined(CONFIG_SMP) && !defined(CONFIG_PREEMPT_RT))
	preempt_disable();
#endif
	return __u64_stats_fetch_begin(syncp);
}

static inline bool __u64_stats_fetch_retry(const struct u64_stats_sync *syncp,
					 unsigned int start)
{
	return false;
}

static inline bool u64_stats_fetch_retry(const struct u64_stats_sync *syncp,
					 unsigned int start)
{
#if BITS_PER_LONG == 32 && (!defined(CONFIG_SMP) && !defined(CONFIG_PREEMPT_RT))
	preempt_enable();
#endif
	return __u64_stats_fetch_retry(syncp, start);
}

 
static inline unsigned int u64_stats_fetch_begin_irq(const struct u64_stats_sync *syncp)
{
#if   BITS_PER_LONG == 32 && !defined(CONFIG_SMP)
	local_irq_disable();
#endif
	return __u64_stats_fetch_begin(syncp);
}

static inline bool u64_stats_fetch_retry_irq(const struct u64_stats_sync *syncp,
					     unsigned int start)
{
#if   BITS_PER_LONG == 32 && !defined(CONFIG_SMP)
	local_irq_enable();
#endif
	return __u64_stats_fetch_retry(syncp, start);
}

#endif  
