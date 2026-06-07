#include <asm/cpu_device_id.h>

/* Stub implementation - x86_match_cpu is rarely called */
const struct x86_cpu_id *x86_match_cpu(const struct x86_cpu_id *match)
{
	return NULL;
}
