/* Minimal stub for checksum.h */
#ifndef _NET_CHECKSUM_H
#define _NET_CHECKSUM_H

#include <linux/types.h>

typedef __u16 __sum16;
typedef __u32 __wsum;

static inline __sum16 csum_fold(__wsum csum)
{
	return 0;
}

static inline __wsum csum_partial(const void *buff, int len, __wsum sum)
{
	return 0;
}

#endif /* _NET_CHECKSUM_H */