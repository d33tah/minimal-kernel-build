
#include <linux/stdarg.h>
#include <linux/build_bug.h>
/* errname removed - unused */
/* linux/module.h removed - no EXPORT_SYMBOL */
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
/* kallsyms.h removed - only dereference_symbol_descriptor was used (no-op) */
/* math64.h removed - unused */
#include <linux/uaccess.h>
/* ioport.h, dcache.h, cred.h, rtc.h, time.h removed - unused */
/* uuid.h removed - guid_t not used */
/* of.h removed - device_node not used in vsprintf */

/* Removed: linux/random.h, linux/siphash.h - ptr hashing disabled */
/* IPV6_FLOWINFO_MASK removed - unused */

#include <linux/compiler.h>

#include "../mm/internal.h"

#include <asm/page.h>
#include <asm/byteorder.h>
/* asm/unaligned.h removed - get_unaligned not used */

/* string_helpers.h removed - unused */

/* no_hash_pointers removed - always false, never set */

/* simple_strtoull, simple_strtoul removed - only caller was name_to_dev_t (removed from do_mounts.c) */

static noinline_for_stack int skip_atoi(const char **s)
{
	int i = 0;

	do {
		i = i * 10 + *((*s)++) - '0';
	} while (isdigit(**s));

	return i;
}

static const u16 decpair[100] = {
#define _(x) (__force u16) cpu_to_le16(((x % 10) | ((x / 10) << 8)) + 0x3030)
	_(0),  _(1),  _(2),  _(3),  _(4),  _(5),  _(6),	 _(7),	_(8),  _(9),
	_(10), _(11), _(12), _(13), _(14), _(15), _(16), _(17), _(18), _(19),
	_(20), _(21), _(22), _(23), _(24), _(25), _(26), _(27), _(28), _(29),
	_(30), _(31), _(32), _(33), _(34), _(35), _(36), _(37), _(38), _(39),
	_(40), _(41), _(42), _(43), _(44), _(45), _(46), _(47), _(48), _(49),
	_(50), _(51), _(52), _(53), _(54), _(55), _(56), _(57), _(58), _(59),
	_(60), _(61), _(62), _(63), _(64), _(65), _(66), _(67), _(68), _(69),
	_(70), _(71), _(72), _(73), _(74), _(75), _(76), _(77), _(78), _(79),
	_(80), _(81), _(82), _(83), _(84), _(85), _(86), _(87), _(88), _(89),
	_(90), _(91), _(92), _(93), _(94), _(95), _(96), _(97), _(98), _(99),
#undef _
};

static noinline_for_stack char *put_dec_trunc8(char *buf, unsigned r)
{
	unsigned q;

	if (r < 100)
		goto out_r;

	q = (r * (u64)0x28f5c29) >> 32;
	*((u16 *)buf) = decpair[r - 100 * q];
	buf += 2;

	if (q < 100)
		goto out_q;

	r = (q * (u64)0x28f5c29) >> 32;
	*((u16 *)buf) = decpair[q - 100 * r];
	buf += 2;

	if (r < 100)
		goto out_r;

	q = (r * 0x147b) >> 19;
	*((u16 *)buf) = decpair[r - 100 * q];
	buf += 2;
out_q:

	r = q;
out_r:

	*((u16 *)buf) = decpair[r];
	buf += r < 10 ? 1 : 2;
	return buf;
}

/* BITS_PER_LONG == 32 on x86-32, use 32-bit optimized put_dec */

static noinline_for_stack unsigned put_dec_helper4(char *buf, unsigned x)
{
	uint32_t q = (x * (uint64_t)0x346DC5D7) >> 43;
	/* Inlined put_dec_full4 */
	unsigned r = x - q * 10000;
	unsigned rq = (r * 0x147b) >> 19;
	*((u16 *)buf) = decpair[r - 100 * rq];
	*((u16 *)(buf + 2)) = decpair[rq];
	return q;
}

