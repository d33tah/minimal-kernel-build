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

u64 siphash_1u32(const u32 first, const siphash_key_t *key)
{
	const u8 *p = (const u8 *)&first;
	u64 hash = key->key[0] ^ key->key[1];
	size_t i;

	for (i = 0; i < sizeof(first); i++)
		hash = hash * 31 + p[i];

	return hash;
}
