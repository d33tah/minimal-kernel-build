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

/* Removed: __siphash_aligned, siphash_1u64, siphash_2u64, siphash_3u64,
   siphash_4u64, siphash_3u32 - no external callers */

u64 siphash_1u32(const u32 first, const siphash_key_t *key)
{
	return __siphash_unaligned(&first, sizeof(first), key);
}

/* Removed: __hsiphash_unaligned, __hsiphash_aligned, hsiphash_1u32,
   hsiphash_2u32, hsiphash_3u32, hsiphash_4u32 - no external callers */
