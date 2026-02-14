
#include <linux/stdarg.h>
#include <linux/build_bug.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>

#include <linux/compiler.h>

#include "../mm/internal.h"

#include <asm/page.h>
#include <asm/byteorder.h>

static noinline_for_stack int skip_atoi(const char **s)
{
	int i = 0;

	do {
		i = i * 10 + *((*s)++) - '0';
	} while (isdigit(**s));

	return i;
}

static char *put_dec(char *buf, unsigned long long n)
{
	do {
		*buf++ = '0' + do_div(n, 10);
	} while (n);
	return buf;
}

#define SIGN 1
#define ZEROPAD 16
#define SMALL 32
#define SPECIAL 64

static_assert(SIGN == 1);
static_assert(ZEROPAD == ('0' - ' '));
static_assert(SMALL == ('a' ^ 'A'));

enum format_type {
	FORMAT_TYPE_NONE,
	FORMAT_TYPE_STR = 4,
	FORMAT_TYPE_PTR,
	FORMAT_TYPE_PERCENT_CHAR,
	FORMAT_TYPE_INVALID,
	FORMAT_TYPE_LONG_LONG,
	FORMAT_TYPE_ULONG,
	FORMAT_TYPE_LONG,
	FORMAT_TYPE_UINT,
	FORMAT_TYPE_INT,
	FORMAT_TYPE_SIZE_T
};

struct printf_spec {
	unsigned int type : 8;
	signed int field_width : 24;
	unsigned int flags : 8;
	unsigned int base : 8;
	signed int precision : 16;
} __packed;
static_assert(sizeof(struct printf_spec) == 8);

#define FIELD_WIDTH_MAX ((1 << 23) - 1)
#define PRECISION_MAX ((1 << 15) - 1)

static noinline_for_stack char *
number(char *buf, char *end, unsigned long long num, struct printf_spec spec)
{
	char tmp[3 * sizeof(num)] __aligned(2);
	char sign;
	char locase;
	int need_pfx = ((spec.flags & SPECIAL) && spec.base != 10);
	int i;
	bool is_zero = num == 0LL;
	int field_width = spec.field_width;
	int precision = spec.precision;

	locase = (spec.flags & SMALL);
	sign = 0;
	if (spec.flags & SIGN) {
		if ((signed long long)num < 0) {
			sign = '-';
			num = -(signed long long)num;
			field_width--;
		}
	}
	if (need_pfx) {
		if (spec.base == 16)
			field_width -= 2;
		else if (!is_zero)
			field_width--;
	}

	i = 0;
	if (num < spec.base)
		tmp[i++] = hex_asc_upper[num] | locase;
	else if (spec.base != 10) {
		int mask = spec.base - 1;
		int shift = 3;

		if (spec.base == 16)
			shift = 4;
		do {
			tmp[i++] = (hex_asc_upper[((unsigned char)num) & mask] |
				    locase);
			num >>= shift;
		} while (num);
	} else {
		i = put_dec(tmp, num) - tmp;
	}

	if (i > precision)
		precision = i;

	field_width -= precision;
	if (!(spec.flags & ZEROPAD)) {
		while (--field_width >= 0) {
			if (buf < end)
				*buf = ' ';
			++buf;
		}
	}

	if (sign) {
		if (buf < end)
			*buf = sign;
		++buf;
	}

	if (need_pfx) {
		if (spec.base == 16 || !is_zero) {
			if (buf < end)
				*buf = '0';
			++buf;
		}
		if (spec.base == 16) {
			if (buf < end)
				*buf = ('X' | locase);
			++buf;
		}
	}

	{
		char c = ' ' + (spec.flags & ZEROPAD);

		while (--field_width >= 0) {
			if (buf < end)
				*buf = c;
			++buf;
		}
	}

	while (i <= --precision) {
		if (buf < end)
			*buf = '0';
		++buf;
	}

	while (--i >= 0) {
		if (buf < end)
			*buf = tmp[i];
		++buf;
	}

	return buf;
}

static noinline_for_stack char *
special_hex_number(char *buf, char *end, unsigned long long num, int size)
{
	struct printf_spec spec;

	spec.type = FORMAT_TYPE_PTR;
	spec.field_width = 2 + 2 * size;
	spec.flags = SPECIAL | SMALL | ZEROPAD;
	spec.base = 16;
	spec.precision = -1;

	return number(buf, end, num, spec);
}

