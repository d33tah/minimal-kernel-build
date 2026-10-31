#ifndef _LINUX_STRING_H_
#define _LINUX_STRING_H_

#include <linux/compiler.h>
#include <linux/errno.h>

#include <asm/string_32.h>

size_t strlcpy(char *, const char *, size_t);

ssize_t strscpy_pad(char *dest, const char *src, size_t count);

extern char *strrchr(const char *, int);

extern int memcmp(const void *, const void *, __kernel_size_t);

static inline const char *kbasename(const char *path)
{
	const char *tail = strrchr(path, '/');
	return tail ? tail + 1 : path;
}

#endif
