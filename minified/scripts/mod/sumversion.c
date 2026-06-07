/* Stubbed - no loadable modules, source versioning not needed */
#include "modpost.h"

/* Empty implementation - never called without loadable modules */
void get_src_version(const char *modname, char sum[], unsigned sumlen)
{
	/* No source versioning needed for builtin modules */
	if (sumlen > 0)
		sum[0] = '\0';
}
