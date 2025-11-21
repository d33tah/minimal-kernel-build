/* STUB: string helper functions - minimal implementations */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/string_helpers.h>

void string_get_size(u64 size, u64 blk_size, const enum string_size_units units,
		     char *buf, int len)
{
	if (len > 0)
		snprintf(buf, len, "%llu", (unsigned long long)size);
}
EXPORT_SYMBOL(string_get_size);

int string_unescape(char *src, char *dst, size_t size, unsigned int flags)
{
	if (size > 0 && src && dst) {
		size_t len = strlen(src);
		if (len >= size) len = size - 1;
		memcpy(dst, src, len);
		dst[len] = '\0';
		return len;
	}
	return -EINVAL;
}
EXPORT_SYMBOL(string_unescape);

int string_escape_mem(const char *src, size_t isz, char *dst, size_t osz,
		      unsigned int flags, const char *only)
{
	if (osz > 0 && src && dst) {
		size_t len = isz;
		if (len >= osz) len = osz - 1;
		memcpy(dst, src, len);
		dst[len] = '\0';
		return len;
	}
	return -EINVAL;
}
EXPORT_SYMBOL(string_escape_mem);

void kfree_strarray(char **array, size_t n)
{
	/* Stub - do nothing */
}
EXPORT_SYMBOL_GPL(kfree_strarray);

ssize_t strscpy_pad(char *dest, const char *src, size_t count)
{
	size_t len;
	if (!dest || !src || !count)
		return -E2BIG;
	len = strlcpy(dest, src, count);
	if (len >= count)
		return -E2BIG;
	memset(dest + len, 0, count - len);
	return len;
}
EXPORT_SYMBOL(strscpy_pad);

int match_string(const char * const *array, size_t n, const char *string)
{
	int index;
	if (!array || !string)
		return -EINVAL;
	for (index = 0; index < n; index++) {
		if (!array[index])
			break;
		if (!strcmp(array[index], string))
			return index;
	}
	return -EINVAL;
}
EXPORT_SYMBOL(match_string);

int __sysfs_match_string(const char * const *array, size_t n, const char *str)
{
	return match_string(array, n, str);
}
EXPORT_SYMBOL(__sysfs_match_string);

void memcpy_and_pad(void *dest, size_t dest_len, const void *src, size_t count,
		    int pad)
{
	if (!dest || !src)
		return;
	if (count > dest_len)
		count = dest_len;
	memcpy(dest, src, count);
	if (count < dest_len)
		memset(dest + count, pad, dest_len - count);
}
EXPORT_SYMBOL(memcpy_and_pad);

char *skip_spaces(const char *str)
{
	while (isspace(*str))
		++str;
	return (char *)str;
}
EXPORT_SYMBOL(skip_spaces);

char *strreplace(char *s, char old, char new)
{
	for (; *s; ++s)
		if (*s == old)
			*s = new;
	return s;
}
EXPORT_SYMBOL(strreplace);

bool sysfs_streq(const char *s1, const char *s2)
{
	while (*s1 && *s1 == *s2) {
		s1++;
		s2++;
	}
	if (*s1 == *s2)
		return true;
	if (!*s1 && *s2 == '\n' && !s2[1])
		return true;
	if (*s1 == '\n' && !s1[1] && !*s2)
		return true;
	return false;
}
EXPORT_SYMBOL(sysfs_streq);
