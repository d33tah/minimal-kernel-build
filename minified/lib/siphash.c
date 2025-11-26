/* Minimal stub for siphash - cryptographic hash function
 * Used only by vsprintf.c for pointer address hashing
 * Original: 358 LOC, Stubbed to simple hash
 */

#include <linux/siphash.h>
#include <asm/unaligned.h>
#include <linux/bug.h>

/* Simplified hash - not cryptographically secure but sufficient
 * for pointer obfuscation in vsprintf
 */

u64 __siphash_unaligned(const void *data, size_t len, const siphash_key_t *key)
{
	const u8 *p = data;
	u64 hash = key->key[0] ^ key->key[1];
	size_t i;

	for (i = 0; i < len; i++)
		hash = hash * 31 + p[i];

	return hash;
}

/* Stub: __siphash_aligned not used externally */
u64 __siphash_aligned(const void *data, size_t len, const siphash_key_t *key) { BUG(); }

/* Stub: siphash_1u64 not used externally */
u64 siphash_1u64(const u64 first, const siphash_key_t *key) { BUG(); }

/* Stub: siphash_2u64 not used externally */
u64 siphash_2u64(const u64 first, const u64 second, const siphash_key_t *key) { BUG(); }

/* Stub: siphash_3u64 not used externally */
u64 siphash_3u64(const u64 first, const u64 second, const u64 third,
		 const siphash_key_t *key) { BUG(); }

/* Stub: siphash_4u64 not used externally */
u64 siphash_4u64(const u64 first, const u64 second, const u64 third,
		 const u64 fourth, const siphash_key_t *key) { BUG(); }

u64 siphash_1u32(const u32 first, const siphash_key_t *key)
{
	return __siphash_unaligned(&first, sizeof(first), key);
}

/* Stub: siphash_3u32 not used externally */
u64 siphash_3u32(const u32 first, const u32 second, const u32 third,
		 const siphash_key_t *key) { BUG(); }

/* Stub: __hsiphash_unaligned not used externally */
u32 __hsiphash_unaligned(const void *data, size_t len,
			 const hsiphash_key_t *key) { BUG(); }

/* Stub: __hsiphash_aligned not used externally */
u32 __hsiphash_aligned(const void *data, size_t len,
		       const hsiphash_key_t *key) { BUG(); }

/* Stub: hsiphash_1u32 not used externally */
u32 hsiphash_1u32(const u32 first, const hsiphash_key_t *key) { BUG(); }

/* Stub: hsiphash_2u32 not used externally */
u32 hsiphash_2u32(const u32 first, const u32 second, const hsiphash_key_t *key) { BUG(); }

/* Stub: hsiphash_3u32 not used externally */
u32 hsiphash_3u32(const u32 first, const u32 second, const u32 third,
		  const hsiphash_key_t *key) { BUG(); }

/* Stub: hsiphash_4u32 not used externally */
u32 hsiphash_4u32(const u32 first, const u32 second, const u32 third,
		  const u32 fourth, const hsiphash_key_t *key) { BUG(); }
