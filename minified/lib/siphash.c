/* Minimal stub for siphash - cryptographic hash function
 * Used only by vsprintf.c for pointer address hashing
 * Original: 358 LOC, Stubbed to simple hash
 */

#include <linux/siphash.h>
#include <asm/unaligned.h>

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

u64 __siphash_aligned(const void *data, size_t len, const siphash_key_t *key)
{
	return __siphash_unaligned(data, len, key);
}

u64 siphash_1u64(const u64 first, const siphash_key_t *key)
{
	return __siphash_unaligned(&first, sizeof(first), key);
}

u64 siphash_2u64(const u64 first, const u64 second, const siphash_key_t *key)
{
	u64 data[2] = { first, second };
	return __siphash_unaligned(data, sizeof(data), key);
}

u64 siphash_3u64(const u64 first, const u64 second, const u64 third,
		 const siphash_key_t *key)
{
	u64 data[3] = { first, second, third };
	return __siphash_unaligned(data, sizeof(data), key);
}

u64 siphash_4u64(const u64 first, const u64 second, const u64 third,
		 const u64 fourth, const siphash_key_t *key)
{
	u64 data[4] = { first, second, third, fourth };
	return __siphash_unaligned(data, sizeof(data), key);
}

u64 siphash_1u32(const u32 first, const siphash_key_t *key)
{
	return __siphash_unaligned(&first, sizeof(first), key);
}

u64 siphash_3u32(const u32 first, const u32 second, const u32 third,
		 const siphash_key_t *key)
{
	u32 data[3] = { first, second, third };
	return __siphash_unaligned(data, sizeof(data), key);
}

/* Half-size siphash variants - use same simple hash */

u32 __hsiphash_unaligned(const void *data, size_t len,
			 const hsiphash_key_t *key)
{
	const u8 *p = data;
	u32 hash = key->key[0] ^ key->key[1];
	size_t i;

	for (i = 0; i < len; i++)
		hash = hash * 31 + p[i];

	return hash;
}

u32 __hsiphash_aligned(const void *data, size_t len,
		       const hsiphash_key_t *key)
{
	return __hsiphash_unaligned(data, len, key);
}

u32 hsiphash_1u32(const u32 first, const hsiphash_key_t *key)
{
	return __hsiphash_unaligned(&first, sizeof(first), key);
}

u32 hsiphash_2u32(const u32 first, const u32 second, const hsiphash_key_t *key)
{
	u32 data[2] = { first, second };
	return __hsiphash_unaligned(data, sizeof(data), key);
}

u32 hsiphash_3u32(const u32 first, const u32 second, const u32 third,
		  const hsiphash_key_t *key)
{
	u32 data[3] = { first, second, third };
	return __hsiphash_unaligned(data, sizeof(data), key);
}

u32 hsiphash_4u32(const u32 first, const u32 second, const u32 third,
		  const u32 fourth, const hsiphash_key_t *key)
{
	u32 data[4] = { first, second, third, fourth };
	return __hsiphash_unaligned(data, sizeof(data), key);
}
