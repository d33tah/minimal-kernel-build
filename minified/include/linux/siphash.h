
#ifndef _LINUX_SIPHASH_H
#define _LINUX_SIPHASH_H

#include <linux/types.h>
#include <linux/kernel.h>

typedef struct {
	u64 key[2];
} siphash_key_t;

u64 __siphash_unaligned(const void *data, size_t len, const siphash_key_t *key);

u64 siphash_1u32(const u32 a, const siphash_key_t *key);

#endif