static noinline_for_stack char *widen_string(char *buf, int n, char *end,
					     struct printf_spec spec)
{
	unsigned spaces;

	if (likely(n >= spec.field_width))
		return buf;

	spaces = spec.field_width - n;
	{
		char *mr_buf = buf - n;
		unsigned mr_len = n;
		if (mr_buf < end) {
			size_t size = end - mr_buf;
			if (size <= spaces)
				memset(mr_buf, ' ', size);
			else {
				if (mr_len) {
					if (mr_len > size - spaces)
						mr_len = size - spaces;
					memmove(mr_buf + spaces, mr_buf,
						mr_len);
				}
				memset(mr_buf, ' ', spaces);
			}
		}
	}
	return buf + spaces;
}

static char *string_nocheck(char *buf, char *end, const char *s,
			    struct printf_spec spec)
{
	int len = 0;
	int lim = spec.precision;

	while (lim--) {
		char c = *s++;
		if (!c)
			break;
		if (buf < end)
			*buf = c;
		++buf;
		++len;
	}
	return widen_string(buf, len, end, spec);
}

static char *error_string(char *buf, char *end, const char *s,
			  struct printf_spec spec)
{
	if (spec.precision == -1)
		spec.precision = 2 * sizeof(void *);

	return string_nocheck(buf, end, s, spec);
}

static int check_pointer(char **buf, char *end, const void *ptr,
			 struct printf_spec spec)
{
	const char *err_msg = NULL;
	if (!ptr)
		err_msg = "(null)";
	else if ((unsigned long)ptr < PAGE_SIZE || IS_ERR_VALUE(ptr))
		err_msg = "(efault)";

	if (err_msg) {
		*buf = error_string(*buf, end, err_msg, spec);
		return -EFAULT;
	}

	return 0;
}

static noinline_for_stack char *string(char *buf, char *end, const char *s,
				       struct printf_spec spec)
{
	if (check_pointer(&buf, end, s, spec))
		return buf;

	return string_nocheck(buf, end, s, spec);
}

static char *pointer_string(char *buf, char *end, const void *ptr,
			    struct printf_spec spec)
{
	spec.base = 16;
	spec.flags |= SMALL;
	if (spec.field_width == -1) {
		spec.field_width = 2 * sizeof(ptr);
		spec.flags |= ZEROPAD;
	}

	return number(buf, end, (unsigned long int)ptr, spec);
}

/* Simplified for minimal kernel - no pointer hashing */

/* dentry_name, file_dentry_name, symbol_string, va_format, address_val, flags_string inlined */

static noinline_for_stack char *pointer(const char *fmt, char *buf, char *end,
					void *ptr, struct printf_spec spec)
{
	switch (*fmt) {
	case 'S':
	case 's':
		fallthrough;
	case 'B':
		/* symbol_string inlined */
		{
			unsigned long value;
			if (fmt[1] == 'R')
				ptr = __builtin_extract_return_addr(ptr);
			value = (unsigned long)ptr;
			return special_hex_number(buf, end, value,
						  sizeof(void *));
		}
	case 'V':
		/* va_format inlined */
		{
			struct va_format *va_fmt = ptr;
			va_list va;
			if (check_pointer(&buf, end, va_fmt, spec))
				return buf;
			va_copy(va, *va_fmt->va);
			buf += vsnprintf(buf, end > buf ? end - buf : 0,
					 va_fmt->fmt, va);
			va_end(va);
			return buf;
		}
	default:
		/* %pa, %pd, %pD, %pG, %px all fall through to default pointer */
		return pointer_string(buf, end, ptr, spec);
	}
}

static noinline_for_stack int format_decode(const char *fmt,
					    struct printf_spec *spec)
{
	const char *start = fmt;
	char qualifier;

	spec->type = FORMAT_TYPE_NONE;

	for (; *fmt; ++fmt) {
		if (*fmt == '%')
			break;
	}

	if (fmt != start || !*fmt)
		return fmt - start;

	spec->flags = 0;

	while (1) {
		bool found = true;

		++fmt;

		switch (*fmt) {
		case '#':
			spec->flags |= SPECIAL;
			break;
		case '0':
			spec->flags |= ZEROPAD;
			break;
		default:
			found = false;
		}

		if (!found)
			break;
	}

	spec->field_width = -1;

	if (isdigit(*fmt))
		spec->field_width = skip_atoi(&fmt);

