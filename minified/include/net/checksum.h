 
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

static inline __wsum csum_block_add(__wsum csum, __wsum csum2, int offset)
{
	return csum;
}

static inline __wsum csum_block_sub(__wsum csum, __wsum csum2, int offset)
{
	return csum;
}

static inline __wsum csum_add(__wsum csum, __wsum addend)
{
	return csum;
}

static inline __wsum remcsum_adjust(void *ptr, __wsum csum, int start, int offset)
{
	return csum;
}

static inline __sum16 csum_unfold(__wsum csum)
{
	return 0;
}

static inline __wsum wsum_negate(__wsum csum)
{
	return csum;
}

static inline __wsum csum_partial_copy_nocheck(const void *src, void *dst, int len)
{
	return 0;
}

#endif  