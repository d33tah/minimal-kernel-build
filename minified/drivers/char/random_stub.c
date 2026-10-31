/* Stub random number generator */
#include <linux/string.h>
#include <linux/random.h>

void get_random_bytes(void *buf, size_t len)
{
	memset(buf, 0, len);
}
