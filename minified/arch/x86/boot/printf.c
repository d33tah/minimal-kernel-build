

#include "boot.h"

/* Minimal number output: base-10 only, signed or unsigned */
static char *number(char *str, long num, int is_signed)
{
	char tmp[22];
	int i = 0;

	if (is_signed && num < 0) {
		*str++ = '-';
		num = -num;
	}
	if (num == 0)
		tmp[i++] = '0';
	else
		while (num != 0) {
			tmp[i++] = '0' + ((unsigned long)num) % 10;
			num = ((unsigned long)num) / 10;
		}
	while (i-- > 0)
		*str++ = tmp[i];
	return str;
}

/* Minimal vsprintf: only %d, %s, %lu, %u, %% (all boot code needs) */
int vsprintf(char *buf, const char *fmt, va_list args)
{
	char *str;
	const char *s;

	for (str = buf; *fmt; ++fmt) {
		if (*fmt != '%') {
			*str++ = *fmt;
			continue;
		}
		++fmt;
		switch (*fmt) {
		case 's':
			s = va_arg(args, char *);
			while (*s)
				*str++ = *s++;
			continue;
		case '%':
			*str++ = '%';
			continue;
		case 'l':
			++fmt; /* skip 'u' after 'l' */
			str = number(str, va_arg(args, unsigned long), 0);
			continue;
		case 'd':
		case 'i':
			str = number(str, va_arg(args, int), 1);
			continue;
		case 'u':
			str = number(str, va_arg(args, unsigned int), 0);
			continue;
		default:
			*str++ = '%';
			if (*fmt)
				*str++ = *fmt;
			else
				--fmt;
			continue;
		}
	}
	*str = '\0';
	return str - buf;
}

int sprintf(char *buf, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsprintf(buf, fmt, args);
	va_end(args);
	return i;
}

int printf(const char *fmt, ...)
{
	char printf_buf[1024];
	va_list args;
	int printed;

	va_start(args, fmt);
	printed = vsprintf(printf_buf, fmt, args);
	va_end(args);

	puts(printf_buf);

	return printed;
}
