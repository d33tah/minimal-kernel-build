 
 
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/export.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/limits.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/string_helpers.h>

 
void string_get_size(u64 size, u64 blk_size, const enum string_size_units units,
		     char *buf, int len)
{
	static const char *const units_10[] = {
		"B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"
	};
	static const char *const units_2[] = {
		"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"
	};
	static const char *const *const units_str[] = {
		[STRING_UNITS_10] = units_10,
		[STRING_UNITS_2] = units_2,
	};
	static const unsigned int divisor[] = {
		[STRING_UNITS_10] = 1000,
		[STRING_UNITS_2] = 1024,
	};
	static const unsigned int rounding[] = { 500, 50, 5 };
	int i = 0, j;
	u32 remainder = 0, sf_cap;
	char tmp[8];
	const char *unit;

	tmp[0] = '\0';

	if (blk_size == 0)
		size = 0;
	if (size == 0)
		goto out;

	 
	while (blk_size >> 32) {
		do_div(blk_size, divisor[units]);
		i++;
	}

	while (size >> 32) {
		do_div(size, divisor[units]);
		i++;
	}

	 
	size *= blk_size;

	 
	while (size >= divisor[units]) {
		remainder = do_div(size, divisor[units]);
		i++;
	}

	 
	sf_cap = size;
	for (j = 0; sf_cap*10 < 1000; j++)
		sf_cap *= 10;

	if (units == STRING_UNITS_2) {
		 
		remainder *= 1000;
		remainder >>= 10;
	}

	 
	remainder += rounding[j];
	if (remainder >= 1000) {
		remainder -= 1000;
		size += 1;
	}

	if (j) {
		snprintf(tmp, sizeof(tmp), ".%03u", remainder);
		tmp[j+1] = '\0';
	}

 out:
	if (i >= ARRAY_SIZE(units_2))
		unit = "UNK";
	else
		unit = units_str[units][i];

	snprintf(buf, len, "%u%s %s", (u32)size,
		 tmp, unit);
}

static bool unescape_space(char **src, char **dst)
{
	return false;  
}

static bool unescape_octal(char **src, char **dst)
{
	return false;  
}

static bool unescape_hex(char **src, char **dst)
{
	return false;  
}

static bool unescape_special(char **src, char **dst)
{
	return false;  
}

 
int string_unescape(char *src, char *dst, size_t size, unsigned int flags)
{
	char *out = dst;

	while (*src && --size) {
		if (src[0] == '\\' && src[1] != '\0' && size > 1) {
			src++;
			size--;

			if (flags & UNESCAPE_SPACE &&
					unescape_space(&src, &out))
				continue;

			if (flags & UNESCAPE_OCTAL &&
					unescape_octal(&src, &out))
				continue;

			if (flags & UNESCAPE_HEX &&
					unescape_hex(&src, &out))
				continue;

			if (flags & UNESCAPE_SPECIAL &&
					unescape_special(&src, &out))
				continue;

			*out++ = '\\';
		}
		*out++ = *src++;
	}
	*out = '\0';

	return out - dst;
}

static bool escape_passthrough(unsigned char c, char **dst, char *end)
{
	char *out = *dst;

	if (out < end)
		*out = c;
	*dst = out + 1;
	return true;
}

static bool escape_space(unsigned char c, char **dst, char *end)
{
	return false;  
}

static bool escape_special(unsigned char c, char **dst, char *end)
{
	return false;  
}

static bool escape_null(unsigned char c, char **dst, char *end)
{
	return false;  
}

static bool escape_octal(unsigned char c, char **dst, char *end)
{
	return false;  
}

