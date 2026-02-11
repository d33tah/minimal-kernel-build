#include <linux/ctype.h>
#include <linux/errno.h>
/* linux/export.h removed - no EXPORT_SYMBOL used */
/* Inlined from kstrtox.h */
int __must_check kstrtoull(const char *s, unsigned int base,
			   unsigned long long *res);
int __must_check kstrtouint(const char *s, unsigned int base,
			    unsigned int *res);
/* simple_strtoul declaration removed - function removed from vsprintf.c */
#include <linux/math64.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define KSTRTOX_OVERFLOW (1U << 31)

noinline const char *_parse_integer_fixup_radix(const char *s,
						unsigned int *base)
{
	if (*base == 0) {
		if (s[0] == '0') {
			if (_tolower(s[1]) == 'x' && isxdigit(s[2]))
				*base = 16;
			else
				*base = 8;
		} else
			*base = 10;
	}
	if (*base == 16 && s[0] == '0' && _tolower(s[1]) == 'x')
		s += 2;
	return s;
}

noinline unsigned int _parse_integer_limit(const char *s, unsigned int base,
					   unsigned long long *p,
					   size_t max_chars)
{
	unsigned long long res;
	unsigned int rv;

	res = 0;
	rv = 0;
	while (max_chars--) {
		unsigned int c = *s;
		unsigned int lc = c | 0x20;
		unsigned int val;

		if ('0' <= c && c <= '9')
			val = c - '0';
		else if ('a' <= lc && lc <= 'f')
			val = lc - 'a' + 10;
		else
			break;

		if (val >= base)
			break;

		if (unlikely(res & (~0ull << 60))) {
			if (res > div_u64(ULLONG_MAX - val, base))
				rv |= KSTRTOX_OVERFLOW;
		}
		res = res * base + val;
		rv++;
		s++;
	}
	*p = res;
	return rv;
}

noinline unsigned int _parse_integer(const char *s, unsigned int base,
				     unsigned long long *p)
{
	return _parse_integer_limit(s, base, p, INT_MAX);
}

noinline int kstrtoull(const char *s, unsigned int base,
		       unsigned long long *res)
{
	unsigned long long _res;
	unsigned int rv;

	if (s[0] == '+')
		s++;
	s = _parse_integer_fixup_radix(s, &base);
	rv = _parse_integer(s, base, &_res);
	if (rv & KSTRTOX_OVERFLOW)
		return -ERANGE;
	if (rv == 0)
		return -EINVAL;
	s += rv;
	if (*s == '\n')
		s++;
	if (*s)
		return -EINVAL;
	*res = _res;
	return 0;
}

/* kstrtoll, _kstrtoul, _kstrtol removed - no callers */

noinline int kstrtouint(const char *s, unsigned int base, unsigned int *res)
{
	unsigned long long tmp;
	int rv;

	rv = kstrtoull(s, base, &tmp);
	if (rv < 0)
		return rv;
	if (tmp != (unsigned int)tmp)
		return -ERANGE;
	*res = tmp;
	return 0;
}
/* kstrtoint, kstrtou16, kstrtos16, kstrtou8, kstrtobool removed - no callers */
