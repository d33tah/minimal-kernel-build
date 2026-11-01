/*
 * No entropy pool and no CRNG on this kernel: upstream's get_random_bytes()
 * extracts a ChaCha key from the BLAKE2s-hashed input pool, and none of that
 * machinery exists here. The buffer is filled with zeros, so the AT_RANDOM
 * bytes given to /init are constant (documented feature loss).
 */
#include <linux/string.h>
#include <linux/random.h>

/*
 * This function is the exported kernel interface.  It returns some
 * number of good random numbers, suitable for key generation, seeding
 * TCP sequence numbers, etc. In order to ensure that the randomness
 * by this function is okay, the function wait_for_random_bytes()
 * should be called and return 0 at least once at any point prior.
 */
void get_random_bytes(void *buf, size_t len)
{
	memset(buf, 0, len);
}