static bool escape_hex(unsigned char c, char **dst, char *end)
{
	return false;  
}

 
int string_escape_mem(const char *src, size_t isz, char *dst, size_t osz,
		      unsigned int flags, const char *only)
{
	char *p = dst;
	char *end = p + osz;
	bool is_dict = only && *only;
	bool is_append = flags & ESCAPE_APPEND;

	while (isz--) {
		unsigned char c = *src++;
		bool in_dict = is_dict && strchr(only, c);

		 
		if (!(is_append || in_dict) && is_dict &&
					  escape_passthrough(c, &p, end))
			continue;

		if (!(is_append && in_dict) && isascii(c) && isprint(c) &&
		    flags & ESCAPE_NAP && escape_passthrough(c, &p, end))
			continue;

		if (!(is_append && in_dict) && isprint(c) &&
		    flags & ESCAPE_NP && escape_passthrough(c, &p, end))
			continue;

		if (!(is_append && in_dict) && isascii(c) &&
		    flags & ESCAPE_NA && escape_passthrough(c, &p, end))
			continue;

		if (flags & ESCAPE_SPACE && escape_space(c, &p, end))
			continue;

		if (flags & ESCAPE_SPECIAL && escape_special(c, &p, end))
			continue;

		if (flags & ESCAPE_NULL && escape_null(c, &p, end))
			continue;

		 
		if (flags & ESCAPE_OCTAL && escape_octal(c, &p, end))
			continue;

		if (flags & ESCAPE_HEX && escape_hex(c, &p, end))
			continue;

		escape_passthrough(c, &p, end);
	}

	return p - dst;
}

 
char *kstrdup_quotable(const char *src, gfp_t gfp)
{
	size_t slen, dlen;
	char *dst;
	const int flags = ESCAPE_HEX;
	const char esc[] = "\f\n\r\t\v\a\e\\\"";

	if (!src)
		return NULL;
	slen = strlen(src);

	dlen = string_escape_mem(src, slen, NULL, 0, flags, esc);
	dst = kmalloc(dlen + 1, gfp);
	if (!dst)
		return NULL;

	WARN_ON(string_escape_mem(src, slen, dst, dlen, flags, esc) != dlen);
	dst[dlen] = '\0';

	return dst;
}

 
char *kstrdup_quotable_cmdline(struct task_struct *task, gfp_t gfp)
{
	char *buffer, *quoted;
	int i, res;

	buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buffer)
		return NULL;

	res = get_cmdline(task, buffer, PAGE_SIZE - 1);
	buffer[res] = '\0';

	 
	while (--res >= 0 && buffer[res] == '\0')
		;

	 
	for (i = 0; i <= res; i++)
		if (buffer[i] == '\0')
			buffer[i] = ' ';

	 
	quoted = kstrdup_quotable(buffer, gfp);
	kfree(buffer);
	return quoted;
}

 
char *kstrdup_quotable_file(struct file *file, gfp_t gfp)
{
	char *temp, *pathname;

	if (!file)
		return kstrdup("<unknown>", gfp);

	 
	temp = kmalloc(PATH_MAX + 11, GFP_KERNEL);
	if (!temp)
		return kstrdup("<no_memory>", gfp);

	pathname = file_path(file, temp, PATH_MAX + 11);
	if (IS_ERR(pathname))
		pathname = kstrdup("<too_long>", gfp);
	else
		pathname = kstrdup_quotable(pathname, gfp);

	kfree(temp);
	return pathname;
}

 
char **kasprintf_strarray(gfp_t gfp, const char *prefix, size_t n)
{
	char **names;
	size_t i;

	names = kcalloc(n + 1, sizeof(char *), gfp);
	if (!names)
		return NULL;

	for (i = 0; i < n; i++) {
		names[i] = kasprintf(gfp, "%s-%zu", prefix, i);
		if (!names[i]) {
			kfree_strarray(names, i);
			return NULL;
		}
	}

	return names;
}

 
void kfree_strarray(char **array, size_t n)
{
	unsigned int i;

	if (!array)
		return;

	for (i = 0; i < n; i++)
		kfree(array[i]);
	kfree(array);
}

struct strarray {
	char **array;
	size_t n;
};

static void devm_kfree_strarray(struct device *dev, void *res)
{
	struct strarray *array = res;

	kfree_strarray(array->array, array->n);
}

char **devm_kasprintf_strarray(struct device *dev, const char *prefix, size_t n)
{
	struct strarray *ptr;

	ptr = devres_alloc(devm_kfree_strarray, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	ptr->array = kasprintf_strarray(GFP_KERNEL, prefix, n);
	if (!ptr->array) {
		devres_free(ptr);
		return ERR_PTR(-ENOMEM);
	}

	ptr->n = n;
	devres_add(dev, ptr);

	return ptr->array;
}

 
ssize_t strscpy_pad(char *dest, const char *src, size_t count)
{
	ssize_t written;

	written = strscpy(dest, src, count);
	if (written < 0 || written == count - 1)
		return written;

	memset(dest + written + 1, 0, count - written - 1);

	return written;
}

 
char *skip_spaces(const char *str)
{
	while (isspace(*str))
		++str;
	return (char *)str;
}

 
char *strim(char *s)
{
	size_t size;
	char *end;

	size = strlen(s);
	if (!size)
		return s;

	end = s + size - 1;
	while (end >= s && isspace(*end))
		end--;
	*(end + 1) = '\0';

	return skip_spaces(s);
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

 
int match_string(const char * const *array, size_t n, const char *string)
{
	int index;
	const char *item;

	for (index = 0; index < n; index++) {
		item = array[index];
		if (!item)
			break;
		if (!strcmp(item, string))
			return index;
	}

	return -EINVAL;
}

 
int __sysfs_match_string(const char * const *array, size_t n, const char *str)
{
	const char *item;
	int index;

	for (index = 0; index < n; index++) {
		item = array[index];
		if (!item)
			break;
		if (sysfs_streq(item, str))
			return index;
	}

	return -EINVAL;
}

 
char *strreplace(char *s, char old, char new)
{
	for (; *s; ++s)
		if (*s == old)
			*s = new;
	return s;
}

 
void memcpy_and_pad(void *dest, size_t dest_len, const void *src, size_t count,
		    int pad)
{
	if (dest_len > count) {
		memcpy(dest, src, count);
		memset(dest + count, pad,  dest_len - count);
	} else {
		memcpy(dest, src, dest_len);
	}
}