static char *put_dec(char *buf, unsigned long long n)
{
	uint32_t d3, d2, d1, q, h;

	if (n < 100 * 1000 * 1000)
		return put_dec_trunc8(buf, n);

	d1 = ((uint32_t)n >> 16);
	h = (n >> 32);
	d2 = (h) & 0xffff;
	d3 = (h >> 16);

	q = 656 * d3 + 7296 * d2 + 5536 * d1 + ((uint32_t)n & 0xffff);
	q = put_dec_helper4(buf, q);

	q += 7671 * d3 + 9496 * d2 + 6 * d1;
	q = put_dec_helper4(buf + 4, q);

	q += 4749 * d3 + 42 * d2;
	q = put_dec_helper4(buf + 8, q);

	q += 281 * d3;
	buf += 12;
	if (q)
		buf = put_dec_trunc8(buf, q);
	else
		while (buf[-1] == '0')
			--buf;

	return buf;
}

#define SIGN 1
#define LEFT 2
#define PLUS 4
#define SPACE 8
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
	if (spec.flags & LEFT)
		spec.flags &= ~ZEROPAD;
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
	if (!(spec.flags & (ZEROPAD | LEFT))) {
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

	if (!(spec.flags & LEFT)) {
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

	while (--field_width >= 0) {
		if (buf < end)
			*buf = ' ';
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

/* move_right inlined into widen_string */

static noinline_for_stack char *widen_string(char *buf, int n, char *end,
					     struct printf_spec spec)
{
	unsigned spaces;

	if (likely(n >= spec.field_width))
		return buf;

	spaces = spec.field_width - n;
	if (!(spec.flags & LEFT)) {
		/* Inlined move_right(buf - n, end, n, spaces) */
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
		return buf + spaces;
	}
	while (spaces--) {
		if (buf < end)
			*buf = ' ';
		++buf;
	}
	return buf;
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
	/* Inlined check_pointer_msg */
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
/* ptr_to_id, default_pointer removed - just called pointer_string */
/* restricted_pointer removed - never called */

/* dentry_name, file_dentry_name, symbol_string, va_format, address_val, flags_string inlined */

static noinline_for_stack char *pointer(const char *fmt, char *buf, char *end,
					void *ptr, struct printf_spec spec)
{
	switch (*fmt) {
	case 'S':
	case 's':
		/* dereference_symbol_descriptor removed - was no-op (just returned ptr) */
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
		case '-':
			spec->flags |= LEFT;
			break;
		/* PLUS and SPACE flags removed - never used in kernel */
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

precision:

	spec->precision = -1;
	if (*fmt == '.') {
		++fmt;
		if (isdigit(*fmt)) {
			spec->precision = skip_atoi(&fmt);
			if (spec->precision < 0)
				spec->precision = 0;
		}
	}

qualifier:

	qualifier = 0;
	if (*fmt == 'h' || _tolower(*fmt) == 'l' || *fmt == 'z' ||
	    *fmt == 't') {
		qualifier = *fmt++;
		if (unlikely(qualifier == *fmt)) {
			if (qualifier == 'l') {
				qualifier = 'L';
				++fmt;
			} else if (qualifier == 'h') {
				qualifier = 'H';
				++fmt;
			}
		}
	}

	spec->base = 10;
	switch (*fmt) {
	/* %c removed - unused in kernel printk */
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

	case 'n':

		fallthrough;

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
		/* FORMAT_TYPE_PTRDIFF, BYTE, SHORT qualifiers removed - unused */
	} else {
		BUILD_BUG_ON(FORMAT_TYPE_UINT + SIGN != FORMAT_TYPE_INT);
		spec->type = FORMAT_TYPE_UINT + (spec->flags & SIGN);
	}

	return ++fmt - start;
}

/* set_field_width and set_precision inlined into vsnprintf */

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

			/* FORMAT_TYPE_WIDTH and FORMAT_TYPE_PRECISION removed - %*d never used */

			/* FORMAT_TYPE_CHAR removed - %c unused in kernel */

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
			/* FORMAT_TYPE_PTRDIFF, BYTE, SHORT cases removed - unused */
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

/* scnprintf removed - only caller was mount_block_root (removed from do_mounts.c) */

/* vsprintf removed - no callers in main kernel */

int sprintf(char *buf, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsnprintf(buf, INT_MAX, fmt, args);
	va_end(args);

	return i;
}

/* sscanf removed - only caller was name_to_dev_t (removed from do_mounts.c) */
