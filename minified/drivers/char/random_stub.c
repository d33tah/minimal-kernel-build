/* Stub random number generator */
#include <linux/random.h>
#include <linux/string.h>


void get_random_bytes(void *buf, size_t len) {
	memset(buf, 0, len); }

u32 get_random_u32(void) {
	return 0; }

bool rng_is_initialized(void) {
	return true; }