	spec->precision = -1;
	if (*fmt == '.') {
		++fmt;
		if (isdigit(*fmt)) {
			spec->precision = skip_atoi(&fmt);
			if (spec->precision < 0)
				spec->precision = 0;
		}
	}

	qualifier = 0;
	if (_tolower(*fmt) == 'l' || *fmt == 'z') {
		qualifier = *fmt++;
		if (qualifier == 'l' && *fmt == 'l') {
			qualifier = 'L';
			++fmt;
		}
	}

	spec->base = 10;
	switch (*fmt) {
	case 's':
		spec->type = FORMAT_TYPE_STR;
		return ++fmt - start;

	case 'p':
		spec->type = FORMAT_TYPE_PTR;
		return ++fmt - start;

	case '%':
		spec->type = FORMAT_TYPE_PERCENT_CHAR;
		return ++fmt - start;

	case 'o':
		spec->base = 8;
		break;

	case 'x':
		spec->flags |= SMALL;
		fallthrough;

	case 'X':
		spec->base = 16;
		break;

	case 'd':
	case 'i':
		spec->flags |= SIGN;
		break;
	case 'u':
		break;

	default:
		WARN_ONCE(1,
			  "Please remove unsupported %%%c in format string\n",
			  *fmt);
		spec->type = FORMAT_TYPE_INVALID;
		return fmt - start;
	}

	if (qualifier == 'L')
		spec->type = FORMAT_TYPE_LONG_LONG;
	else if (qualifier == 'l') {
		BUILD_BUG_ON(FORMAT_TYPE_ULONG + SIGN != FORMAT_TYPE_LONG);
		spec->type = FORMAT_TYPE_ULONG + (spec->flags & SIGN);
	} else if (qualifier == 'z') {
		spec->type = FORMAT_TYPE_SIZE_T;
	} else {
		BUILD_BUG_ON(FORMAT_TYPE_UINT + SIGN != FORMAT_TYPE_INT);
		spec->type = FORMAT_TYPE_UINT + (spec->flags & SIGN);
	}

	return ++fmt - start;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	unsigned long long num;
	char *str, *end;
	struct printf_spec spec = { 0 };

	if (WARN_ON_ONCE(size > INT_MAX))
		return 0;

	str = buf;
	end = buf + size;

	if (end < buf) {
		end = ((void *)-1);
		size = end - buf;
	}

	while (*fmt) {
		const char *old_fmt = fmt;
		int read = format_decode(fmt, &spec);

		fmt += read;

		switch (spec.type) {
		case FORMAT_TYPE_NONE: {
			int copy = read;
			if (str < end) {
				if (copy > end - str)
					copy = end - str;
				memcpy(str, old_fmt, copy);
			}
			str += read;
			break;
		}

		case FORMAT_TYPE_STR:
			str = string(str, end, va_arg(args, char *), spec);
			break;

		case FORMAT_TYPE_PTR:
			str = pointer(fmt, str, end, va_arg(args, void *),
				      spec);
			while (isalnum(*fmt))
				fmt++;
			break;

		case FORMAT_TYPE_PERCENT_CHAR:
			if (str < end)
				*str = '%';
			++str;
			break;

		case FORMAT_TYPE_INVALID:

			goto out;

		default:
			switch (spec.type) {
			case FORMAT_TYPE_LONG_LONG:
				num = va_arg(args, long long);
				break;
			case FORMAT_TYPE_ULONG:
				num = va_arg(args, unsigned long);
				break;
			case FORMAT_TYPE_LONG:
				num = va_arg(args, long);
				break;
			case FORMAT_TYPE_SIZE_T:
				if (spec.flags & SIGN)
					num = va_arg(args, ssize_t);
				else
					num = va_arg(args, size_t);
				break;
			case FORMAT_TYPE_INT:
				num = (int)va_arg(args, int);
				break;
			default:
				num = va_arg(args, unsigned int);
			}

			str = number(str, end, num, spec);
		}
	}

out:
	if (size > 0) {
		if (str < end)
			*str = '\0';
		else
			end[-1] = '\0';
	}

	return str - buf;
}

int vscnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	int i;

	if (unlikely(!size))
		return 0;

	i = vsnprintf(buf, size, fmt, args);

	if (likely(i < size))
		return i;

	return size - 1;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsnprintf(buf, size, fmt, args);
	va_end(args);

	return i;
}

int sprintf(char *buf, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsnprintf(buf, INT_MAX, fmt, args);
	va_end(args);

	return i;
}
