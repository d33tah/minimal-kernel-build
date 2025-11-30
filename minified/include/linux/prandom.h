 
 
#ifndef _LINUX_PRANDOM_H
#define _LINUX_PRANDOM_H

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/random.h>

static inline u32 prandom_u32(void)
{
	return get_random_u32();
}

static inline void prandom_bytes(void *buf, size_t nbytes)
{
	return get_random_bytes(buf, nbytes);
}

/* Unused rnd_state struct and related functions removed */
struct rnd_state;

static inline u32 prandom_u32_max(u32 ep_ro)
{
	return (u32)(((u64) prandom_u32() * ep_ro) >> 32);
}

 
static inline u32 next_pseudo_random32(u32 seed)
{
	return seed * 1664525 + 1013904223;
}

#endif
