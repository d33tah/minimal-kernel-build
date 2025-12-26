
#include <linux/ratelimit.h>
#include <linux/export.h>

/* Simplified for minimal kernel - no rate limiting needed */
int ___ratelimit(struct ratelimit_state *rs, const char *func)
{
	return 1; /* Always allow */
}
