#include <asm/cpu_device_id.h>

/* Stub implementations - these functions are never called */
const struct x86_cpu_id *x86_match_cpu(const struct x86_cpu_id *match)
{
	return NULL;
}

bool x86_cpu_has_min_microcode_rev(const struct x86_cpu_desc *table)
{
	return false;
}
