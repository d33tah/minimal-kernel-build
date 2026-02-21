
#include <linux/ctype.h>
#include <linux/uaccess.h>

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

static noinline_for_stack char *
number(char *buf, char *end, unsigned long long num, struct printf_spec spec)
{
	char tmp[3 * sizeof(num)] __aligned(2);
	char sign;
	char locase;
	int need_pfx = ((spec.flags & SPECIAL) && spec.base != 10);
	int i;
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
	if (need_pfx)
		field_width -= 2;

	i = 0;
	if (num < spec.base)
		tmp[i++] = hex_asc_upper[num] | locase;
	else if (spec.base == 16) {
		do {
			tmp[i++] = (hex_asc_upper[((unsigned char)num) & 0xf] |
				    locase);
			num >>= 4;
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
		if (buf < end)
			*buf = '0';
		++buf;
		if (buf < end)
			*buf = ('X' | locase);
		++buf;
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

static noinline_for_stack char *string(char *buf, char *end, const char *s,
				       struct printf_spec spec)
{
	int lim;

	if (!s)
		s = "(null)";

	lim = spec.precision;
	while (lim--) {
		char c = *s++;
		if (!c)
			break;
		if (buf < end)
			*buf = c;
		++buf;
	}
	return buf;
}

static noinline_for_stack char *pointer(const char *fmt, char *buf, char *end,
					void *ptr, struct printf_spec spec)
{
	spec.base = 16;
	spec.flags |= SMALL;
	if (spec.field_width == -1) {
		spec.field_width = 2 * sizeof(ptr);
		spec.flags |= ZEROPAD;
	}

	return number(buf, end, (unsigned long int)ptr, spec);
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

int sprintf(char *buf, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsnprintf(buf, INT_MAX, fmt, args);
	va_end(args);

	return i;
}
