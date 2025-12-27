#ifndef _LINUX_SIPHASH_H
#define _LINUX_SIPHASH_H
#include <linux/types.h>
#include <linux/kernel.h>
typedef struct { u64 key[2]; } siphash_key_t;
#define siphash_aligned_key_t siphash_key_t __aligned(16)
u64 __siphash_aligned(const void *data, size_t len, const siphash_key_t *key);
u64 __siphash_unaligned(const void *data, size_t len, const siphash_key_t *key);
u64 siphash_1u64(const u64 a, const siphash_key_t *key);
u64 siphash_2u64(const u64 a, const u64 b, const siphash_key_t *key);
u64 siphash_3u64(const u64 a, const u64 b, const u64 c, const siphash_key_t *key);
u64 siphash_4u64(const u64 a, const u64 b, const u64 c, const u64 d, const siphash_key_t *key);
u64 siphash_1u32(const u32 a, const siphash_key_t *key);
u64 siphash_3u32(const u32 a, const u32 b, const u32 c, const siphash_key_t *key);
/* hsiphash functions and hsiphash_key_t removed - unused */
#endif
