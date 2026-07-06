/* Minimal includes for kasprintf */
#include <linux/string.h>

char *kvasprintf(gfp_t gfp, const char *fmt, va_list ap)
{
	/* Runtime-dead: the only live caller (kvasprintf_const) takes the
	 * strchr('%')/"%s" fast paths at boot and never reaches here. */
	return NULL;
}

const char *kvasprintf_const(gfp_t gfp, const char *fmt, va_list ap)
{
	if (!strchr(fmt, '%'))
		return kstrdup_const(fmt, gfp);
	if (!strcmp(fmt, "%s"))
		return kstrdup_const(va_arg(ap, const char*), gfp);
	return kvasprintf(gfp, fmt, ap);
}
