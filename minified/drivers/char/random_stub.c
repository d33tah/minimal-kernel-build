/* Stub random number generator */
#include <linux/random.h>
#include <linux/string.h>

void get_random_bytes(void *buf, size_t len)
{
	memset(buf, 0, len);
}
