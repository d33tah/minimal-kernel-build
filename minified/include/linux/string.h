#ifndef _LINUX_STRING_H_
#define _LINUX_STRING_H_

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/stdarg.h>

#include <asm/string_32.h>

size_t strlcpy(char *, const char *, size_t);

ssize_t strscpy_pad(char *dest, const char *src, size_t count);

extern char *strrchr(const char *, int);

extern int memcmp(const void *, const void *, __kernel_size_t);

extern void kfree_const(const void *x);

extern char *kstrdup(const char *s, gfp_t gfp) __malloc;
extern const char *kstrdup_const(const char *s, gfp_t gfp);

static inline const char *kbasename(const char *path)
{
	const char *tail = strrchr(path, '/');
	return tail ? tail + 1 : path;
}

#endif
