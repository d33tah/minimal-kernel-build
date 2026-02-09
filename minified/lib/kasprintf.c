/* Minimal includes for kasprintf */
#include <linux/slab.h>
#include <linux/string.h>

char *kvasprintf(gfp_t gfp, const char *fmt, va_list ap)
{
	unsigned int first, second;
	char *p;
	va_list aq;

	va_copy(aq, ap);
	first = vsnprintf(NULL, 0, fmt, aq);
	va_end(aq);

	p = kmalloc_track_caller(first + 1, gfp);
	if (!p)
		return NULL;

	second = vsnprintf(p, first + 1, fmt, ap);
	WARN(first != second,
	     "different return values (%u and %u) from vsnprintf(\"%s\", ...)",
	     first, second, fmt);

	return p;
}

char *kasprintf(gfp_t gfp, const char *fmt, ...)
{
	va_list ap;
	char *p;

	va_start(ap, fmt);
	p = kvasprintf(gfp, fmt, ap);
	va_end(ap);

	return p;
}
