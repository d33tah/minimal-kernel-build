/* STUB: string helper functions - minimal implementations */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/string_helpers.h>

void string_get_size(u64 size, u64 blk_size, const enum string_size_units units,
		     char *buf, int len) { BUG(); }
int string_unescape(char *src, char *dst, size_t size, unsigned int flags) { BUG(); }
int string_escape_mem(const char *src, size_t isz, char *dst, size_t osz,
		      unsigned int flags, const char *only) { BUG(); }
void kfree_strarray(char **array, size_t n) { BUG(); }

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

int __sysfs_match_string(const char * const *array, size_t n, const char *str)
{
	return match_string(array, n, str);
}

void memcpy_and_pad(void *dest, size_t dest_len, const void *src, size_t count,
		    int pad) { BUG(); }

char *skip_spaces(const char *str)
{
	while (isspace(*str))
		++str;
	return (char *)str;
}

char *strreplace(char *s, char old, char new)
{
	for (; *s; ++s)
		if (*s == old)
			*s = new;
	return s;
}

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
/* EXPORT_SYMBOL removed - monolithic kernel without modules */
