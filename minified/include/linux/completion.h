#ifndef __LINUX_COMPLETION_H
#define __LINUX_COMPLETION_H

struct completion {};

static inline void init_completion(struct completion *x)
{
}

static inline void wait_for_completion(struct completion *x)
{
}
static inline void complete(struct completion *x)
{
}

#endif
