
#ifndef _LINUX_SIPHASH_H
#define _LINUX_SIPHASH_H

#include <linux/types.h>
#include <linux/kernel.h>

typedef struct {
	u64 key[2];
} siphash_key_t;

#define siphash_aligned_key_t siphash_key_t __aligned(16)


u64 __siphash_aligned(const void *data, size_t len, const siphash_key_t *key);
u64 __siphash_unaligned(const void *data, size_t len, const siphash_key_t *key);

u64 siphash_1u64(const u64 a, const siphash_key_t *key);
u64 siphash_2u64(const u64 a, const u64 b, const siphash_key_t *key);
u64 siphash_3u64(const u64 a, const u64 b, const u64 c,
		 const siphash_key_t *key);
u64 siphash_4u64(const u64 a, const u64 b, const u64 c, const u64 d,
		 const siphash_key_t *key);
u64 siphash_1u32(const u32 a, const siphash_key_t *key);
u64 siphash_3u32(const u32 a, const u32 b, const u32 c,
		 const siphash_key_t *key);




static inline u64 siphash(const void *data, size_t len,
			  const siphash_key_t *key)
{
	return __siphash_unaligned(data, len, key);
}

typedef struct {
	unsigned long key[2];
} hsiphash_key_t;

u32 __hsiphash_aligned(const void *data, size_t len,
		       const hsiphash_key_t *key);
u32 __hsiphash_unaligned(const void *data, size_t len,
			 const hsiphash_key_t *key);

u32 hsiphash_1u32(const u32 a, const hsiphash_key_t *key);
u32 hsiphash_2u32(const u32 a, const u32 b, const hsiphash_key_t *key);
u32 hsiphash_3u32(const u32 a, const u32 b, const u32 c,
		  const hsiphash_key_t *key);
u32 hsiphash_4u32(const u32 a, const u32 b, const u32 c, const u32 d,
		  const hsiphash_key_t *key);



/* SIPHASH_PERMUTATION, SIPHASH_CONST_*, HSIPHASH_PERMUTATION, HSIPHASH_CONST_*
   removed - never used */

#endif
