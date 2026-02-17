#include <linux/cpumask.h>

extern raw_spinlock_t jiffies_lock;
extern seqcount_raw_spinlock_t jiffies_seq;

#if HZ < 34
#define JIFFIES_SHIFT	6
#elif HZ < 67
#define JIFFIES_SHIFT	7
#else
#define JIFFIES_SHIFT	8
#endif
